//------------------------------------------------------------------------------------------------
//! Applies a GRAD_LoadoutData tree onto a target entity by clearing its editable storages and
//! re-spawning the recorded items into the recorded slots.
//!
//! Architecture note: equipped weapons live in an EquipedWeaponStorageComponent, which IS a
//! BaseInventoryStorageComponent. So weapons, clothing, and container contents all go through the
//! same storage path here — TrySpawnPrefabToStorage(prefab, storage, slotID, ...). For fixed-slot
//! storages (weapon slots) we address by the captured slot index rather than free insertion,
//! satisfying the "match slot counts" requirement.
//!
//! Two contexts, selected by the caller:
//!  - REPLICATED: run on the server/authority; spawned items replicate to clients.
//!  - LOCAL: run on the client for the preview mannequin; uses local-only spawning, nothing
//!    replicates.
//!
//! Resilience: a missing/unloaded prefab is skipped with a warning, never a hard failure. Every
//! created entity is collected into an out-array so the caller can clean up (e.g. on cancel).
class GRAD_LoadoutApply
{
	//------------------------------------------------------------------------------------------------
	//! Apply a loadout to a target entity.
	//!
	//! \param target        the entity to equip
	//! \param data          the loadout to apply
	//! \param localOnly     true = preview mannequin (local spawn); false = authoritative/replicated
	//! \param force         true = also clear locked slots before applying (server/GM)
	//! \param outCreated    receives every entity created, for later cleanup
	//! \return true if apply completed (even if some items were skipped), false on a fatal setup error
	//! \param clearFirst   true = strip the target's editable slots before applying (full-loadout
	//!                      restore / OK confirm); false = ADD the data on top of the current kit
	//!                      without clearing (single-item click in the arsenal). Defaults to true to
	//!                      preserve the full-loadout behavior.
	static bool Apply(IEntity target, GRAD_LoadoutData data, bool localOnly, bool force, out notnull array<IEntity> outCreated, bool clearFirst = true)
	{
		outCreated.Clear();

		if (!target)
		{
			GRAD_Log.Error("Apply: target is null");
			return false;
		}

		if (!data || !data.m_Root)
		{
			GRAD_Log.Error("Apply: loadout data is null/empty");
			return false;
		}

		InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(target.FindComponent(InventoryStorageManagerComponent));
		if (!manager)
		{
			GRAD_Log.Error(string.Format("Apply: %1 has no inventory manager", GRAD_InventoryLib.GetEntityShortName(target)));
			return false;
		}

		// Safety: a clearing apply with an empty loadout would strip the target and add nothing back,
		// leaving the unit naked. This happens if capture/serialization produced no entries (e.g. the
		// zero-arg-constructor serialization bug). Refuse rather than destroy the unit's kit.
		if (clearFirst && data.m_Root.GetChildCount() == 0)
		{
			GRAD_Log.Error(string.Format("Apply: refusing to clear %1 for an EMPTY loadout (would leave it naked)",
				GRAD_InventoryLib.GetEntityShortName(target)));
			return false;
		}

		// 1) Strip the target down to its editable (or all, if force) slots — unless this is an
		//    additive apply (a single-item click), which must leave the existing kit untouched.
		if (clearFirst)
		{
			int cleared = GRAD_InventoryLib.ClearStorages(target, force);
			GRAD_Log.Info(string.Format("Apply: cleared %1 items from %2", cleared, GRAD_InventoryLib.GetEntityShortName(target)));
		}

		// 2) Resolve the target's top-level storages so we can map captured storage classes to live
		//    storages. Children of the root entry are placed into these.
		array<BaseInventoryStorageComponent> topStorages = {};
		GRAD_InventoryLib.GetTopLevelStorages(target, topStorages);

		// 3) Walk the tree. Root children are top-level equipment; deeper entries recurse into the
		//    storages of the items we spawn.
		int spawned = 0;
		int skipped = 0;
		foreach (GRAD_LoadoutEntry child : data.m_Root.m_aChildren)
			ApplyEntry(manager, child, topStorages, localOnly, outCreated, spawned, skipped, 0);

		GRAD_Log.Info(string.Format("Apply: spawned %1 items, skipped %2 (localOnly=%3)", spawned, skipped, localOnly));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn one entry into the best-matching storage among candidateStorages, then recurse into the
	//! spawned item's own storages for the entry's children.
	protected static void ApplyEntry(
		notnull InventoryStorageManagerComponent manager,
		GRAD_LoadoutEntry entry,
		notnull array<BaseInventoryStorageComponent> candidateStorages,
		bool localOnly,
		notnull array<IEntity> outCreated,
		inout int spawned,
		inout int skipped,
		int depth)
	{
		if (!entry || depth > 16)
			return;

		if (GRAD_CommonUtils.IsBlank(entry.m_sPrefab))
		{
			skipped++;
			return;
		}

		// Pick the live storage that matches the captured storage class. May be null: a top-level
		// item added by clicking (no captured slot address) has a blank class and no match, in which
		// case we let the engine choose the most suitable storage rather than forcing it into the
		// first candidate (which is the identity storage and rejects everything).
		BaseInventoryStorageComponent storage = PickStorage(candidateStorages, entry.m_sStorageClass);

		// `created` may be null even on success: engine-chosen placement spawns the item but does
		// not hand it back, so we can't always locate it. `spawnedOk` is the authoritative result.
		bool spawnedOk;
		IEntity created = SpawnInto(manager, storage, entry, localOnly, spawnedOk);
		if (!spawnedOk)
		{
			// SpawnInto already logged the reason (missing prefab, no room, etc.).
			skipped++;
			return;
		}

		if (created)
			outCreated.Insert(created);
		spawned++;

		// Recurse: the children of this entry go into THIS item's own storages. This requires the
		// spawned entity handle; for engine-placed items we don't have it, so children are skipped.
		// (This only affects deep captured trees, not click-to-add which has no children.)
		if (created && entry.GetChildCount() > 0)
		{
			array<BaseInventoryStorageComponent> childStorages = {};
			BaseInventoryStorageComponent childStorage = BaseInventoryStorageComponent.Cast(created.FindComponent(BaseInventoryStorageComponent));
			if (childStorage)
				childStorages.Insert(childStorage);

			if (!childStorages.IsEmpty())
			{
				foreach (GRAD_LoadoutEntry grandChild : entry.m_aChildren)
					ApplyEntry(manager, grandChild, childStorages, localOnly, outCreated, spawned, skipped, depth + 1);
			}
			else
			{
				GRAD_Log.Debug(string.Format("Apply: '%1' has %2 children but no storage to hold them",
					entry.m_sPrefab, entry.GetChildCount()));
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn entry.m_sPrefab into the target inventory. When `storage` is non-null (a captured
	//! slot address that matched a live storage) the item is placed there at the captured slot;
	//! when it is null (e.g. a top-level item added by clicking, with no captured address) the
	//! engine chooses the most suitable storage — `TrySpawnPrefabToStorage(prefab, null, ...)` /
	//! `TryInsertItem(item, PURPOSE_ANY)`. This is what routes a vest to the vest slot, a magazine
	//! to a pouch, etc., instead of forcing everything into the first (identity) storage.
	//!
	//! `spawnedOk` is set true whenever the item was actually placed — even when we cannot return
	//! the entity handle (engine-chosen replicated placement does not return it). The returned
	//! entity is non-null only when we know its exact storage+slot (used for child recursion).
	protected static IEntity SpawnInto(
		notnull InventoryStorageManagerComponent manager,
		BaseInventoryStorageComponent storage,
		notnull GRAD_LoadoutEntry entry,
		bool localOnly,
		out bool spawnedOk)
	{
		spawnedOk = false;
		ResourceName prefab = entry.m_sPrefab;

		if (localOnly)
		{
			// Local preview path: spawn a non-replicated entity and insert it.
			IEntity item = GRAD_InventoryLib.SpawnLocal(prefab);
			if (!item)
				return null;

			bool inserted;
			if (storage)
			{
				// Captured slot address: place at the recorded slot, else any free slot in it.
				inserted = manager.TryInsertItemInStorage(item, storage, entry.m_iSlotIndex);
				if (!inserted)
					inserted = manager.TryInsertItemInStorage(item, storage, -1);
			}

			// No matched storage (or targeted placement failed): equip it. Clothing/gear (headgear,
			// vest, uniform, backpack) lives in LOCKED loadout slots that plain insertion refuses;
			// EquipAny routes the item to the correct loadout slot, replacing what's there. Falls
			// back to free insertion (PURPOSE_ANY) for items that belong in a container (magazines,
			// grenades, meds).
			if (!inserted)
				inserted = TryEquipOrInsert(manager, item);

			if (!inserted)
			{
				GRAD_Log.Warn(string.Format("Apply(local): could not place '%1' (no suitable slot/storage)", prefab));
				SCR_EntityHelper.DeleteEntityAndChildren(item);
				return null;
			}

			spawnedOk = true;
			return item;
		}

		// Replicated path: the manager spawns and inserts in one authoritative step. A null storage
		// tells the engine to choose the most suitable owned storage.
		bool ok;
		int slotID = -1;
		if (storage)
		{
			slotID = entry.m_iSlotIndex;
			ok = manager.TrySpawnPrefabToStorage(prefab, storage, slotID);
			if (!ok)
				ok = manager.TrySpawnPrefabToStorage(prefab, storage, -1);
		}

		// No matched storage (or targeted spawn failed): engine-chosen placement.
		if (!ok)
		{
			ok = manager.TrySpawnPrefabToStorage(prefab, null, -1, EStoragePurpose.PURPOSE_ANY);
			storage = null; // read-back below must reflect the engine-chosen storage, which we don't know
		}

		if (!ok)
		{
			GRAD_Log.Warn(string.Format("Apply: TrySpawnPrefabToStorage failed for '%1' (no suitable storage)", prefab));
			return null;
		}

		spawnedOk = true;

		// TrySpawnPrefabToStorage does not return the entity. We can only read it back when we know
		// the exact storage+slot (the targeted path). For engine-chosen placement we cannot reliably
		// locate it, so return null for the created-entity handle while still counting it as spawned.
		if (storage)
			return storage.Get(slotID);

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Place an already-spawned item onto the character using the type-aware equip path, falling
	//! back to free container insertion. EquipAny resolves the correct loadout slot for clothing/gear
	//! (headgear, vest, uniform, backpack) — slots that are LOCKED and which plain TryInsertItem
	//! refuses. Items that belong in a container (magazines, grenades, medical) are not clothing, so
	//! EquipAny returns false for them and we fall through to TryInsertItem(PURPOSE_ANY).
	//!
	//! Requires the SCR_ subclass of the manager (where EquipAny / GetCharacterStorage live); if the
	//! entity only has the base manager we can only do free insertion.
	protected static bool TryEquipOrInsert(notnull InventoryStorageManagerComponent manager, notnull IEntity item)
	{
		SCR_InventoryStorageManagerComponent scrManager = SCR_InventoryStorageManagerComponent.Cast(manager);
		if (scrManager)
		{
			// Weapons go through the weapon-equip path so they take the weapon slot (holstering the
			// previous weapon) instead of EquipAny filling every weapon slot until full. Detect a
			// weapon by its WeaponComponent on the spawned entity.
			if (item.FindComponent(WeaponComponent))
			{
				if (scrManager.EquipWeapon(item))
					return true;
			}

			SCR_CharacterInventoryStorageComponent charStorage = scrManager.GetCharacterStorage();
			if (charStorage && scrManager.EquipAny(charStorage, item, -1))
				return true;
		}

		// Not clothing/gear/weapon (or no character storage): free insertion into any suitable storage.
		return manager.TryInsertItem(item, EStoragePurpose.PURPOSE_ANY);
	}

	//------------------------------------------------------------------------------------------------
	//! Choose the storage whose class name matches the captured class. Returns null when there is
	//! no match (including a blank wanted class) — the caller treats null as "let the engine choose
	//! the most suitable storage" rather than forcing the item into candidates[0] (the identity
	//! storage), which rejects nearly everything.
	protected static BaseInventoryStorageComponent PickStorage(notnull array<BaseInventoryStorageComponent> candidates, string wantedClass)
	{
		if (candidates.IsEmpty() || GRAD_CommonUtils.IsBlank(wantedClass))
			return null;

		foreach (BaseInventoryStorageComponent storage : candidates)
		{
			if (storage && storage.Type().ToString() == wantedClass)
				return storage;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Delete every entity in a created-items list (used to discard a preview on cancel). Safe to
	//! call with locally-spawned or replicated entities; deletes leaf-first via the engine helper.
	static void CleanupCreated(notnull array<IEntity> created)
	{
		for (int i = created.Count() - 1; i >= 0; i--)
		{
			IEntity e = created[i];
			if (e)
				SCR_EntityHelper.DeleteEntityAndChildren(e);
		}
		created.Clear();
	}
}
