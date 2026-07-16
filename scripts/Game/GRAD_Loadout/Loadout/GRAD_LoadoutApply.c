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
//!  - LOCAL: run on the client for the preview mannequin; nothing replicates (the target itself is a
//!    non-replicated SpawnEntityPrefabLocal clone).
//!
//! BUG FIX (2026-07-14, live-diagnosed): the LOCAL path used to pre-spawn a bare entity via
//! GRAD_InventoryLib.SpawnLocal, then hand it cold into TryInsertItemInStorage/TryInsertItem/EquipAny.
//! Every one of those calls returned `true` while doing nothing — a live worldPos diagnostic showed
//! "placed" items still sitting at their raw spawn position <0,0,0>, never attached, leaving the
//! preview character visibly naked. Root cause unconfirmed at the source level (InventoryStorage
//! ManagerComponent is a native `proto external` class with no script source to read), but the fix,
//! confirmed by matching the REPLICATED branch's own working strategy, is to use
//! TrySpawnPrefabToStorage for BOTH paths — it spawns and inserts as one atomic manager-owned
//! operation, instead of us inserting an already-existing entity separately.
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
	//! \param preferredStorage  when non-null, TOP-LEVEL entries are inserted into this specific
	//!                      storage first (the arsenal's "choose destination container" for stackable
	//!                      items — a mag into the chosen vest/backpack). Falls back to the normal
	//!                      equip/insert path when the item can't fit there. Null = today's behavior
	//!                      (engine chooses the storage). Only honored on the LOCAL preview path; the
	//!                      replicated path is unaffected.
	static bool Apply(IEntity target, GRAD_LoadoutData data, bool localOnly, bool force, out notnull array<IEntity> outCreated, bool clearFirst = true, BaseInventoryStorageComponent preferredStorage = null)
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
			ApplyEntry(manager, child, topStorages, localOnly, outCreated, spawned, skipped, 0, preferredStorage);

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
		int depth,
		BaseInventoryStorageComponent preferredStorage = null)
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

		// A caller-chosen destination container (arsenal "put this in the vest/backpack") applies only
		// to TOP-LEVEL entries — nested children still go into their own parent's storage. It takes
		// precedence over the class-matched storage for placement.
		BaseInventoryStorageComponent preferred = null;
		if (depth == 0)
			preferred = preferredStorage;

		// `created` may be null even on success: engine-chosen placement spawns the item but does
		// not hand it back, so we can't always locate it. `spawnedOk` is the authoritative result.
		bool spawnedOk;
		IEntity created = SpawnInto(manager, storage, entry, localOnly, spawnedOk, preferred);
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
		out bool spawnedOk,
		BaseInventoryStorageComponent preferredStorage = null)
	{
		spawnedOk = false;
		ResourceName prefab = entry.m_sPrefab;

		if (localOnly)
		{
			// ROOT CAUSE FOUND (2026-07-14, live-diagnosed): the previous approach here — SpawnLocal a
			// bare entity, then hand it cold into TryInsertItemInStorage/TryInsertItem/EquipAny — returned
			// `true` from every one of those calls while doing NOTHING: the live worldPos diagnostic
			// showed every "placed" item still sitting at <0,0,0> (its raw spawn position), never
			// attached, on a character that then rendered visibly naked. Those manager methods are native
			// `proto external` (no script source exists to confirm why), but the practical fact, tested
			// live, is that pre-spawning the entity ourselves and inserting it separately does not work
			// for a local/non-replicated entity.
			//
			// FIX: use TrySpawnPrefabToStorage — the SAME call the REPLICATED branch below already uses
			// successfully — which spawns AND inserts as one atomic manager-owned operation instead of us
			// handing it a cold pre-spawned entity. This mirrors the working replicated path as closely
			// as possible instead of maintaining a second, apparently-broken insertion strategy.
			string failTrail = "";
			bool ok = false;
			int slotID = -1;

			if (preferredStorage)
			{
				// BUG FIX (2026-07-15, live-diagnosed): double-clicking a baseline cosmetic accessory that
				// is ALREADY attached to the destination container (e.g. Vest_ALICE_suspenders_1/
				// Scabbard_Bayonet_M9/Canteen_US_01 already built into a worn vest) failed here with
				// "preferred[ClothNodeStorageComponent] ... equipReplace[equipAnyFailed]" — the vest has no
				// free slot for a SECOND copy of its own baked-in part, and EquipAny can't replace an item
				// with an identical one either. Same root cause and same fix as the "already correctly
				// equipped" short-circuit below for the captured-storage path: if the destination already
				// directly holds this exact prefab AND has no room for another, there is nothing to do —
				// not a failure.
				//
				// BUG FIX (2026-07-16, live-diagnosed): this guard originally fired on ANY prefab match in
				// the preferred storage, with no regard for free space — which silently broke the arsenal's
				// own [+] "add one more" button for every stackable already represented by at least one
				// instance (ammo/meds/grenades into a vest/backpack): `Apply: spawned 1 items` kept logging
				// (spawnedOk=true counts as spawned in the caller regardless of whether anything was
				// created), but no new entity was ever inserted, so the loadout panel's count never
				// increased. The ORIGINAL bug this guard fixed only exists when the destination has NO FREE
				// SLOT for a second copy (a single baked-in cosmetic node) — a stackable item's preferred
				// container almost always has room, so gate the short-circuit on that instead of a bare
				// prefab match: skip only when a match exists AND the storage is genuinely full.
				bool alreadyInPreferred = false;
				int preferredSlots = preferredStorage.GetSlotsCount();
				for (int pi = 0; pi < preferredSlots; pi++)
				{
					IEntity existingPreferred = preferredStorage.Get(pi);
					if (existingPreferred && GRAD_InventoryLib.GetPrefabResourceName(existingPreferred) == prefab)
					{
						alreadyInPreferred = true;
						break;
					}
				}

				if (alreadyInPreferred && !GRAD_InventoryLib.StorageHasFreeSlot(preferredStorage))
				{
					GRAD_Log.Debug(string.Format("Apply: '%1' already present in FULL preferred storage %2 (prefab-baseline match, no room for another) — skipping re-place", prefab, preferredStorage.Type().ToString()));
					spawnedOk = true;
					return null;
				}

				ok = manager.TrySpawnPrefabToStorage(prefab, preferredStorage, -1);
				if (!ok)
					failTrail += string.Format("preferred[%1] ", preferredStorage.Type().ToString());
			}

			if (!ok && storage)
			{
				slotID = entry.m_iSlotIndex;

				// BUG FIX (2026-07-14): Vest_ALICE_suspenders_1/Scabbard_Bayonet_M9 (and similar) always
				// failed here on a fresh clone whose PREFAB already spawns with baked-in default cosmetic
				// gear (force=false deliberately preserves those locked nodes — see ClearStorages' own
				// gotcha docs). If the captured item is the SAME prefab already occupying the target slot,
				// the clone is already correctly equipped — this is not a failure, just nothing to do.
				InventoryStorageSlot targetSlot = storage.GetSlot(slotID);
				if (targetSlot)
				{
					IEntity existing = targetSlot.GetAttachedEntity();
					if (existing && GRAD_InventoryLib.GetPrefabResourceName(existing) == prefab)
					{
						GRAD_Log.Debug(string.Format("Apply: '%1' already occupies its target slot %2 (prefab-baseline match) — skipping re-place", prefab, slotID));
						spawnedOk = true;
						return existing;
					}
				}

				ok = manager.TrySpawnPrefabToStorage(prefab, storage, slotID);
				if (!ok)
				{
					slotID = -1;
					ok = manager.TrySpawnPrefabToStorage(prefab, storage, -1);
				}
				if (!ok)
					failTrail += string.Format("captured[%1#%2] ", storage.Type().ToString(), entry.m_iSlotIndex);
			}

			// BUG FIX (2026-07-15, live-diagnosed): headgear could only be swapped when the uniform was
			// UNworn. Root cause: when there was no matched captured storage (`storage` null — exactly
			// the plain EQUIP-click case with no destination chosen), the code used to try the generic
			// PURPOSE_ANY insert BEFORE EquipAny. PURPOSE_ANY happily stuffs the item into ANY free slot
			// ANYWHERE (a pocket, an ammo pouch — not necessarily the item's actual loadout slot) and
			// reports success, so EquipAny's REPLACE-worn-item behavior was never reached — the item
			// landed in generic inventory space instead of the headgear slot, live-confirmed via "it adds
			// the item to the inventory rather than equipping it to headgear slot." It only ever appeared
			// to "work" once the uniform was removed because that happened to change which slots the
			// engine considered free/chosen — never the actual mechanism.
			//
			// FIX: when there is no matched storage, try TryEquipReplace FIRST (it resolves the item's
			// real loadout slot and REPLACES whatever currently occupies it — real vanilla behavior,
			// verified this project's own memory/gotchas), and only fall back to the generic PURPOSE_ANY
			// insert if EquipAny itself fails (e.g. for an item type with no dedicated equip slot at all).
			BaseInventoryStorageComponent landedStorage = storage;
			if (!ok && !storage)
			{
				IEntity spawnedItem = TryEquipReplace(manager, prefab, true, failTrail);
				if (spawnedItem)
				{
					spawnedOk = true;
					return spawnedItem;
				}
			}

			// No matched storage (or targeted spawn failed) and EquipAny didn't take it either: let the
			// engine choose the most suitable owned storage, same fallback the replicated branch uses.
			if (!ok)
			{
				ok = manager.TrySpawnPrefabToStorage(prefab, null, -1, EStoragePurpose.PURPOSE_ANY);
				landedStorage = null; // engine-chosen; we don't know which one without reading back
				slotID = -1;
				if (!ok)
					failTrail += "engineChosen ";
			}

			// Last resort for the case a captured `storage` WAS matched but its targeted spawn failed
			// (e.g. the slot is occupied by a different prefab) — retry via EquipAny here too, since the
			// branch above only covers the no-matched-storage case.
			if (!ok)
			{
				IEntity spawnedItem = TryEquipReplace(manager, prefab, true, failTrail);
				if (spawnedItem)
				{
					spawnedOk = true;
					return spawnedItem;
				}
			}

			if (!ok)
			{
				GRAD_Log.Warn(string.Format("Apply(local): could not place '%1' — failed steps: %2", prefab, failTrail));
				return null;
			}

			spawnedOk = true;

			// Only read back the entity when we know its exact storage+slot (matches the replicated
			// branch's own comment/behavior below) — engine-chosen placement can't be reliably located.
			if (landedStorage)
				return landedStorage.Get(slotID);

			return null;
		}

		// Replicated path: the manager spawns and inserts in one authoritative step. A null storage
		// tells the engine to choose the most suitable owned storage.
		//
		// BUG FIX (2026-07-15): `ok` used to be declared but left UNASSIGNED when `storage` was null (the
		// `if (storage)` block below was skipped entirely) — reading an uninitialized bool at the `if
		// (!ok)` engine-chosen fallback further down was undefined behavior, not a deliberate "no matched
		// storage" branch. Explicitly false here so "no captured storage" reliably falls through to the
		// EquipAny/PURPOSE_ANY fallbacks below instead of depending on whatever garbage `ok` happened to
		// hold.
		bool ok = false;
		int slotID = -1;
		if (storage)
		{
			slotID = entry.m_iSlotIndex;

			// Same "already correctly equipped" short-circuit as the local branch above — if
			// ClearStorages failed to remove this exact prefab from this exact slot (e.g. an
			// attachment-style item its own removal call refuses), don't treat the resulting collision
			// as a placement failure.
			InventoryStorageSlot targetSlot = storage.GetSlot(slotID);
			if (targetSlot)
			{
				IEntity existing = targetSlot.GetAttachedEntity();
				if (existing && GRAD_InventoryLib.GetPrefabResourceName(existing) == prefab)
				{
					GRAD_Log.Debug(string.Format("Apply: '%1' already occupies its target slot %2 (prefab-baseline match) — skipping re-place", prefab, slotID));
					spawnedOk = true;
					return existing;
				}
			}

			ok = manager.TrySpawnPrefabToStorage(prefab, storage, slotID);
			if (!ok)
				ok = manager.TrySpawnPrefabToStorage(prefab, storage, -1);
		}

		// BUG FIX (2026-07-15, live-diagnosed — see the local branch's matching comment above for the
		// full story): when there is NO matched storage (a plain EQUIP click with no captured slot
		// address), the generic PURPOSE_ANY insert used to run BEFORE EquipAny. PURPOSE_ANY happily
		// stuffs the item into any free slot anywhere and reports success, so EquipAny's REPLACE-worn-item
		// behavior was never reached — live-confirmed as "headgear only swappable when uniform removed"
		// (the item was landing in generic inventory space, not the headgear slot). FIX: try EquipAny
		// FIRST whenever there's no matched storage, falling back to PURPOSE_ANY only if EquipAny itself
		// can't place it.
		string failTrailReplicated = "";
		if (!ok && !storage)
		{
			IEntity spawnedItem = TryEquipReplace(manager, prefab, false, failTrailReplicated);
			if (spawnedItem)
			{
				spawnedOk = true;
				return spawnedItem;
			}
		}

		// No matched storage (or targeted spawn failed) and EquipAny didn't take it either: engine-chosen
		// placement.
		if (!ok)
		{
			ok = manager.TrySpawnPrefabToStorage(prefab, null, -1, EStoragePurpose.PURPOSE_ANY);
			storage = null; // read-back below must reflect the engine-chosen storage, which we don't know
		}

		// Last resort for the case a captured `storage` WAS matched but its targeted spawn failed (e.g.
		// the slot is occupied by a different prefab) — retry via EquipAny here too, since the branch
		// above only covers the no-matched-storage case.
		if (!ok)
		{
			IEntity spawnedItem = TryEquipReplace(manager, prefab, false, failTrailReplicated);
			if (spawnedItem)
			{
				spawnedOk = true;
				return spawnedItem;
			}
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
	//! Last-resort placement for clothing/gear that is already WORN — a loadout slot occupied by a
	//! DIFFERENT item (e.g. swapping the base BDU uniform for a pilot suit, or an already-worn backpack
	//! for a different one). TrySpawnPrefabToStorage only INSERTS into a free slot; it never replaces
	//! an occupied one, which is why "double-click to equip a uniform/headgear/backpack that's already
	//! worn" failed 100% of the time in the 2026-07-14 live test (`engineChosen` in the failTrail).
	//!
	//! ATTEMPT 1 (removed): spawn into WHATEVER free slot on WHATEVER storage would accept it, then
	//! EquipAny to relocate. Live-diagnosed as broken FOR A DIFFERENT REASON than the original bug:
	//! most storages on a character (weapon slots, ammo pouches) are type-restricted and reject an
	//! arbitrary clothing prefab — "found 17 empty slots, none accepted this prefab" was a genuine
	//! engine rejection (wrong item type for those slots), not a script bug. There is no generic
	//! "holds anything" storage to use as a waypoint.
	//!
	//! CURRENT FIX: EquipAny's own real body (verified this session from arexplorer) explicitly
	//! handles an item with NO parent slot at all: `if (!sourceSlot || !sourceSlot.GetStorage()) { ...
	//! TryReplaceItem / TryInsertItemInStorage ... }`. So the item does not need a holding slot BEFORE
	//! EquipAny — it can be a bare, freshly-spawned, un-inserted entity. The earlier "SpawnLocal then
	//! hand to EquipAny cold" bug was specific to `GRAD_InventoryLib.SpawnLocal`
	//! (`SpawnEntityPrefabLocal`) on the LOCAL/preview path never registering with the manager. Here we
	//! use the manager-appropriate spawn for each context: `Game.SpawnEntityPrefab` (replicated,
	//! authoritative) for the server/real-target path, `SpawnEntityPrefabLocal` for the local preview
	//! path — and hand the result straight to EquipAny, since EquipAny's OWN documented handling
	//! covers this "no parent slot yet" case rather than us working around it with a second insert
	//! step.
	protected static IEntity TryEquipReplace(notnull InventoryStorageManagerComponent manager, ResourceName prefab, bool localOnly, out string failTrail)
	{
		SCR_InventoryStorageManagerComponent scrManager = SCR_InventoryStorageManagerComponent.Cast(manager);
		if (!scrManager)
		{
			failTrail += "noScrManager ";
			return null;
		}

		SCR_CharacterInventoryStorageComponent charStorage = scrManager.GetCharacterStorage();
		if (!charStorage)
		{
			failTrail += "noCharStorage ";
			return null;
		}

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			failTrail += "equipReplace[prefabLoadFailed] ";
			return null;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			failTrail += "equipReplace[noWorld] ";
			return null;
		}

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);

		IEntity spawned = null;
		if (localOnly)
			spawned = GetGame().SpawnEntityPrefabLocal(resource, world, params);
		else
			spawned = GetGame().SpawnEntityPrefab(resource, world, params);

		if (!spawned)
		{
			failTrail += "equipReplace[spawnFailed] ";
			return null;
		}

		if (scrManager.EquipAny(charStorage, spawned, -1))
		{
			// ROOT CAUSE FOUND (2026-07-16, live-diagnosed): EquipAny reports `true` even when the item
			// never actually lands on the character. Confirmed via arexplorer's real EquipAny body: when
			// the target slot already has an occupant, EquipAny takes its "performDropOfOriginalItem"
			// branch, which creates a DropAndMoveOperationCallback and returns the result of
			// TryRemoveItemFromStorage() on the OLD occupant immediately — the new item's own insertion
			// (TryMoveItemToStorage) only happens later, from that callback's OnDropComplete(). On the
			// local/non-replicated preview clone there is nothing driving that callback to completion, so
			// `spawned` is dropped into limbo: EquipAny already returned true, but the entity never gets
			// re-parented onto the character. Live-confirmed exactly this way for BDU Trousers re-equipped
			// right after a one-piece coverall (Suit_Pilot/Suit_Tanker, which also occupies the LEGS slot)
			// was removed from that same slot — GetParent() came back null and worldPos was still the
			// spawn-origin identity transform from Math3D.MatrixIdentity4 above.
			//
			// FIX: don't trust EquipAny's return value alone — read back real state (this project's own
			// hard rule; see the camera-position bug for the same lesson). If the entity isn't actually
			// parented onto the character after EquipAny claims success, treat it as a failure: delete the
			// orphan and let the caller fall through to its next fallback instead of silently losing the
			// item.
			IEntity spawnedParent = spawned.GetParent();
			vector spawnedTransform[4];
			spawned.GetWorldTransform(spawnedTransform);
			GRAD_Log.Info(string.Format("TryEquipReplaceDiag: prefab='%1' parent=%2 parentName=%3 worldPos=%4",
				prefab, spawnedParent != null, GRAD_InventoryLib.GetEntityShortName(spawnedParent), spawnedTransform[3].ToString()));

			if (spawnedParent)
				return spawned;

			failTrail += "equipReplace[equipAnyOrphaned] ";
			SCR_EntityHelper.DeleteEntityAndChildren(spawned);
			return null;
		}

		failTrail += "equipReplace[equipAnyFailed] ";
		SCR_EntityHelper.DeleteEntityAndChildren(spawned);
		return null;
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
