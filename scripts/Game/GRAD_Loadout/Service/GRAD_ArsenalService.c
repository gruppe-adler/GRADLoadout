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

	//! On-screen "Preloading arsenal…" progress indicator, shown while the index builds.
	protected ref GRAD_PreloadIndicator m_PreloadIndicator;

	//------------------------------------------------------------------------------------------------
	static GRAD_ArsenalService GetInstance()
	{
		return s_Instance;
	}

	//! Period (ms) of the build-driver timer. Small so the amortized build finishes quickly while
	//! staying spread across ticks. The entity FRAME event proved unreliable for a script-spawned
	//! service (it fired once then stopped), so the build is driven off the game callqueue instead.
	protected const int BUILD_TICK_MS = 33;

	//------------------------------------------------------------------------------------------------
	void GRAD_ArsenalService(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT);

		// [Attribute] defvalues only populate when the entity comes from a prefab/config. The preload
		// spawns this service by CLASS (SpawnEntity(GRAD_ArsenalService)), where attributes stay at
		// type defaults (0 / false) — which left m_bAutoBuildIndex false and the build never started.
		// Seed sane runtime defaults in the constructor so a bare-spawned service still builds; a placed
		// prefab still overrides these via its attribute values after construction.
		if (m_iEntriesPerFrame <= 0)
			m_iEntriesPerFrame = 64;
		m_bAutoBuildIndex = true;
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

		// Drive the build + indicator off the game callqueue (reliable, unlike the entity FRAME event
		// for a script-spawned entity). The driver kicks BeginBuild once catalogs are ready, ticks the
		// amortized build, updates the preload bar, and stops itself when complete.
		if (m_bAutoBuildIndex)
			GetGame().GetCallqueue().CallLater(GradBuildDriver, BUILD_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Repeating callqueue driver: start the build when catalogs are ready, tick it, update the bar,
	//! and stop once the index is complete.
	protected int m_iGradDriverTicks;

	protected void GradBuildDriver()
	{
		// One-shot proof the driver fires + state snapshot.
		if (m_iGradDriverTicks == 0)
			GRAD_Log.Info(string.Format("BuildDriver first tick: catalogMgr=%1",
				SCR_EntityCatalogManagerComponent.GetInstance() != null));
		m_iGradDriverTicks++;

		if (!m_CatalogIndex)
		{
			GetGame().GetCallqueue().Remove(GradBuildDriver);
			return;
		}

		if (m_CatalogIndex.IsComplete())
		{
			UpdatePreloadIndicator();
			GetGame().GetCallqueue().Remove(GradBuildDriver);
			return;
		}

		if (!m_CatalogIndex.IsBuilding() && SCR_EntityCatalogManagerComponent.GetInstance())
			m_CatalogIndex.BeginBuild();

		if (m_CatalogIndex.IsBuilding())
			m_CatalogIndex.Tick();

		UpdatePreloadIndicator();
	}

	//------------------------------------------------------------------------------------------------
	//! Show a small "Preloading arsenal…" bar while the index builds; tear it down once complete.
	protected void UpdatePreloadIndicator()
	{
		if (!m_CatalogIndex)
			return;

		if (m_CatalogIndex.IsComplete())
		{
			if (m_PreloadIndicator)
			{
				m_PreloadIndicator.Destroy();
				m_PreloadIndicator = null;
			}
			return;
		}

		// Building (or about to): make sure the indicator exists and reflects progress.
		if (m_CatalogIndex.IsBuilding())
		{
			if (!m_PreloadIndicator)
				m_PreloadIndicator = new GRAD_PreloadIndicator();
			m_PreloadIndicator.Update(m_CatalogIndex.GetProgress());
		}
	}

	//------------------------------------------------------------------------------------------------
	GRAD_CatalogIndex GetCatalogIndex()
	{
		return m_CatalogIndex;
	}

	//------------------------------------------------------------------------------------------------
	//! True once the catalog index has finished its amortized build — i.e. the arsenal is ready to
	//! open with a fully-populated item browser. Entry points (GM action, arsenal box) gate their
	//! availability on this so the "Open Arsenal" button only appears after preload completes.
	bool IsCatalogReady()
	{
		return m_CatalogIndex && m_CatalogIndex.IsComplete();
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
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(GradBuildDriver);

		if (m_PreloadIndicator)
		{
			m_PreloadIndicator.Destroy();
			m_PreloadIndicator = null;
		}

		if (s_Instance == this)
			s_Instance = null;
	}
}
