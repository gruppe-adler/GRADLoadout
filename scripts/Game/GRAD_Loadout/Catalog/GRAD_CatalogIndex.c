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
				m_aRecords.Insert(record);
		}

		if (m_aWorkQueue.IsEmpty())
		{
			m_bBuilding = false;
			m_bComplete = true;
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

		string displayName = entry.GetEntityName();
		SCR_UIInfo uiInfo = entry.GetEntityUiInfo();
		if (GRAD_CommonUtils.IsBlank(displayName) && uiInfo)
			displayName = uiInfo.GetName();

		if (GRAD_CommonUtils.IsBlank(displayName))
			displayName = SCR_StringHelper.FormatResourceNameToUserFriendly(prefab);

		return new GRAD_ArsenalItemRecord(prefab, displayName, arsenalType, uiInfo, factionKey);
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
