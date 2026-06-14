//------------------------------------------------------------------------------------------------
//! Builds a GRAD_LoadoutData tree from a live character/entity by walking its inventory.
//!
//! Two trust levels:
//!  - FULL: include every slot (server/GM context). Captures everything the entity carries.
//!  - FILTERED: skip slots locked by LoadoutSlotInfo (player context). Cosmetic/fixed clothing
//!    nodes are not recorded, so applying the result will not attempt to touch them.
//!
//! The walk descends storages recursively; a container item (vest, rifle) becomes an entry whose
//! children are the items inside it. The synthetic root entry has no prefab; its children are the
//! character's top-level equipment.
class GRAD_LoadoutCapture
{
	//------------------------------------------------------------------------------------------------
	//! Capture a loadout from an entity. Returns a populated GRAD_LoadoutData, or null if the entity
	//! has no usable inventory.
	//!
	//! \param entity      the source character/entity
	//! \param name        user-facing name to stamp on the loadout
	//! \param fullCapture true = include locked slots (server/GM); false = skip locked (player)
	static GRAD_LoadoutData Capture(IEntity entity, string name, bool fullCapture)
	{
		if (!entity)
		{
			GRAD_Log.Error("Capture: entity is null");
			return null;
		}

		array<BaseInventoryStorageComponent> roots = {};
		if (GRAD_InventoryLib.GetTopLevelStorages(entity, roots) == 0)
		{
			GRAD_Log.Warn(string.Format("Capture: %1 has no top-level storages", GRAD_InventoryLib.GetEntityShortName(entity)));
			return null;
		}

		GRAD_LoadoutData data = new GRAD_LoadoutData();
		data.m_sName = name;
		data.m_iSchemaVersion = GRAD_LoadoutData.CURRENT_SCHEMA_VERSION;

		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (character)
			data.m_sFactionKey = GetFactionKey(character);

		// Walk each top-level storage and append the items it holds to the root entry.
		foreach (BaseInventoryStorageComponent storage : roots)
			CaptureStorage(storage, data.m_Root, fullCapture, 0);

		GRAD_Log.Info(string.Format("Capture: '%1' -> %2 nodes (full=%3)",
			name, data.m_Root.CountSubtree() - 1, fullCapture));

		return data;
	}

	//------------------------------------------------------------------------------------------------
	//! Record every occupied slot of a storage as a child of parentEntry, recursing into items that
	//! are themselves storages.
	protected static void CaptureStorage(BaseInventoryStorageComponent storage, notnull GRAD_LoadoutEntry parentEntry, bool fullCapture, int depth)
	{
		if (!storage || depth > 16)
			return;

		string storageClass = storage.Type().ToString();
		int slotCount = storage.GetSlotsCount();

		for (int slotId = 0; slotId < slotCount; slotId++)
		{
			InventoryStorageSlot slot = storage.GetSlot(slotId);
			if (!slot)
				continue;

			// In filtered mode, skip fixed/cosmetic locked nodes entirely.
			if (!fullCapture && slot.IsLocked())
				continue;

			IEntity item = storage.Get(slotId);
			if (!item)
				continue;

			if (!GRAD_InventoryLib.IsVisibleInInventory(item))
				continue;

			ResourceName prefab = GRAD_InventoryLib.GetPrefabResourceName(item);
			if (prefab == ResourceName.Empty)
			{
				GRAD_Log.Debug(string.Format("Capture: skipping prefab-less item in %1#%2", storageClass, slotId));
				continue;
			}

			GRAD_LoadoutEntry entry = GRAD_LoadoutEntry.Create(prefab, slotId, storageClass, 1);
			parentEntry.AddChild(entry);

			// Recurse into this item's own storages (e.g. magazines inside a vest pouch, optic on
			// a rifle). Each nested storage's contents become children of this entry.
			array<BaseInventoryStorageComponent> childStorages = {};
			GetItemStorages(item, childStorages);
			foreach (BaseInventoryStorageComponent childStorage : childStorages)
				CaptureStorage(childStorage, entry, fullCapture, depth + 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Storage component an item itself exposes (e.g. a vest's pouch storage, a rifle's attachment
	//! storage). Uses the verified singular FindComponent, which returns the item's primary storage
	//! — sufficient for the common single-storage case.
	//!
	//! VERIFY IN WORKBENCH (P2): a few items expose more than one storage component (some weapons).
	//! If the live API confirms entity-level FindComponents(TypeName, out array<...>) exists, switch
	//! to it here to capture every storage; until then we capture the primary one.
	protected static int GetItemStorages(IEntity item, out notnull array<BaseInventoryStorageComponent> outStorages)
	{
		outStorages.Clear();
		if (!item)
			return 0;

		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(item.FindComponent(BaseInventoryStorageComponent));
		if (storage)
			outStorages.Insert(storage);

		return outStorages.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Best-effort faction key for grouping. Returns empty string if unavailable.
	protected static string GetFactionKey(ChimeraCharacter character)
	{
		SCR_ChimeraCharacter scrChar = SCR_ChimeraCharacter.Cast(character);
		if (!scrChar)
			return string.Empty;

		return scrChar.GetFactionKey();
	}
}
