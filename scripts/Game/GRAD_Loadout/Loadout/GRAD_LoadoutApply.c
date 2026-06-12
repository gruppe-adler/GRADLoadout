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
	static bool Apply(IEntity target, GRAD_LoadoutData data, bool localOnly, bool force, out notnull array<IEntity> outCreated)
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

		// 1) Strip the target down to its editable (or all, if force) slots.
		int cleared = GRAD_InventoryLib.ClearStorages(target, force);
		GRAD_Log.Info(string.Format("Apply: cleared %1 items from %2", cleared, GRAD_InventoryLib.GetEntityShortName(target)));

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

		// Pick the live storage that matches the captured storage class; fall back to the first
		// candidate so a renamed/changed storage class still gets a reasonable target.
		BaseInventoryStorageComponent storage = PickStorage(candidateStorages, entry.m_sStorageClass);
		if (!storage)
		{
			GRAD_Log.Warn(string.Format("Apply: no storage for '%1' (class '%2') — skipped",
				entry.m_sPrefab, entry.m_sStorageClass));
			skipped++;
			return;
		}

		IEntity created = SpawnInto(manager, storage, entry, localOnly);
		if (!created)
		{
			// SpawnInto already logged the reason (missing prefab, no room, etc.).
			skipped++;
			return;
		}

		outCreated.Insert(created);
		spawned++;

		// Recurse: the children of this entry go into THIS item's own storages.
		if (entry.GetChildCount() > 0)
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
	//! Spawn entry.m_sPrefab into the given storage at the captured slot index. For replicated
	//! apply this uses the manager's TrySpawnPrefabToStorage; for local preview it spawns a
	//! local-only entity and inserts it.
	//!
	//! Returns the created entity, or null on failure (missing prefab / no room) — caller treats
	//! null as a skip.
	protected static IEntity SpawnInto(
		notnull InventoryStorageManagerComponent manager,
		notnull BaseInventoryStorageComponent storage,
		notnull GRAD_LoadoutEntry entry,
		bool localOnly)
	{
		ResourceName prefab = entry.m_sPrefab;

		if (localOnly)
		{
			// Local preview path: spawn a non-replicated entity and insert it into the storage.
			IEntity item = GRAD_InventoryLib.SpawnLocal(prefab);
			if (!item)
				return null;

			bool inserted = manager.TryInsertItemInStorage(item, storage, entry.m_iSlotIndex);
			if (!inserted)
			{
				// Could not place at the recorded slot; try any free slot in this storage.
				inserted = manager.TryInsertItemInStorage(item, storage, -1);
			}

			if (!inserted)
			{
				GRAD_Log.Warn(string.Format("Apply(local): could not insert '%1' into %2",
					prefab, storage.Type().ToString()));
				SCR_EntityHelper.DeleteEntityAndChildren(item);
				return null;
			}

			return item;
		}

		// Replicated path: the manager spawns and inserts in one authoritative step.
		bool ok = manager.TrySpawnPrefabToStorage(prefab, storage, entry.m_iSlotIndex);
		if (!ok)
		{
			// Retry with free insertion before giving up.
			ok = manager.TrySpawnPrefabToStorage(prefab, storage, -1);
		}

		if (!ok)
		{
			GRAD_Log.Warn(string.Format("Apply: TrySpawnPrefabToStorage failed for '%1' in %2",
				prefab, storage.Type().ToString()));
			return null;
		}

		// TrySpawnPrefabToStorage does not directly return the entity; read it back from the slot.
		IEntity created = storage.Get(entry.m_iSlotIndex);
		return created;
	}

	//------------------------------------------------------------------------------------------------
	//! Choose the storage whose class name matches the captured class; otherwise the first
	//! candidate. Returns null only if there are no candidates.
	protected static BaseInventoryStorageComponent PickStorage(notnull array<BaseInventoryStorageComponent> candidates, string wantedClass)
	{
		if (candidates.IsEmpty())
			return null;

		if (!GRAD_CommonUtils.IsBlank(wantedClass))
		{
			foreach (BaseInventoryStorageComponent storage : candidates)
			{
				if (storage && storage.Type().ToString() == wantedClass)
					return storage;
			}
		}

		return candidates[0];
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
