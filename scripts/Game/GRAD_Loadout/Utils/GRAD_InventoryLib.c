//------------------------------------------------------------------------------------------------
//! Stateless inventory / slot traversal helpers for GRAD_Loadout.
//!
//! Pure static methods: no entities, no UI, no replication. These wrap the Enfusion inventory
//! API into the few operations the arsenal needs — enumerate editable slots, walk the storage
//! hierarchy, count prefab instances, clear a character, and produce stable slot identifiers.
//!
//! Design notes:
//!  - "Editable" excludes slots locked by LoadoutSlotInfo (fixed cosmetic clothing nodes). The
//!    engine forbids removing those, so we never present them for editing. This is a correctness
//!    constraint, not a style choice.
//!  - Slot identifiers are derived from the storage class name plus the slot id. They are stable
//!    for a given prefab layout and are used both for debug strings and for re-addressing slots
//!    when applying a saved loadout.
class GRAD_InventoryLib
{
	//------------------------------------------------------------------------------------------------
	// Slot identification & naming
	//------------------------------------------------------------------------------------------------

	//! Human-readable, stable identifier for a slot: "<StorageClass>#<slotId>".
	//! e.g. "SCR_WeaponAttachmentsStorageComponent#2". Returns "<none>" for a null slot.
	static string GetSlotDisplayName(InventoryStorageSlot slot)
	{
		if (!slot)
			return "<none>";

		BaseInventoryStorageComponent storage = slot.GetStorage();
		string storageName = "<detached>";
		if (storage)
			storageName = storage.Type().ToString();

		return string.Format("%1#%2", storageName, slot.GetID());
	}

	//------------------------------------------------------------------------------------------------
	//! Debug string describing the slot chain from the leaf item up to its root entity.
	//! Walks parent slots via the owning entity's InventoryItemComponent. Best-effort; intended
	//! for logs, not for addressing.
	static string GetHierarchyPath(IEntity leaf)
	{
		if (!leaf)
			return "<null>";

		array<string> segments = {};
		IEntity current = leaf;
		int guard = 0; // defensive cap against malformed cycles

		while (current && guard < 32)
		{
			guard++;
			segments.Insert(GetEntityShortName(current));

			InventoryItemComponent item = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (!item)
				break;

			InventoryStorageSlot parentSlot = item.GetParentSlot();
			if (!parentSlot)
				break;

			BaseInventoryStorageComponent storage = parentSlot.GetStorage();
			if (!storage)
				break;

			current = storage.GetOwner();
		}

		// Reverse so the path reads root -> leaf.
		array<string> ordered = {};
		for (int i = segments.Count() - 1; i >= 0; i--)
			ordered.Insert(segments[i]);

		return SCR_StringHelper.Join(" > ", ordered);
	}

	//------------------------------------------------------------------------------------------------
	//! Short readable name for an entity, derived from its prefab resource name when available,
	//! otherwise its class name. Used in hierarchy/debug strings.
	static string GetEntityShortName(IEntity entity)
	{
		if (!entity)
			return "<null>";

		ResourceName prefab = GetPrefabResourceName(entity);
		if (prefab != ResourceName.Empty)
			return SCR_StringHelper.FormatResourceNameToUserFriendly(prefab);

		return entity.Type().ToString();
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve the prefab ResourceName an entity was spawned from, or ResourceName.Empty if it has
	//! no prefab data (procedurally spawned, etc.).
	static ResourceName GetPrefabResourceName(IEntity entity)
	{
		if (!entity)
			return ResourceName.Empty;

		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return ResourceName.Empty;

		return prefabData.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	// Storage / slot enumeration
	//------------------------------------------------------------------------------------------------

	//! Direct, top-level storages on an entity (skips nested item storages). Returns the entity's
	//! own BaseInventoryStorageComponents that are not themselves contained inside another storage.
	//! These are the roots the arsenal walks when capturing or clearing a loadout.
	static int GetTopLevelStorages(IEntity entity, out notnull array<BaseInventoryStorageComponent> outStorages)
	{
		outStorages.Clear();
		if (!entity)
			return 0;

		InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(entity.FindComponent(InventoryStorageManagerComponent));
		if (!manager)
			return 0;

		array<BaseInventoryStorageComponent> all = {};
		manager.GetStorages(all);

		foreach (BaseInventoryStorageComponent storage : all)
		{
			if (!storage)
				continue;

			// A top-level storage is not nested inside another storage slot.
			if (!storage.GetParentSlot())
				outStorages.Insert(storage);
		}

		return outStorages.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Enumerate all usable slots on an entity by descending through its storages recursively.
	//! When includeLocked is false (default), slots reporting IsLocked() are skipped — these are
	//! fixed/cosmetic loadout nodes the engine will not let us edit.
	static int EnumerateSlots(IEntity entity, out notnull array<ref GRAD_SlotRef> outSlots, bool includeLocked = false)
	{
		outSlots.Clear();
		if (!entity)
			return 0;

		array<BaseInventoryStorageComponent> roots = {};
		GetTopLevelStorages(entity, roots);

		foreach (BaseInventoryStorageComponent storage : roots)
			CollectSlotsRecursive(storage, outSlots, includeLocked, 0);

		return outSlots.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Recursive worker for EnumerateSlots. Depth guard prevents runaway recursion on malformed
	//! storage graphs.
	protected static void CollectSlotsRecursive(BaseInventoryStorageComponent storage, notnull array<ref GRAD_SlotRef> outSlots, bool includeLocked, int depth)
	{
		if (!storage || depth > 16)
			return;

		int slotCount = storage.GetSlotsCount();
		for (int slotId = 0; slotId < slotCount; slotId++)
		{
			InventoryStorageSlot slot = storage.GetSlot(slotId);
			if (!slot)
				continue;

			if (!includeLocked && slot.IsLocked())
				continue;

			outSlots.Insert(new GRAD_SlotRef(storage, slot, slotId, depth));

			// Descend into a contained item that is itself a storage (vest -> magazines, etc.).
			IEntity contained = storage.Get(slotId);
			if (contained)
			{
				BaseInventoryStorageComponent childStorage = BaseInventoryStorageComponent.Cast(contained.FindComponent(BaseInventoryStorageComponent));
				if (childStorage)
					CollectSlotsRecursive(childStorage, outSlots, includeLocked, depth + 1);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Character-specific weapon slots (primary/secondary/handgun/grenade etc.) via the weapon
	//! manager. Returns the WeaponSlotComponents so the arsenal can address them by slot index.
	static int GetCharacterWeaponSlots(ChimeraCharacter character, out notnull array<WeaponSlotComponent> outSlots)
	{
		outSlots.Clear();
		if (!character)
			return 0;

		BaseWeaponManagerComponent weaponMgr = character.GetWeaponManager();
		if (!weaponMgr)
			return 0;

		array<WeaponSlotComponent> slots = {};
		weaponMgr.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (slot)
				outSlots.Insert(slot);
		}

		return outSlots.Count();
	}

	//------------------------------------------------------------------------------------------------
	// Destination containers (for "choose where a stackable item goes")
	//------------------------------------------------------------------------------------------------

	//! Collect the worn cargo containers on a character that the arsenal can insert stackable items
	//! into — the vest, backpack, uniform/jacket, trousers pockets, etc. Each is returned with a
	//! friendly label (the owning clothing item's short name) so the menu can present a destination
	//! selector.
	//!
	//! A destination container is a storage owned by a worn CARGO-BEARING garment: a backpack, vest,
	//! jacket/uniform, or trousers. Identified by the owner item's arsenal type (via the catalog
	//! index), so weapon/attachment storages (a rifle owns an attachments storage — NOT a cargo
	//! container) are excluded. Character-owned storages (fixed loadout/identity slots) are excluded
	//! too. Empty storages (zero slots) are skipped. Order follows the storage walk.
	//!
	//! `containerTypes` is the set of arsenal-type bits that count as cargo garments (see
	//! GRAD_ContainerTypes.MASK). `typeForPrefab` maps an owner prefab -> its arsenal type; pass
	//! GRAD_CatalogIndex.GetArsenalTypeForPrefab bound via the index (or a 0-returning stub to accept
	//! nothing).
	static int CollectDestinationContainers(IEntity character, GRAD_CatalogIndex index, out notnull array<ref GRAD_ContainerRef> outContainers)
	{
		outContainers.Clear();
		if (!character)
			return 0;

		array<BaseInventoryStorageComponent> roots = {};
		GetTopLevelStorages(character, roots);

		foreach (BaseInventoryStorageComponent storage : roots)
			CollectDestinationContainersRecursive(character, index, storage, outContainers, 0);

		return outContainers.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Recursive worker for CollectDestinationContainers. Descends the storage graph; records a storage
	//! only when its owner is a cargo garment (backpack/vest/jacket/trousers by arsenal type) with
	//! slots. Continues descending so a backpack nested in a vest slot is still found.
	protected static void CollectDestinationContainersRecursive(
		IEntity character,
		GRAD_CatalogIndex index,
		BaseInventoryStorageComponent storage,
		notnull array<ref GRAD_ContainerRef> outContainers,
		int depth)
	{
		if (!storage || depth > 16)
			return;

		IEntity owner = storage.GetOwner();
		if (owner && owner != character && storage.GetSlotsCount() > 0 && index)
		{
			ResourceName ownerPrefab = GetPrefabResourceName(owner);
			int ownerType = index.GetArsenalTypeForPrefab(ownerPrefab);
			if ((ownerType & GRAD_ContainerTypes.MASK) != 0)
			{
				string label = GetEntityShortName(owner);
				outContainers.Insert(new GRAD_ContainerRef(storage, label, owner));
			}
		}

		// Descend into contained items that are themselves storages (backpack inside a slot, etc.).
		int slotCount = storage.GetSlotsCount();
		for (int slotId = 0; slotId < slotCount; slotId++)
		{
			IEntity contained = storage.Get(slotId);
			if (!contained)
				continue;

			BaseInventoryStorageComponent childStorage = BaseInventoryStorageComponent.Cast(contained.FindComponent(BaseInventoryStorageComponent));
			if (childStorage)
				CollectDestinationContainersRecursive(character, index, childStorage, outContainers, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Storage fill (for the loadout panel's fill bars)
	//------------------------------------------------------------------------------------------------

	//! Fill fraction [0,1] of a container storage by SLOT OCCUPANCY (occupied top-level slots / total).
	//! Occupancy is used rather than weight because the verified storage weight API is limited; this is
	//! a robust, always-available metric. Returns 0 for a null/zero-slot storage.
	//!
	//! NOTE: only counts the storage's OWN direct slots (not nested container contents), matching how a
	//! player perceives "how full is my vest".
	static float GetStorageFillFraction(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return 0;

		int total = storage.GetSlotsCount();
		if (total <= 0)
			return 0;

		int used = 0;
		for (int i = 0; i < total; i++)
		{
			if (storage.Get(i))
				used++;
		}

		return used / (float)total;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a container storage has at least one free direct slot (for enabling an ADD button).
	static bool StorageHasFreeSlot(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return false;

		int total = storage.GetSlotsCount();
		for (int i = 0; i < total; i++)
		{
			if (!storage.Get(i))
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	// Item collection & counting
	//------------------------------------------------------------------------------------------------

	//! Flatten every item attached anywhere under an entity into a single list. Includes items
	//! nested inside container items. Order is depth-first by storage then slot.
	static int CollectAllItems(IEntity entity, out notnull array<IEntity> outItems)
	{
		outItems.Clear();
		if (!entity)
			return 0;

		array<BaseInventoryStorageComponent> roots = {};
		GetTopLevelStorages(entity, roots);

		foreach (BaseInventoryStorageComponent storage : roots)
			CollectItemsRecursive(storage, outItems, 0);

		return outItems.Count();
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectItemsRecursive(BaseInventoryStorageComponent storage, notnull array<IEntity> outItems, int depth)
	{
		if (!storage || depth > 16)
			return;

		int slotCount = storage.GetSlotsCount();
		for (int slotId = 0; slotId < slotCount; slotId++)
		{
			IEntity item = storage.Get(slotId);
			if (!item)
				continue;

			outItems.Insert(item);

			BaseInventoryStorageComponent childStorage = BaseInventoryStorageComponent.Cast(item.FindComponent(BaseInventoryStorageComponent));
			if (childStorage)
				CollectItemsRecursive(childStorage, outItems, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Count instances of each prefab across an entity list. Feeds the "quantity owned" display in
	//! the item browser. Entities with no prefab data are skipped.
	static void CountPrefabInstances(notnull array<IEntity> items, out notnull map<ResourceName, int> outCounts)
	{
		outCounts.Clear();

		foreach (IEntity item : items)
		{
			if (!item)
				continue;

			ResourceName prefab = GetPrefabResourceName(item);
			if (prefab == ResourceName.Empty)
				continue;

			int existing = 0;
			outCounts.Find(prefab, existing);
			outCounts.Set(prefab, existing + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Visibility predicate
	//------------------------------------------------------------------------------------------------

	//! Whether an entity should appear in an inventory UI: it must carry an InventoryItemComponent
	//! and not be hidden/locked by the system. Containers (which are also item components) pass.
	static bool IsVisibleInInventory(IEntity entity)
	{
		if (!entity)
			return false;

		InventoryItemComponent item = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		if (!item)
			return false;

		// System-locked items are engine-managed and must not be shown as editable.
		if (item.IsSystemLocked())
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Clearing
	//------------------------------------------------------------------------------------------------

	//! Remove every removable item from an entity's editable storages. With force = false, slots
	//! reporting IsLocked() are skipped. Non-removable items are skipped gracefully (logged at
	//! debug), never fatal. Returns the number of items removed.
	//!
	//! NOTE: This deletes the removed item entities. On networked entities this must run
	//! server-side; for the local preview character it runs locally. The caller is responsible for
	//! choosing the correct context — this helper does not check authority.
	static int ClearStorages(IEntity entity, bool force = false)
	{
		if (!entity)
			return 0;

		// TryRemoveItemFromInventory lives on SCR_InventoryStorageManagerComponent specifically,
		// which is what characters and arsenal-capable entities carry. If an entity only has the
		// base manager, we cannot safely remove items here and bail.
		SCR_InventoryStorageManagerComponent manager = SCR_InventoryStorageManagerComponent.Cast(entity.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
		{
			GRAD_Log.Debug(string.Format("ClearStorages: %1 has no SCR_InventoryStorageManagerComponent", GetEntityShortName(entity)));
			return 0;
		}

		array<IEntity> items = {};
		CollectAllItems(entity, items);

		int removed = 0;

		// Remove leaf-first: iterating the flattened list in reverse removes nested items before
		// their containers, which avoids invalidating a container we are about to touch.
		for (int i = items.Count() - 1; i >= 0; i--)
		{
			IEntity item = items[i];
			if (!item)
				continue;

			InventoryItemComponent itemComp = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
			if (!itemComp)
				continue;

			if (!force && itemComp.IsLocked())
			{
				GRAD_Log.Debug(string.Format("ClearStorages: skipping locked item %1", GetEntityShortName(item)));
				continue;
			}

			if (manager.TryRemoveItemFromInventory(item))
			{
				SCR_EntityHelper.DeleteEntityAndChildren(item);
				removed++;
			}
			else
			{
				// DIAGNOSTIC (2026-07-14): bumped from Debug to Warn — Vest_ALICE_suspenders_1/
				// Scabbard_Bayonet_M9 (attachment-style items on a vest's cloth node) consistently fail
				// to re-place later in Apply() with "no suitable storage," even under force=true clears.
				// If TryRemoveItemFromInventory is ALSO silently failing for these same items (i.e. they
				// survive the clear and collide with their own captured re-application later), that would
				// fully explain the symptom — this was invisible at Debug level.
				GRAD_Log.Warn(string.Format("ClearStorages: could not remove %1 (force=%2)", GetEntityShortName(item), force));
			}
		}

		return removed;
	}

	//------------------------------------------------------------------------------------------------
	// Local (non-replicated) spawning
	//------------------------------------------------------------------------------------------------

	//! Spawn a local-only (non-replicated) instance of a prefab, for the preview character. The
	//! resulting entity exists on this machine only and is never sent over the network, so it is
	//! safe to spawn, mutate, and delete freely on the client while the arsenal is open.
	//!
	//! Returns null if the prefab cannot be loaded (missing/unloaded content) — callers must treat
	//! a missing prefab as a skip, not a fatal error.
	//!
	//! `world` (2026-07-15): optional target BaseWorld. Defaults to null = the live game world, which
	//! is the historical behaviour every existing caller relies on. The arsenal menu's character
	//! preview now passes its own ISOLATED preview world here (BaseWorld.CreateWorld) so the preview
	//! clone is spawned into that world instead of the live one — see GRAD_ArsenalMenu.SetupPreview.
	//! Passed straight through to SpawnEntityPrefabLocal, whose verified signature already accepts the
	//! target world: `IEntity SpawnEntityPrefabLocal(notnull Resource templateResource,
	//! BaseWorld world = null, EntitySpawnParams params = null)`.
	static IEntity SpawnLocal(ResourceName prefab, vector position = vector.Zero, BaseWorld world = null)
	{
		if (prefab == ResourceName.Empty)
			return null;

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			GRAD_Log.Warn(string.Format("SpawnLocal: could not load prefab '%1'", prefab));
			return null;
		}

		// No explicit world requested -> fall back to the live game world (historical behaviour).
		if (!world)
			world = GetGame().GetWorld();

		if (!world)
		{
			GRAD_Log.Error("SpawnLocal: no world available");
			return null;
		}

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = position;

		IEntity entity = GetGame().SpawnEntityPrefabLocal(resource, world, params);
		if (!entity)
			GRAD_Log.Warn(string.Format("SpawnLocal: spawn returned null for '%1'", prefab));

		return entity;
	}
}

//------------------------------------------------------------------------------------------------
//! Lightweight value object describing one slot found during enumeration. Holds enough to
//! re-address the slot (its storage, slot id) plus traversal depth for UI indentation.
//! Not a JsonApiStruct — this is an in-memory traversal result, not a persisted record.
class GRAD_SlotRef
{
	BaseInventoryStorageComponent m_Storage;
	InventoryStorageSlot m_Slot;
	int m_iSlotId;
	int m_iDepth;

	//------------------------------------------------------------------------------------------------
	void GRAD_SlotRef(BaseInventoryStorageComponent storage, InventoryStorageSlot slot, int slotId, int depth)
	{
		m_Storage = storage;
		m_Slot = slot;
		m_iSlotId = slotId;
		m_iDepth = depth;
	}

	//------------------------------------------------------------------------------------------------
	//! Item currently occupying this slot, or null if empty.
	IEntity GetContent()
	{
		if (!m_Storage)
			return null;

		return m_Storage.Get(m_iSlotId);
	}

	//------------------------------------------------------------------------------------------------
	bool IsEmpty()
	{
		return GetContent() == null;
	}
}

//------------------------------------------------------------------------------------------------
//! Arsenal-type bits (SCR_EArsenalItemType) whose items carry a cargo storage the player can pour
//! stackables into: backpack, jacket/uniform torso, vest & belt, trousers/legs, radio backpack.
//! Weapons and their attachment storages are deliberately absent. Mirrors the labels in
//! GRAD_ArsenalCategoryLabels.
class GRAD_ContainerTypes
{
	static const int MASK =
		  (1 << 7)    // BACKPACK
		| (1 << 11)   // TORSO (jacket/uniform)
		| (1 << 12)   // VEST_AND_WAIST
		| (1 << 13)   // LEGS (trousers)
		| (1 << 15);  // RADIO_BACKPACK
}

//------------------------------------------------------------------------------------------------
//! A worn cargo container the arsenal can target: the storage to insert into plus a friendly label
//! (the owning clothing item's short name) and the owner item entity. Produced by
//! GRAD_InventoryLib.CollectDestinationContainers; consumed by the menu's destination selector.
class GRAD_ContainerRef : Managed
{
	BaseInventoryStorageComponent m_Storage;
	string m_sLabel;
	IEntity m_Owner;

	//------------------------------------------------------------------------------------------------
	void GRAD_ContainerRef(BaseInventoryStorageComponent storage, string label, IEntity owner)
	{
		m_Storage = storage;
		m_sLabel = label;
		m_Owner = owner;
	}
}
