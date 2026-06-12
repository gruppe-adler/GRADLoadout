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
				GRAD_Log.Debug(string.Format("ClearStorages: could not remove %1", GetEntityShortName(item)));
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
	static IEntity SpawnLocal(ResourceName prefab, vector position = vector.Zero)
	{
		if (prefab == ResourceName.Empty)
			return null;

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			GRAD_Log.Warn(string.Format("SpawnLocal: could not load prefab '%1'", prefab));
			return null;
		}

		BaseWorld world = GetGame().GetWorld();
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
