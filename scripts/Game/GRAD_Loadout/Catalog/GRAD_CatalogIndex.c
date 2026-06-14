//------------------------------------------------------------------------------------------------
//! Builds and owns the flattened arsenal item index, amortized over multiple frames so loading a
//! full catalog never causes a hitch.
//!
//! Source of truth is the engine catalog (SCR_EntityCatalogManagerComponent) queried by
//! composition — no `modded class` (see docs/DECISIONS.md D2). We read the ITEM catalog (general
//! + per-faction), pull each entry's SCR_ArsenalItem data for its arsenal type, and produce
//! GRAD_ArsenalItemRecord objects the menu can sort/filter cheaply.
//!
//! Build model: BeginBuild() seeds a work queue of catalog entries; Tick() processes up to
//! m_iEntriesPerFrame of them per call (driven by the singleton service entity's frame event).
//! When the queue drains, OnComplete fires once.
class GRAD_CatalogIndex
{
	//! How many catalog entries to convert per frame. Tunable; keeps per-frame cost bounded.
	protected int m_iEntriesPerFrame;

	//! Completed records, ready for the UI.
	protected ref array<ref GRAD_ArsenalItemRecord> m_aRecords;

	//! prefab -> arsenal item type (the SCR_EArsenalItemType bit). Lets callers classify an already
	//! equipped item (which only knows its prefab) without re-querying the catalog. Built alongside
	//! m_aRecords. Used by the menu's [Empty]/quantity logic to find equipped items of a category.
	protected ref map<ResourceName, int> m_mPrefabType;

	//! Pending work: catalog entries still to be converted, paired with their faction key.
	protected ref array<ref GRAD_CatalogWorkItem> m_aWorkQueue;

	//! Build state.
	protected bool m_bBuilding;
	protected bool m_bComplete;

	//! Fires once when a build finishes.
	protected ref ScriptInvoker m_OnComplete;

	//------------------------------------------------------------------------------------------------
	void GRAD_CatalogIndex(int entriesPerFrame = 64)
	{
		m_iEntriesPerFrame = Math.Max(1, entriesPerFrame);
		m_aRecords = new array<ref GRAD_ArsenalItemRecord>();
		m_aWorkQueue = new array<ref GRAD_CatalogWorkItem>();
		m_mPrefabType = new map<ResourceName, int>();
		m_OnComplete = new ScriptInvoker();
	}

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnComplete()
	{
		return m_OnComplete;
	}

	//------------------------------------------------------------------------------------------------
	bool IsComplete()
	{
		return m_bComplete;
	}

	//------------------------------------------------------------------------------------------------
	bool IsBuilding()
	{
		return m_bBuilding;
	}

	//------------------------------------------------------------------------------------------------
	int GetRecordCount()
	{
		return m_aRecords.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! All records (the full index). Caller must not mutate.
	array<ref GRAD_ArsenalItemRecord> GetRecords()
	{
		return m_aRecords;
	}

	//------------------------------------------------------------------------------------------------
	//! Arsenal item type (SCR_EArsenalItemType bit) for a prefab, or 0 if the prefab is not a known
	//! arsenal item. Lets the menu classify an equipped item by its prefab.
	int GetArsenalTypeForPrefab(ResourceName prefab)
	{
		int type = 0;
		m_mPrefabType.Find(prefab, type);
		return type;
	}

	//------------------------------------------------------------------------------------------------
	//! Seed the work queue from the engine item catalogs. Idempotent: a second call while building
	//! is ignored. Returns the number of entries queued.
	int BeginBuild()
	{
		if (m_bBuilding)
		{
			GRAD_Log.Debug("CatalogIndex: BeginBuild ignored, already building");
			return m_aWorkQueue.Count();
		}

		m_aRecords.Clear();
		m_aWorkQueue.Clear();
		m_mPrefabType.Clear();
		m_bComplete = false;
		m_bBuilding = true;

		SCR_EntityCatalogManagerComponent catalogMgr = SCR_EntityCatalogManagerComponent.GetInstance();
		if (!catalogMgr)
		{
			GRAD_Log.Error("CatalogIndex: no SCR_EntityCatalogManagerComponent instance");
			m_bBuilding = false;
			return 0;
		}

		// General (factionless) item catalog.
		QueueCatalog(catalogMgr.GetEntityCatalogOfType(EEntityCatalogType.ITEM, false), string.Empty);

		// Per-faction item catalogs.
		QueueFactionCatalogs(catalogMgr);

		GRAD_Log.Info(string.Format("CatalogIndex: queued %1 entries for indexing", m_aWorkQueue.Count()));
		return m_aWorkQueue.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Queue every faction's ITEM catalog.
	protected void QueueFactionCatalogs(notnull SCR_EntityCatalogManagerComponent catalogMgr)
	{
		FactionManager factionMgr = GetGame().GetFactionManager();
		if (!factionMgr)
			return;

		array<Faction> factions = {};
		factionMgr.GetFactionsList(factions);

		foreach (Faction faction : factions)
		{
			if (!faction)
				continue;

			FactionKey factionKey = faction.GetFactionKey();
			SCR_EntityCatalog catalog = catalogMgr.GetFactionEntityCatalogOfType(EEntityCatalogType.ITEM, factionKey, false);
			QueueCatalog(catalog, factionKey);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Append a catalog's entries to the work queue, tagged with their faction key.
	protected void QueueCatalog(SCR_EntityCatalog catalog, string factionKey)
	{
		if (!catalog)
			return;

		array<SCR_EntityCatalogEntry> entries = {};
		catalog.GetEntityList(entries);

		foreach (SCR_EntityCatalogEntry entry : entries)
		{
			if (entry)
				m_aWorkQueue.Insert(new GRAD_CatalogWorkItem(entry, factionKey));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Process up to m_iEntriesPerFrame queued entries. Call once per frame while building. When the
	//! queue empties, marks complete and fires OnComplete. Safe to call when idle.
	void Tick()
	{
		if (!m_bBuilding)
			return;

		int processed = 0;
		while (processed < m_iEntriesPerFrame && !m_aWorkQueue.IsEmpty())
		{
			GRAD_CatalogWorkItem work = m_aWorkQueue[m_aWorkQueue.Count() - 1];
			m_aWorkQueue.Remove(m_aWorkQueue.Count() - 1);
			processed++;

			if (!work || !work.m_Entry)
				continue;

			GRAD_ArsenalItemRecord record = ConvertEntry(work.m_Entry, work.m_sFactionKey);
			if (record)
			{
				m_aRecords.Insert(record);
				m_mPrefabType.Set(record.m_sPrefab, record.m_iArsenalType);
			}
		}

		if (m_aWorkQueue.IsEmpty())
		{
			m_bBuilding = false;
			m_bComplete = true;
			DisambiguateNames();	// append variant suffixes to same-named records
			GRAD_Log.Info(string.Format("CatalogIndex: build complete, %1 records", m_aRecords.Count()));
			m_OnComplete.Invoke();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Convert one catalog entry into a record, or null if it is not an arsenal-eligible item.
	protected GRAD_ArsenalItemRecord ConvertEntry(notnull SCR_EntityCatalogEntry entry, string factionKey)
	{
		ResourceName prefab = entry.GetPrefab();
		if (prefab == ResourceName.Empty)
			return null;

		// Only items carrying SCR_ArsenalItem data are arsenal-eligible.
		SCR_ArsenalItem arsenalData = SCR_ArsenalItem.Cast(entry.GetEntityDataOfType(SCR_ArsenalItem));
		if (!arsenalData)
			return null;

		int arsenalType = arsenalData.GetItemType();

		// Skip non-personal-equipment categories (vehicles, helicopters, mortars): a character arsenal
		// only deals with things a soldier can carry/wear.
		if (!IsPersonalEquipmentType(arsenalType))
			return null;

		string displayName = entry.GetEntityName();
		SCR_UIInfo uiInfo = entry.GetEntityUiInfo();
		if (GRAD_CommonUtils.IsBlank(displayName) && uiInfo)
			displayName = uiInfo.GetName();

		// Resolve the localized name now so we can detect a missing stringtable key (which would
		// otherwise leak a raw "#AR-…_Name" into the UI, e.g. ExplosiveCharge_M112). If the key does
		// not resolve, fall back to a friendly name derived from the prefab.
		string resolved = WidgetManager.Translate(displayName);
		if (GRAD_CommonUtils.IsBlank(resolved) || resolved.IndexOf("#") == 0 || resolved.IndexOf("AR-") == 0)
			resolved = SCR_StringHelper.FormatResourceNameToUserFriendly(prefab);

		displayName = resolved;
		if (GRAD_CommonUtils.IsBlank(displayName))
			displayName = SCR_StringHelper.FormatResourceNameToUserFriendly(prefab);

		string variantSuffix = VariantSuffixFromPrefab(prefab);

		// DIAGNOSTIC (temporary, step 0): dump the native editable-entity labels for a sample so we
		// can pick a label that tracks caliber/family for grouping. Limited count to keep logs sane.
		GRAD_DebugDumpLabels(entry, displayName, prefab, arsenalType);

		return new GRAD_ArsenalItemRecord(prefab, displayName, arsenalType, uiInfo, factionKey, variantSuffix);
	}

	//! DIAGNOSTIC (temporary): how many label dumps we've emitted, to cap log spam.
	protected static int s_iLabelDumpCount = 0;

	//------------------------------------------------------------------------------------------------
	//! DIAGNOSTIC (temporary, step 0): log the native editable-entity labels for an entry so we can
	//! choose a grouping field. Caps total output. Logs the integer label values (the enum may not be
	//! script-reachable) alongside name/prefab/arsenalType.
	protected void GRAD_DebugDumpLabels(notnull SCR_EntityCatalogEntry entry, string name, ResourceName prefab, int arsenalType)
	{
		if (s_iLabelDumpCount >= 80)
			return;

		array<EEditableEntityLabel> labels = {};
		entry.GetEditableEntityLabels(labels);

		string labelStr = "";
		foreach (EEditableEntityLabel lbl : labels)
		{
			if (labelStr != "")
				labelStr += ",";
			labelStr += lbl.ToString();
		}

		string shortPrefab = SCR_StringHelper.FormatResourceNameToUserFriendly(prefab);
		GRAD_Log.Info(string.Format("LABELDUMP atype=%1 name='%2' prefab='%3' labels=[%4]",
			arsenalType, name, shortPrefab, labelStr));

		s_iLabelDumpCount++;
	}

	//------------------------------------------------------------------------------------------------
	//! Build a readable variant tag from a prefab's file stem. e.g.
	//!   "…/Rifle_M16A2_carbine_M203.et" -> "Rifle M16A2 carbine M203"
	//! The dedupe pass decides whether to actually show it (only when names collide) and trims the
	//! base-name words off the front, so this just returns the full friendly stem.
	protected static string VariantSuffixFromPrefab(ResourceName prefab)
	{
		string path = prefab;

		// Strip the "{GUID}" prefix if present.
		int brace = path.IndexOf("}");
		if (brace >= 0)
			path = path.Substring(brace + 1, path.Length() - (brace + 1));

		// Take the file stem (after the last '/' and before '.et').
		int slash = path.LastIndexOf("/");
		if (slash >= 0)
			path = path.Substring(slash + 1, path.Length() - (slash + 1));

		int dot = path.LastIndexOf(".");
		if (dot >= 0)
			path = path.Substring(0, dot);

		// Underscores -> spaces for readability.
		path.Replace("_", " ");
		return path;
	}

	//------------------------------------------------------------------------------------------------
	//! After the build completes, append a variant suffix to the display name of any record whose
	//! base (localized) name is shared by more than one record, so variants are distinguishable. The
	//! suffix is the part of the friendly prefab stem that differs from the base name.
	protected void DisambiguateNames()
	{
		// Count how many records share each base name.
		map<string, int> baseCounts = new map<string, int>();
		foreach (GRAD_ArsenalItemRecord r : m_aRecords)
		{
			if (!r)
				continue;
			int c = 0;
			baseCounts.Find(r.m_sBaseName, c);
			baseCounts.Set(r.m_sBaseName, c + 1);
		}

		foreach (GRAD_ArsenalItemRecord r : m_aRecords)
		{
			if (!r)
				continue;

			int shared = 0;
			baseCounts.Find(r.m_sBaseName, shared);
			if (shared <= 1)
				continue; // unique name: leave it as the plain localized base name

			string tag = TrimLeadingWords(r.m_sVariantSuffix, r.m_sBaseName);
			if (GRAD_CommonUtils.IsBlank(tag))
				tag = r.m_sVariantSuffix; // fall back to the full stem if nothing distinct remains

			if (!GRAD_CommonUtils.IsBlank(tag))
				r.m_sDisplayName = string.Format("%1 (%2)", r.m_sBaseName, tag);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Remove from the front of `full` any leading whitespace-separated words that also appear (in
	//! order) at the start of `lead`, case-insensitively. e.g. full="Rifle M16A2 carbine M203",
	//! lead="M16A2" -> "carbine M203" (the family word M16A2 is dropped; the generic "Rifle" prefix
	//! is also dropped if it isn't part of the base name). Returns the remaining tail trimmed.
	protected string TrimLeadingWords(string full, string lead)
	{
		array<string> fullWords = {};
		full.Split(" ", fullWords, true);

		array<string> leadWords = {};
		lead.Split(" ", leadWords, true);

		// Drop any leading full-word that matches ANY base-name word (covers "Rifle"/family tokens
		// regardless of order), stopping at the first word that is genuinely a variant.
		int start = 0;
		while (start < fullWords.Count())
		{
			string w = fullWords[start];
			w.ToLower();
			bool inBase = false;
			foreach (string lw : leadWords)
			{
				string lwLower = lw;
				lwLower.ToLower();
				if (w == lwLower)
				{
					inBase = true;
					break;
				}
			}
			if (!inBase)
				break;
			start++;
		}

		string result = "";
		for (int i = start; i < fullWords.Count(); i++)
		{
			if (i > start)
				result += " ";
			result += fullWords[i];
		}
		return result;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether an arsenal item type is personal soldier equipment (carried/worn) and so belongs in
	//! the character arsenal. Excludes vehicle-scale categories. Bit values are SCR_EArsenalItemType
	//! (RIFLE=1<<1 .. VEHICLE=1<<22); excluded: MORTARS=1<<20, HELICOPTER=1<<21, VEHICLE=1<<22.
	protected static bool IsPersonalEquipmentType(int arsenalType)
	{
		const int EXCLUDED =
			  (1 << 20)   // MORTARS
			| (1 << 21)   // HELICOPTER
			| (1 << 22);  // VEHICLE

		if ((arsenalType & EXCLUDED) != 0)
			return false;

		// An item with no recognized type bit is junk for a personal arsenal.
		return arsenalType != 0;
	}
}

//------------------------------------------------------------------------------------------------
//! A single unit of indexing work: one catalog entry plus the faction it came from.
class GRAD_CatalogWorkItem
{
	SCR_EntityCatalogEntry m_Entry;
	string m_sFactionKey;

	void GRAD_CatalogWorkItem(SCR_EntityCatalogEntry entry, string factionKey)
	{
		m_Entry = entry;
		m_sFactionKey = factionKey;
	}
}
