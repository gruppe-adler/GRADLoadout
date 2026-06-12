//------------------------------------------------------------------------------------------------
//! P1 acceptance harness for the Utils module.
//!
//! Not shipped behaviour — a developer probe. Attach this component to a GenericEntity placed in
//! a test world, or call GRAD_UtilsTest.RunOn(character) from the Workbench script console against
//! a spawned vanilla rifleman. It prints slot enumeration, the storage hierarchy, and prefab
//! counts so the Utils helpers can be eyeballed for sanity.
//!
//! Expected output on a vanilla US rifleman: a handful of editable slots (uniform, vest, helmet,
//! backpack, weapon slots), with cosmetic/locked clothing nodes excluded by default.
class GRAD_UtilsTest
{
	//------------------------------------------------------------------------------------------------
	//! Run the full probe against a character entity and dump results to the log.
	static void RunOn(IEntity character)
	{
		GRAD_Log.SetMinLevel(GRAD_ELogLevel.DEBUG);

		if (!character)
		{
			GRAD_Log.Error("UtilsTest: character is null");
			return;
		}

		GRAD_Log.Info("==== GRAD_UtilsTest begin ====");
		GRAD_Log.Info(string.Format("Subject: %1", GRAD_InventoryLib.GetEntityShortName(character)));

		// 1) Top-level storages.
		array<BaseInventoryStorageComponent> storages = {};
		int storageCount = GRAD_InventoryLib.GetTopLevelStorages(character, storages);
		GRAD_Log.Info(string.Format("Top-level storages: %1", storageCount));
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			GRAD_Log.Info(string.Format("  storage %1 (slots=%2, priority=%3)",
				storage.Type().ToString(), storage.GetSlotsCount(), storage.GetPriority()));
		}

		// 2) Editable slots (locked excluded) vs. all slots (locked included).
		array<ref GRAD_SlotRef> editable = {};
		int editableCount = GRAD_InventoryLib.EnumerateSlots(character, editable, false);
		GRAD_Log.Info(string.Format("Editable slots (locked excluded): %1", editableCount));
		foreach (GRAD_SlotRef slotRef : editable)
		{
			string indent = GeneratePadding(slotRef.m_iDepth);
			string content = "<empty>";
			if (!slotRef.IsEmpty())
				content = GRAD_InventoryLib.GetEntityShortName(slotRef.GetContent());

			GRAD_Log.Info(string.Format("  %1%2 -> %3",
				indent, GRAD_InventoryLib.GetSlotDisplayName(slotRef.m_Slot), content));
		}

		array<ref GRAD_SlotRef> all = {};
		int allCount = GRAD_InventoryLib.EnumerateSlots(character, all, true);
		GRAD_Log.Info(string.Format("All slots (locked included): %1 (locked = %2)",
			allCount, allCount - editableCount));

		// 3) Character weapon slots.
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (chimera)
		{
			array<WeaponSlotComponent> weaponSlots = {};
			int weaponCount = GRAD_InventoryLib.GetCharacterWeaponSlots(chimera, weaponSlots);
			GRAD_Log.Info(string.Format("Weapon slots: %1", weaponCount));
			foreach (WeaponSlotComponent ws : weaponSlots)
			{
				IEntity wpn = ws.GetWeaponEntity();
				string wpnName = "<empty>";
				if (wpn)
					wpnName = GRAD_InventoryLib.GetEntityShortName(wpn);
				GRAD_Log.Info(string.Format("  slot[%1] type='%2' -> %3",
					ws.GetWeaponSlotIndex(), ws.GetWeaponSlotType(), wpnName));
			}
		}

		// 4) Flattened item list + prefab counts.
		array<IEntity> items = {};
		int itemCount = GRAD_InventoryLib.CollectAllItems(character, items);
		GRAD_Log.Info(string.Format("Total attached items: %1", itemCount));

		map<ResourceName, int> counts = new map<ResourceName, int>();
		GRAD_InventoryLib.CountPrefabInstances(items, counts);
		GRAD_Log.Info(string.Format("Distinct prefabs: %1", counts.Count()));
		foreach (ResourceName prefab, int count : counts)
		{
			GRAD_Log.Info(string.Format("  x%1  %2", count,
				SCR_StringHelper.FormatResourceNameToUserFriendly(prefab)));
		}

		// 5) Hierarchy path of the first item, as a spot-check of GetHierarchyPath.
		if (!items.IsEmpty())
			GRAD_Log.Info(string.Format("Path of first item: %1", GRAD_InventoryLib.GetHierarchyPath(items[0])));

		GRAD_Log.Info("==== GRAD_UtilsTest end ====");
	}

	//------------------------------------------------------------------------------------------------
	//! Two spaces per depth level, for readable indented slot dumps.
	protected static string GeneratePadding(int depth)
	{
		string s = "";
		for (int i = 0; i < depth; i++)
			s += "  ";
		return s;
	}
}
