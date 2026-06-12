//------------------------------------------------------------------------------------------------
//! Prefab data class for the arsenal service entity. Holds designer-tunable config so server
//! owners can customize behaviour without touching script.
class GRAD_ArsenalServiceClass : GenericEntityClass
{
}

//------------------------------------------------------------------------------------------------
//! Singleton service entity for the arsenal. One instance per world owns:
//!   - the amortized catalog index build (driven from EOnFrame),
//!   - the in-memory "loadout clipboard" used by GM copy/paste,
//!   - the "last used loadout name" session state,
//!   - (P5+) the preview character lifecycle.
//!
//! Access via GRAD_ArsenalService.GetInstance(). The entity registers itself on init and clears
//! the reference on destruction. If two are accidentally placed, the first wins and the second
//! warns and stays inert.
class GRAD_ArsenalService : GenericEntity
{
	//! Entries converted per frame during catalog indexing. Exposed for server-owner tuning.
	[Attribute(defvalue: "64", desc: "Catalog entries indexed per frame (higher = faster build, more per-frame cost)")]
	protected int m_iEntriesPerFrame;

	//! Whether to build the catalog index automatically once catalogs are ready.
	[Attribute(defvalue: "1", desc: "Build the arsenal item index automatically at world start")]
	protected bool m_bAutoBuildIndex;

	//! The live singleton.
	protected static GRAD_ArsenalService s_Instance;

	//! The arsenal item index (built amortized).
	protected ref GRAD_CatalogIndex m_CatalogIndex;

	//! In-memory clipboard for GM copy/paste of a whole loadout. Null when nothing copied.
	protected ref GRAD_LoadoutData m_ClipboardLoadout;

	//! Last loadout name the local player used, for the "load previous" quick action.
	protected string m_sLastUsedLoadoutName;

	//------------------------------------------------------------------------------------------------
	static GRAD_ArsenalService GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	void GRAD_ArsenalService(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (s_Instance && s_Instance != this)
		{
			GRAD_Log.Warn("GRAD_ArsenalService: a second instance was placed; ignoring it");
			return;
		}

		s_Instance = this;
		m_CatalogIndex = new GRAD_CatalogIndex(m_iEntriesPerFrame);

		GRAD_Log.Info("GRAD_ArsenalService: initialized");

		if (m_bAutoBuildIndex)
			TryBeginIndexBuild();
	}

	//------------------------------------------------------------------------------------------------
	//! Begin the catalog build now if catalogs are ready; otherwise wait for the catalog-init
	//! invoker and build then.
	void TryBeginIndexBuild()
	{
		if (!m_CatalogIndex)
			return;

		if (SCR_EntityCatalogManagerComponent.GetInstance())
		{
			m_CatalogIndex.BeginBuild();
			return;
		}

		// Catalogs not ready yet — subscribe to the init event and build when it fires.
		ScriptInvokerVoid invoker = SCR_EntityCatalogManagerComponent.GetOnEntityCatalogInitialized();
		if (invoker)
			invoker.Insert(OnCatalogsReady);
		else
			GRAD_Log.Warn("GRAD_ArsenalService: catalogs not ready and no init invoker available");
	}

	//------------------------------------------------------------------------------------------------
	protected void OnCatalogsReady()
	{
		ScriptInvokerVoid invoker = SCR_EntityCatalogManagerComponent.GetOnEntityCatalogInitialized();
		if (invoker)
			invoker.Remove(OnCatalogsReady);

		if (m_CatalogIndex)
			m_CatalogIndex.BeginBuild();
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		// Drive the amortized catalog build. Cheap no-op once complete / when idle.
		if (m_CatalogIndex && m_CatalogIndex.IsBuilding())
			m_CatalogIndex.Tick();
	}

	//------------------------------------------------------------------------------------------------
	GRAD_CatalogIndex GetCatalogIndex()
	{
		return m_CatalogIndex;
	}

	//------------------------------------------------------------------------------------------------
	// ---- clipboard (GM copy/paste) --------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	void SetClipboard(GRAD_LoadoutData data)
	{
		m_ClipboardLoadout = data;
		GRAD_Log.Debug("GRAD_ArsenalService: clipboard set");
	}

	GRAD_LoadoutData GetClipboard()
	{
		return m_ClipboardLoadout;
	}

	bool HasClipboard()
	{
		return m_ClipboardLoadout != null;
	}

	//------------------------------------------------------------------------------------------------
	// ---- session state --------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------

	void SetLastUsedLoadoutName(string name)
	{
		m_sLastUsedLoadoutName = name;
	}

	string GetLastUsedLoadoutName()
	{
		return m_sLastUsedLoadoutName;
	}

	//------------------------------------------------------------------------------------------------
	void ~GRAD_ArsenalService()
	{
		if (s_Instance == this)
			s_Instance = null;
	}
}
