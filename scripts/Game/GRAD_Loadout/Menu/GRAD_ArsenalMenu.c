//------------------------------------------------------------------------------------------------
//! Extends the engine menu-preset enum with our arsenal menu. Registered to a layout + this menu
//! class via Configs/UI/GRAD_ArsenalMenu.conf (a MenuPreset entry).
modded enum ChimeraMenuPreset
{
	GRAD_ArsenalMenu
}

//------------------------------------------------------------------------------------------------
//! Full-screen virtual arsenal menu (MVP).
//!
//! UI concept (docs/DECISIONS.md D5): a left category rail drives a single content panel; the
//! preview character sits center-left. This MVP wires:
//!   - the preview character (local-only clone of the target) via ItemPreviewManagerEntity (D1),
//!   - the category rail -> item browser for the focused category,
//!   - OK (serialize preview -> apply RPC on the real target) / Cancel (discard preview).
//!
//! All edits happen on the LOCAL preview character only. Nothing touches the networked target
//! until OK, which sends the apply RPC through GRAD_LoadoutManagerComponent (P3).
//!
//! Opened with a GRAD_ArsenalMenuContext (set just before OpenMenu) describing the target(s).
class GRAD_ArsenalMenu : ChimeraMenuBase
{
	// Widget names expected in the layout (UI/Layouts/GRAD_ArsenalMenu.layout).
	protected const string WIDGET_PREVIEW			= "PaneCenterPreview";
	protected const string WIDGET_CATEGORY_TABVIEW	= "CategoryTabView";	// SCR_TabViewComponent host
	protected const string WIDGET_ITEM_GRID			= "CategoryItems";	// single shared grid, repopulated per tab
	protected const string WIDGET_BTN_OK			= "ButtonOK";
	protected const string WIDGET_BTN_CANCEL		= "ButtonCancel";
	protected const string WIDGET_BTN_IMPORT		= "ButtonLoad";
	protected const string WIDGET_BTN_EXPORT		= "ButtonSave";
	protected const string WIDGET_TITLE				= "Title";

	// Item card child widget names (UI/Layouts/GRAD_ItemCard.layout).
	protected const string WIDGET_CARD_ICON			= "CardIcon";
	protected const string WIDGET_CARD_NAME			= "CardName";
	protected const string WIDGET_CARD_COUNT		= "CardCount";
	protected const string WIDGET_CARD_WEIGHT		= "CardWeight";
	protected const string WIDGET_CARD_BG			= "TileBg";

	// Selected-item panel widget names.
	protected const string WIDGET_SEL_ICON			= "SelIcon";
	protected const string WIDGET_SEL_NAME			= "SelName";
	protected const string WIDGET_SEL_STATS			= "SelStats";
	protected const string WIDGET_BTN_ADD_VEST		= "ButtonAddVest";
	protected const string WIDGET_BTN_ADD_BACKPACK	= "ButtonAddBackpack";
	protected const string WIDGET_BTN_ADD_EQUIP		= "ButtonAddEquip";

	// Right loadout panel slot widget names (Uniform / Vest / Backpack: bar, percent, contents).
	protected const string WIDGET_SLOT_UNIFORM_BAR		= "UniformBar";
	protected const string WIDGET_SLOT_UNIFORM_PCT		= "UniformPercent";
	protected const string WIDGET_SLOT_UNIFORM_CONTENTS	= "UniformContents";
	protected const string WIDGET_SLOT_VEST_BAR			= "VestBar";
	protected const string WIDGET_SLOT_VEST_PCT			= "VestPercent";
	protected const string WIDGET_SLOT_VEST_CONTENTS	= "VestContents";
	protected const string WIDGET_SLOT_BACKPACK_BAR		= "BackpackBar";
	protected const string WIDGET_SLOT_BACKPACK_PCT		= "BackpackPercent";
	protected const string WIDGET_SLOT_BACKPACK_CONTENTS = "BackpackContents";

	// Pixel width of a full fill bar (the bar image's max width in the layout).
	protected const float BAR_FULL_WIDTH = 220;

	// Runtime context (target entities, permission hint) set before the menu opens.
	protected static ref GRAD_ArsenalMenuContext s_PendingContext;

	protected ref GRAD_ArsenalMenuContext m_Context;

	// Preview character + its render plumbing.
	protected IEntity m_PreviewCharacter;
	protected ItemPreviewWidget m_wPreview;
	protected ItemPreviewManagerEntity m_PreviewManager;
	protected ref SCR_InventoryCharacterWidgetHelper m_PreviewCameraHelper;

	// Persistent preview-camera render attributes. The SAME instance must be handed to every
	// SetPreviewItem* call AND fed to the helper's Update() each frame, so mouse drag-rotate / zoom
	// accumulate into the object the preview manager renders from. Declared as the BASE type so it can
	// pass to Update(inout PreviewRenderAttributes) (inout requires an exact type match), but
	// INSTANTIATED as the character subclass (a bare PreviewRenderAttributes renders nothing).
	protected ref PreviewRenderAttributes m_PreviewAttribs;

	// The prefab the engine-managed preview entity was resolved from (for re-resolve after edits).
	protected ResourceName m_sPreviewPrefab;

	// Entities created on the preview character by the last apply (for cleanup).
	protected ref array<IEntity> m_aPreviewCreated = {};

	// The category currently in focus (drives the item browser).
	protected int m_iSelectedCategory = -1;

	// Item browser (query/grouping over the catalog index) + the container widget it fills.
	protected ref GRAD_ItemBrowser m_Browser;

	// The category tab strip. Built from vanilla WLib_TabViewCoreMenus.layout + SCR_TabViewComponent
	// (5 tabs declared as data in the layout, m_bCreateAllTabsAtStart/m_bKeepHiddenTabs so every tab's
	// content pane widget exists immediately — no hand-built bar, no script-painted highlight; the
	// component owns the tab buttons, active-tab look, and E/Q paging natively). We still poll
	// GetShownTab() each frame (PollTabChange) to react when it changes — see that method's comment.
	protected SCR_TabViewComponent m_TabView;

	// The single shared item grid ("CategoryItems" in the layout, a sibling of the tab strip — NOT
	// one grid per tab pane). Resolved once in SetupCategoryRail and repopulated in place by
	// PopulateItems() every time the selected tab changes.
	protected Widget m_wItemList;

	// Tab button layout (vanilla text button + our SCR_InputButtonComponent).
	protected const ResourceName ROW_LAYOUT = "{4BE35AEBB44455F0}UI/Layouts/GRAD_ListButtonRow.layout";

	// Item card layout (icon + name + count/weight). GUID assigned by Workbench on import; the
	// placeholder here is replaced when the layout is registered.
	protected const ResourceName CARD_LAYOUT = "{A704EDAAAADC6ADB}UI/Layouts/GRAD_ItemCard.layout";

	// Loadout-panel contents line: [-] [+] label row. GUID must match GRAD_LoadoutLine.layout.meta.
	protected const ResourceName LINE_LAYOUT = "{C1D2E3F401020304}UI/Layouts/GRAD_LoadoutLine.layout";
	protected const string WIDGET_LINE_MINUS = "LineMinus";
	protected const string WIDGET_LINE_PLUS  = "LinePlus";
	protected const string WIDGET_LINE_LABEL = "LineLabel";
	protected const string WIDGET_LINE_COUNT = "LineCount";

	// Live row handlers, kept alive for the menu's lifetime so their invokers stay valid.
	// m_aRowHandlers: category-rail handlers (built once). m_aItemRowHandlers: item-list handlers,
	// rebuilt on every PopulateItems — MUST be cleared each rebuild, else stale handlers keep
	// invokers bound to destroyed widgets and the menu crashes when one fires.
	protected ref array<ref GRAD_ArsenalRowHandler> m_aRowHandlers = {};
	protected ref array<ref GRAD_ArsenalRowHandler> m_aItemRowHandlers = {};

	// Loadout-panel line handlers ([-]/[+] per contained item). Rebuilt on every RefreshLoadoutPanel
	// (cleared there), so no stale invoker fires on a freed line widget.
	protected ref array<ref GRAD_LoadoutLineHandler> m_aLoadoutLineHandlers = {};

	// Expansion state of base-name sub-groups in the item grid, keyed by group label. Default
	// collapsed; persists across re-populates so toggling one group doesn't reset the others.
	protected ref map<string, bool> m_mExpandedGroups = new map<string, bool>();

	// The item currently selected into the Selected-Item panel (drives the ADD buttons). Null = none.
	protected ref GRAD_ArsenalItemRecord m_SelectedRecord;

	// The card widget (TileBg image inside it) currently highlighted as "selected". SelectedPanel is
	// hidden, so this tile recolor is the only visual feedback for which item is selected. Cleared to
	// null on every PopulateItems (the old widget is destroyed by ClearChildren).
	protected ImageWidget m_wSelectedCardBg;

	// prefab -> count on the preview, recomputed once per PopulateItems (walking the whole inventory
	// per card was O(cards x items) and made the grid sluggish).
	protected ref map<ResourceName, int> m_mPreviewCounts = new map<ResourceName, int>();

	// Item grid wrap: cards flow left-to-right into GRID_COLUMNS columns, wrapping to the next row.
	// GridLayoutWidget does not auto-wrap, so we place each card at (cell % cols, cell / cols).
	protected const int GRID_COLUMNS = 2;
	protected int m_iGridCell;

	//------------------------------------------------------------------------------------------------
	//! Stash the context that the next OpenMenu(GRAD_ArsenalMenu) call should pick up.
	static void SetPendingContext(GRAD_ArsenalMenuContext context)
	{
		s_PendingContext = context;
	}

	//------------------------------------------------------------------------------------------------
	//! Convenience: open the arsenal for the given context.
	static GRAD_ArsenalMenu Open(GRAD_ArsenalMenuContext context)
	{
		SetPendingContext(context);

		MenuManager mm = GetGame().GetMenuManager();
		if (!mm)
			return null;

		return GRAD_ArsenalMenu.Cast(mm.OpenMenu(ChimeraMenuPreset.GRAD_ArsenalMenu));
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_Context = s_PendingContext;
		s_PendingContext = null;

		// A context is required, but a targetless context is allowed: the browser UI still renders;
		// only the live preview and OK-apply need a target. (Targetless is mainly a debug/render
		// path — normal entry points always supply at least one target.)
		if (!m_Context)
			m_Context = new GRAD_ArsenalMenuContext();

		if (!m_Context.HasTargets())
			GRAD_Log.Warn("ArsenalMenu: opened with no target; preview + apply disabled");

		Widget root = GetRootWidget();
		if (!root)
		{
			GRAD_Log.Error("ArsenalMenu: no root widget");
			Close();
			return;
		}

		BindButtons(root);
		SetupPreview(root);
		SetupCategoryRail(root);

		GRAD_Log.Info("ArsenalMenu: opened");
	}

	//------------------------------------------------------------------------------------------------
	protected void BindButtons(notnull Widget root)
	{
		SCR_InputButtonComponent okBtn = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_OK, root);
		if (okBtn)
			okBtn.m_OnActivated.Insert(OnConfirm);

		SCR_InputButtonComponent cancelBtn = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_CANCEL, root);
		if (cancelBtn)
			cancelBtn.m_OnActivated.Insert(OnCancel);

		SCR_InputButtonComponent addVest = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_ADD_VEST, root);
		if (addVest)
			addVest.m_OnActivated.Insert(OnAddToVest);

		SCR_InputButtonComponent addBackpack = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_ADD_BACKPACK, root);
		if (addBackpack)
			addBackpack.m_OnActivated.Insert(OnAddToBackpack);

		SCR_InputButtonComponent addEquip = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_ADD_EQUIP, root);
		if (addEquip)
			addEquip.m_OnActivated.Insert(OnEquipSelected);

		SCR_InputButtonComponent importBtn = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_IMPORT, root);
		if (importBtn)
			importBtn.m_OnActivated.Insert(OnImportClicked);

		SCR_InputButtonComponent exportBtn = SCR_InputButtonComponent.GetInputButtonComponent(WIDGET_BTN_EXPORT, root);
		if (exportBtn)
			exportBtn.m_OnActivated.Insert(OnExportClicked);

		// Give the ADD buttons their captions (they inherit WLib_ButtonText, which defaults to "Button").
		SetButtonText(root, WIDGET_BTN_ADD_VEST, "ADD TO VEST");
		SetButtonText(root, WIDGET_BTN_ADD_BACKPACK, "ADD TO BACKPACK");
		SetButtonText(root, WIDGET_BTN_ADD_EQUIP, "EQUIP");

		// Start with nothing selected → ADD buttons disabled.
		SetAddButtonsEnabled(false, false, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Bind an engine-managed preview entity of the primary target's prefab to the preview widget.
	//!
	//! Uses SetPreviewItemFromPrefab + ResolvePreviewEntityForPrefab so the ItemPreviewManager OWNS the
	//! preview entity's lifetime — that fixes the despawn (a hand-spawned clone got reaped by the
	//! character lifetime system, and Deactivate()-pinning it broke render/input). Rotation/zoom are
	//! driven by ONE persistent SCR_CharacterInventoryPreviewAttributes fed into both the manager and
	//! the widget helper's Update() each frame.
	protected void SetupPreview(notnull Widget root)
	{
		m_wPreview = ItemPreviewWidget.Cast(root.FindAnyWidget(WIDGET_PREVIEW));
		if (!m_wPreview)
		{
			GRAD_Log.Warn("ArsenalMenu: preview widget not found in layout");
			return;
		}

		ChimeraWorld world = GetGame().GetWorld();
		if (world)
			m_PreviewManager = world.GetItemPreviewManager();

		IEntity primary = m_Context.GetPrimaryTarget();
		m_sPreviewPrefab = GRAD_InventoryLib.GetPrefabResourceName(primary);

		if (m_sPreviewPrefab != ResourceName.Empty && m_PreviewManager)
		{
			// One persistent attributes object; the manager renders from it and the helper mutates it
			// from mouse input. MUST be the CHARACTER subclass — a bare PreviewRenderAttributes has no
			// character framing, so the camera sits inside the mesh (extreme zoom) and drag does nothing.
			// Field stays typed as base PreviewRenderAttributes to match Update(inout ...).
			m_PreviewAttribs = new SCR_CharacterInventoryPreviewAttributes();

			// Spawn a local clone and bind it to the widget — this is the render path that actually shows
			// the character (the from-prefab/resolve path rendered blank). Despawn is mitigated by
			// Deactivate() (PinPreviewAlive) which stops the lifetime tick while the manager keeps drawing.
			m_PreviewCharacter = GRAD_InventoryLib.SpawnLocal(m_sPreviewPrefab);

			if (m_PreviewCharacter)
			{
				m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, m_PreviewAttribs);

				// Match the edited unit's face/appearance (a fresh spawn gets a random identity).
				CopyIdentity(primary, m_PreviewCharacter);

				// Mirror the target's current loadout so the player edits from their real starting kit.
				// force=false: keep the prefab's locked cosmetic body/clothing nodes; only mirror the
				// editable items on top.
				GRAD_LoadoutData current = GRAD_LoadoutCapture.Capture(primary, "PreviewBase", true);
				if (current)
					GRAD_LoadoutApply.Apply(m_PreviewCharacter, current, true, false, m_aPreviewCreated);

				// Refresh render with the mirrored kit; same persistent attribs (not null).
				m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, m_PreviewAttribs, true);

				// Establish an initial frame. The attributes object carries NO distance/offset field
				// (API-verified) — RotateItemCamera + ZoomCamera(FOV) are the only levers. Without this
				// the camera defaults to the model origin (inside the chest), so the preview showed a
				// close-up of fabric. Push a facing rotation + a wider FOV to pull the camera out to a
				// full-body shot, then re-render from the same attribs instance.
				FrameFullBody();
				m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, m_PreviewAttribs, true);

				PinPreviewAlive();
			}
		}

		// Mouse orbit + wheel zoom. The helper is a ScriptedWidgetEventHandler — its mouse overrides
		// only fire once registered on the widget via AddHandler.
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (m_wPreview && workspace)
		{
			m_PreviewCameraHelper = new SCR_InventoryCharacterWidgetHelper(m_wPreview, workspace);
			m_wPreview.AddHandler(m_PreviewCameraHelper);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Copy the source character's identity (face/voice/appearance) onto the destination clone so the
	//! preview looks like the actual unit, not a random spawn.
	protected void CopyIdentity(IEntity source, IEntity dest)
	{
		if (!source || !dest)
			return;

		CharacterIdentityComponent srcId = CharacterIdentityComponent.Cast(source.FindComponent(CharacterIdentityComponent));
		CharacterIdentityComponent dstId = CharacterIdentityComponent.Cast(dest.FindComponent(CharacterIdentityComponent));
		if (!srcId || !dstId)
			return;

		Identity identity = srcId.GetIdentity();
		if (!identity)
			return;

		dstId.SetIdentity(identity);
		dstId.CommitChanges();
	}

	//------------------------------------------------------------------------------------------------
	//! Refresh the preview render after a mutation, keeping the same persistent attribs so the camera
	//! rotation/zoom don't reset. The engine-managed preview entity owns its own lifetime, so there is
	//! no Activate/Deactivate pinning to do.
	protected void RefreshPreviewRender()
	{
		if (m_PreviewManager && m_wPreview && m_PreviewCharacter)
			m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, m_PreviewAttribs, true);

		PinPreviewAlive();
	}

	//------------------------------------------------------------------------------------------------
	//! Frame the whole character. RotateItemCamera adds a delta rotation (clamped by the min/max
	//! vectors); ZoomCamera ADDS its first arg to whatever FOV the camera currently has, then clamps
	//! the result to [minFOV, maxFOV] (API-verified: "Add FOV to the current camera" — it is a
	//! relative increment, NOT an absolute target FOV). The previous code passed a modest +90/+70
	//! increment, which only reaches max FOV if the engine's undocumented starting FOV happens to be
	//! low enough — otherwise it lands short of maxFOV and the fixed-distance close-up camera stays
	//! cropped in tight (matches the observed "torso-only, no head/feet" symptom: a wide FOV close up
	//! shows more of the body; a narrower one shows less). There is no distance/pivot API (verified),
	//! so a huge increment that unconditionally clamps at maxFOV=120 is used instead — this removes
	//! any dependency on the unknown baseline FOV and deterministically maxes out the field of view.
	protected void FrameFullBody()
	{
		if (!m_PreviewAttribs)
			return;

		m_PreviewAttribs.ResetDeltaRotation();
		// Yaw so the character faces the viewer (0 showed the back; 360-ish/0 = front — the delta is
		// additive, so 0 keeps the default facing which is front-on for the preview character).
		m_PreviewAttribs.RotateItemCamera("0 0 0", "-90 -180 0", "90 180 0");
		// Oversized increment guarantees the clamp lands exactly at maxFOV regardless of starting FOV.
		m_PreviewAttribs.ZoomCamera(1000.0, 25.0, 120.0);
	}

	//------------------------------------------------------------------------------------------------
	//! Approximate a body-region focus for the given tab. There is NO camera focus/look-at/pivot API
	//! (API-verified) — the only levers are pitch (RotateItemCamera) + FOV (ZoomCamera, which ADDS to
	//! the current FOV — see FrameFullBody's note). Framing is therefore approximate: the character
	//! origin is fixed, so narrowing FOV zooms toward center and a pitch tilt shifts which region
	//! crosses frame-center. Tune live.
	protected void FrameForCategory(int tabIndex)
	{
		if (!m_PreviewAttribs)
			return;

		m_PreviewAttribs.ResetDeltaRotation();

		// tabIndex: 0 Primary, 1 Secondary, 2 Throwables, 3 Apparel, 4 Container.
		// Apparel gets a slight downward tilt + tighter FOV (head/torso); the rest stay full-body.
		// ZoomCamera's first arg is a relative increment (see FrameFullBody). To land deterministically
		// on a target FOV regardless of the current/starting FOV, drive the increment hard in the
		// desired direction and let the clamp pin the result at the corresponding bound: a large
		// negative increment clamps at minFOV (here raised to 70 as the floor -> lands at 70), a large
		// positive increment clamps at maxFOV (120).
		if (tabIndex == 3)
		{
			m_PreviewAttribs.RotateItemCamera("-10 0 0", "-90 -180 0", "90 180 0");
			m_PreviewAttribs.ZoomCamera(-1000.0, 70.0, 120.0);
		}
		else
		{
			m_PreviewAttribs.RotateItemCamera("0 0 0", "-90 -180 0", "90 180 0");
			m_PreviewAttribs.ZoomCamera(1000.0, 25.0, 120.0);
		}

		if (m_PreviewManager && m_wPreview && m_PreviewCharacter)
			m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, m_PreviewAttribs, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Deactivate the preview clone's entity events so the character lifetime system stops ticking it
	//! toward cleanup, while the preview manager keeps rendering it. Safe to call repeatedly.
	protected void PinPreviewAlive()
	{
		GenericEntity ge = GenericEntity.Cast(m_PreviewCharacter);
		if (ge)
			ge.Deactivate();
	}

	//------------------------------------------------------------------------------------------------
	//! Apply a single-item loadout additively to the preview character, then refresh the render. The
	//! clone is re-activated for the mutation (some inventory ops expect an active entity), then re-pinned.
	protected void ApplyToPreview(notnull GRAD_LoadoutData data, BaseInventoryStorageComponent preferredStorage = null)
	{
		if (!m_PreviewCharacter)
			return;

		GenericEntity ge = GenericEntity.Cast(m_PreviewCharacter);
		if (ge)
			ge.Activate();

		// clearFirst=false: this is an ADDITIVE single-item add; do not strip the existing kit.
		// preferredStorage (may be null) routes a stackable item into the chosen destination container.
		array<IEntity> created = {};
		GRAD_LoadoutApply.Apply(m_PreviewCharacter, data, true, false, created, false, preferredStorage);
		foreach (IEntity e : created)
			m_aPreviewCreated.Insert(e);

		RefreshPreviewRender();
	}

	//------------------------------------------------------------------------------------------------
	//! Build the item browser from the catalog index and wire up the category TabView. The item grid
	//! (CategoryItems) is a SINGLE shared widget living alongside the tab strip (not one per tab pane)
	//! — resolved once here and repopulated in place on every tab switch.
	protected void SetupCategoryRail(notnull Widget root)
	{
		Widget tabViewWidget = root.FindAnyWidget(WIDGET_CATEGORY_TABVIEW);
		if (tabViewWidget)
			m_TabView = SCR_TabViewComponent.Cast(tabViewWidget.FindHandler(SCR_TabViewComponent));

		if (!m_TabView)
		{
			GRAD_Log.Warn("ArsenalMenu: CategoryTabView / SCR_TabViewComponent not found");
		}
		else
		{
			// The component's own AddActionListeners() reacts to m_sActionLeft/m_sActionRight directly,
			// AND the paging-button widgets baked into WLib_TabViewCoreMenus.layout (m_PagingLeft/
			// m_PagingRight, SCR_PagingButtonComponent) independently react to the SAME bound action via
			// their own click handling — both paths call back into OnTabRight()/OnTabLeft(), so one E
			// press was advancing the tab twice. SetListenToActions(false) disables the component's own
			// direct listener, leaving only the paging-button widgets' handling (which still works, since
			// they're what's visibly wired to the E/Q hint chips in the tab strip).
			m_TabView.SetListenToActions(false);
		}
		// Tab-change reaction is via PollTabChange() in OnMenuUpdate, not GetOnChanged() — see
		// PollTabChange's comment for why.

		m_wItemList = root.FindAnyWidget(WIDGET_ITEM_GRID);
		if (!m_wItemList)
			GRAD_Log.Warn(string.Format("ArsenalMenu: item grid widget '%1' not found in layout", WIDGET_ITEM_GRID));

		// Source the records from the singleton service's (amortized) catalog index. The service is
		// normally placed in the world, but ensure one exists so the browser works from any entry
		// point (GM right-click, arsenal box, etc.).
		if (!GRAD_ArsenalService.GetInstance())
			GRAD_MenuTest.SpawnService();

		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service || !service.GetCatalogIndex())
		{
			GRAD_Log.Warn("ArsenalMenu: no catalog index available; item browser empty");
			return;
		}

		GRAD_CatalogIndex index = service.GetCatalogIndex();

		// The index builds amortized over frames, so it is often not finished when the menu opens.
		// Populate from whatever is ready now, AND subscribe to OnComplete to repopulate when the
		// full index lands. If the build hasn't started yet, kick it off.
		if (!index.IsComplete() && !index.IsBuilding())
			index.BeginBuild();

		if (!index.IsComplete())
			index.GetOnComplete().Insert(OnCatalogReady);

		RebuildBrowser(index);
	}

	//------------------------------------------------------------------------------------------------
	//! Repopulate the rail/list once the catalog index finishes building.
	protected void OnCatalogReady()
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!service || !service.GetCatalogIndex())
			return;

		service.GetCatalogIndex().GetOnComplete().Remove(OnCatalogReady);
		RebuildBrowser(service.GetCatalogIndex());
	}

	//------------------------------------------------------------------------------------------------
	//! (Re)build the item browser from the current index contents and refill the rail.
	protected void RebuildBrowser(notnull GRAD_CatalogIndex index)
	{
		m_Browser = new GRAD_ItemBrowser(index.GetRecords());

		// Optionally scope to the target's faction so the rail shows relevant items first.
		ChimeraCharacter primaryChar = ChimeraCharacter.Cast(m_Context.GetPrimaryTarget());
		if (primaryChar)
		{
			SCR_ChimeraCharacter scrChar = SCR_ChimeraCharacter.Cast(primaryChar);
			if (scrChar)
				m_Browser.SetFactionKey(scrChar.GetFactionKey());
		}

		GRAD_Log.Info(string.Format("ArsenalMenu: browser has %1 records, %2 categories",
			index.GetRecordCount(), m_Browser.GetCategoryCount()));

		// Explicitly show tab 0 (Primary) so the component's own active-tab highlight is correct at
		// open; SelectCategoryByIndex(0) then resolves the grid + populates it (ShowTab's own
		// GetOnChanged firing on an already-shown index 0 is harmless — SelectCategoryByIndex is
		// idempotent for repeated calls with the same index).
		if (m_TabView)
			m_TabView.ShowTab(0);
		SelectCategoryByIndex(0);	// default to the Primary tab
		RefreshLoadoutPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! Poll the TabView's shown-tab index once per frame (called from OnMenuUpdate) and react on
	//! change. Avoids binding SCR_TabViewComponent.GetOnChanged() directly — its callback prototype
	//! (ScriptInvokerTabViewIndexMethod) rejects both a 1-arg and a 0-arg handler at compile time, and
	//! its exact required signature isn't in the indexed API docs. A cheap int-compare poll sidesteps
	//! the whole guessing game while still reacting the same frame a click or Q/E paging changes tabs.
	protected void PollTabChange()
	{
		if (!m_TabView)
			return;

		int shown = m_TabView.GetShownTab();
		if (shown != m_iSelectedCategory)
			SelectCategoryByIndex(shown);
	}

	//------------------------------------------------------------------------------------------------
	//! Select a tab by index (0..4): filter the browser to that tab's item types, reframe the preview,
	//! and repopulate the SHARED item grid (m_wItemList — resolved once in SetupCategoryRail; there is
	//! only one grid widget, not one per tab, so no re-resolution needed here).
	void SelectCategoryByIndex(int tabIndex)
	{
		m_iSelectedCategory = tabIndex;
		if (m_Browser)
			m_Browser.SetCategoryMask(GRAD_ArsenalTabs.MaskFor(tabIndex));

		GRAD_Log.Info(string.Format("SelectTab %1 mask=%2 itemGridFound=%3",
			tabIndex, GRAD_ArsenalTabs.MaskFor(tabIndex), m_wItemList != null));

		FrameForCategory(tabIndex);
		PopulateItems();
	}

	//------------------------------------------------------------------------------------------------
	//! Fill the item grid with the current tab's filtered records as icon cards. Variants of one base
	//! name collapse under a header card that expands to its variant cards (reuses m_mExpandedGroups).
	protected void PopulateItems()
	{
		if (!m_wItemList || !m_Browser)
			return;

		// Drop the previous card handlers BEFORE destroying their widgets, so no stale handler keeps an
		// invoker bound to a freed widget (that crashes the menu on the next click).
		m_aItemRowHandlers.Clear();
		ClearChildren(m_wItemList);
		m_iGridCell = 0;	// reset the grid wrap counter for this rebuild
		m_wSelectedCardBg = null;	// the widget it pointed at was just destroyed above

		// Precompute prefab->count over the preview once (cheap map), so each card is O(1) not O(items).
		RebuildPreviewCounts();

		array<ref GRAD_ItemGroup> groups = {};
		m_Browser.GetGrouped(groups);

		foreach (GRAD_ItemGroup group : groups)
		{
			if (!group || group.GetCount() == 0)
				continue;

			if (group.GetCount() == 1)
			{
				CreateItemCard(group.m_aItems[0], group.m_aItems[0].m_sDisplayName);
				continue;
			}

			// Multi-variant: a header card (click to expand/collapse) then, when expanded, one card per
			// variant labelled with its concise variant suffix.
			bool expanded = IsGroupExpanded(group.m_sLabel);
			CreateGroupHeaderCard(group, expanded);

			if (expanded)
			{
				foreach (GRAD_ArsenalItemRecord rec : group.m_aItems)
				{
					string variant = ConciseVariant(rec);
					if (GRAD_CommonUtils.IsBlank(variant))
						variant = rec.m_sDisplayName;
					CreateItemCard(rec, variant);
				}
			}
		}

		// Give every column/row an equal fill weight so the grid actually flows the fixed-size tiles
		// instead of collapsing them to zero width/height (the root cause of the old overlapping-list
		// bug: GridLayoutWidget only exposes Set{Row,Column}FillWeight — with no weights set, cells
		// have no claim on the available space). Then force a layout pass.
		GridLayoutWidget grid = GridLayoutWidget.Cast(m_wItemList);
		if (grid)
		{
			for (int c = 0; c < GRID_COLUMNS; c++)
				grid.SetColumnFillWeight(c, 1.0);

			int rows = (m_iGridCell + GRID_COLUMNS - 1) / GRID_COLUMNS;
			for (int r = 0; r < rows; r++)
				grid.SetRowFillWeight(r, 1.0);

			grid.Update();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! A concise variant label for a child row: the friendly prefab stem with the base-name words and
	//! a leading generic noun (Rifle/Pistol/Magazine/Box/Optic/Vest/Jacket/Hat/Helmet/Backpack...)
	//! removed, so "Rifle M21 ARTII OliveGreen Sand Stripes" under base "M21 SWS" reads "ARTII
	//! OliveGreen Sand Stripes". Falls back to the full suffix if everything got stripped.
	protected string ConciseVariant(notnull GRAD_ArsenalItemRecord rec)
	{
		string suffix = rec.m_sVariantSuffix;
		if (GRAD_CommonUtils.IsBlank(suffix))
			return string.Empty;

		array<string> words = {};
		suffix.Split(" ", words, true);

		// Base-name words to drop wherever they appear.
		array<string> baseWords = {};
		rec.m_sBaseName.Split(" ", baseWords, true);

		// Generic leading nouns to drop.
		string generics = " rifle pistol smg launcher gun magazine box belt ammo optic scope suppressor flashhider bayonet ugl vest jacket hat helmet cap backpack pants boots gloves shirt suit grenade smoke mine ";

		string result = "";
		foreach (string w : words)
		{
			string wl = w;
			wl.ToLower();

			// Skip base-name words.
			bool drop = false;
			foreach (string bw : baseWords)
			{
				string bwl = bw;
				bwl.ToLower();
				if (wl == bwl)
				{
					drop = true;
					break;
				}
			}
			// Skip a generic noun (only meaningful as a leading category word; cheap to drop anywhere).
			if (!drop && generics.Contains(" " + wl + " "))
				drop = true;

			if (drop)
				continue;

			if (result != "")
				result += " ";
			result += w;
		}

		return result;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a base-name group is currently expanded (default collapsed).
	protected bool IsGroupExpanded(string label)
	{
		bool expanded = false;
		m_mExpandedGroups.Find(label, expanded);
		return expanded;
	}

	//------------------------------------------------------------------------------------------------
	//! Collapsible group header card: "Label (N)" with an expand marker. Clicking toggles expansion.
	protected void CreateGroupHeaderCard(notnull GRAD_ItemGroup group, bool expanded)
	{
		string marker;
		if (expanded)
			marker = "[-] ";
		else
			marker = "[+] ";

		Widget card = CreateItemCardWidget(marker + group.m_sLabel, string.Format("%1", group.GetCount()), "", null);
		if (!card)
			return;

		GRAD_ArsenalRowHandler handler = new GRAD_ArsenalRowHandler(this, card);
		handler.m_bIsGroupHeader = true;
		handler.m_sGroupKey = group.m_sLabel;
		m_aItemRowHandlers.Insert(handler);
	}

	//------------------------------------------------------------------------------------------------
	//! Toggle a group's expansion and rebuild the grid.
	void OnGroupHeaderClicked(string groupKey)
	{
		m_mExpandedGroups.Set(groupKey, !IsGroupExpanded(groupKey));
		PopulateItems();
	}

	//------------------------------------------------------------------------------------------------
	//! Create one item card (icon + name + count-on-preview) for a record. Clicking selects the item
	//! into the Selected-Item panel (which offers the ADD buttons) rather than equipping immediately.
	protected void CreateItemCard(notnull GRAD_ArsenalItemRecord rec, string nameOverride)
	{
		string name = nameOverride;
		if (GRAD_CommonUtils.IsBlank(name))
			name = rec.m_sDisplayName;

		int count = 0;
		m_mPreviewCounts.Find(rec.m_sPrefab, count);	// O(1) from the precomputed map
		string countText = "";
		if (count > 0)
			countText = count.ToString();

		Widget card = CreateItemCardWidget(name, countText, "", rec.m_UiInfo);
		if (!card)
			return;

		GRAD_ArsenalRowHandler handler = new GRAD_ArsenalRowHandler(this, card);
		handler.m_Record = rec;
		handler.m_bIsCategory = false;
		m_aItemRowHandlers.Insert(handler);
	}

	//! Build an icon tile card from GRAD_ItemCard.layout and place it in the next grid cell (wrapping
	//! into GRID_COLUMNS columns). Fills the icon (from uiInfo), name, and count child widgets.
	protected Widget CreateItemCardWidget(string name, string count, string weight, SCR_UIInfo uiInfo)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return null;

		Widget card = workspace.CreateWidgets(CARD_LAYOUT, m_wItemList);
		if (!card)
			return null;

		// Wrap into a grid: place at (cell % cols, cell / cols), then advance.
		GridSlot.SetColumn(card, m_iGridCell % GRID_COLUMNS);
		GridSlot.SetRow(card, m_iGridCell / GRID_COLUMNS);
		m_iGridCell++;

		TextWidget nameW = TextWidget.Cast(card.FindAnyWidget(WIDGET_CARD_NAME));
		if (nameW)
		{
			nameW.SetText(name);
			nameW.SetTextWrapping(true);	// long names wrap instead of clipping (CardNameSize gives it a fixed 2-line box)
		}

		TextWidget countW = TextWidget.Cast(card.FindAnyWidget(WIDGET_CARD_COUNT));
		if (countW)
			countW.SetText(count);

		ImageWidget iconW = ImageWidget.Cast(card.FindAnyWidget(WIDGET_CARD_ICON));
		if (iconW)
		{
			if (uiInfo && uiInfo.HasIcon())
				uiInfo.SetIconTo(iconW);
			else
				iconW.SetVisible(false);
		}

		return card;
	}

	//------------------------------------------------------------------------------------------------
	//! Select an item into the Selected-Item panel: fill icon/name/stats and enable the ADD buttons
	//! for the containers that currently exist on the preview and have room. `cardWidget` is the
	//! clicked tile itself — SelectedPanel is hidden, so recoloring this tile's background is the
	//! only "which item is selected" feedback the user gets.
	void OnItemRowClicked(GRAD_ArsenalItemRecord record, Widget cardWidget)
	{
		m_SelectedRecord = record;

		if (m_wSelectedCardBg)
			m_wSelectedCardBg.SetColor(new Color(0.1, 0.11, 0.13, 0.9));	// un-highlight the previous tile

		m_wSelectedCardBg = null;
		if (cardWidget)
			m_wSelectedCardBg = ImageWidget.Cast(cardWidget.FindAnyWidget(WIDGET_CARD_BG));
		if (m_wSelectedCardBg)
			m_wSelectedCardBg.SetColor(new Color(0.55, 0.4, 0.1, 0.9));	// amber highlight, matches count color

		RefreshSelectedPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! Refresh the Selected-Item panel from m_SelectedRecord: icon, name, stats, and which ADD buttons
	//! are enabled (a container ADD is enabled only when that container exists on the preview & has a
	//! free slot; the generic "equip" ADD applies to apparel/weapons that auto-slot).
	protected void RefreshSelectedPanel()
	{
		Widget root = GetRootWidget();
		if (!root)
			return;

		TextWidget nameW = TextWidget.Cast(root.FindAnyWidget(WIDGET_SEL_NAME));
		TextWidget statsW = TextWidget.Cast(root.FindAnyWidget(WIDGET_SEL_STATS));
		ImageWidget iconW = ImageWidget.Cast(root.FindAnyWidget(WIDGET_SEL_ICON));

		if (!m_SelectedRecord)
		{
			if (nameW) nameW.SetText("");
			if (statsW) statsW.SetText("");
			if (iconW) iconW.SetVisible(false);
			SetAddButtonsEnabled(false, false, false);
			return;
		}

		if (nameW)
			nameW.SetText(m_SelectedRecord.m_sDisplayName);
		if (statsW)
			statsW.SetText(GRAD_ArsenalCategoryLabels.LabelFor(m_SelectedRecord.m_iArsenalType));
		if (iconW)
		{
			if (m_SelectedRecord.m_UiInfo && m_SelectedRecord.m_UiInfo.HasIcon())
			{
				iconW.SetVisible(true);
				m_SelectedRecord.m_UiInfo.SetIconTo(iconW);
			}
			else
			{
				iconW.SetVisible(false);
			}
		}

		// Decide which ADD buttons make sense. Cargo items (stackables) → vest/backpack when present +
		// have room. Apparel/weapons → the generic Equip add (auto-slots).
		bool stackable = GRAD_ArsenalCategoryLabels.IsStackable(m_SelectedRecord.m_iArsenalType);
		bool vestOk = stackable && FindContainerStorage(GRAD_ContainerTypes.MASK, true) != null;	// any cargo w/ room (approx)
		bool vest = stackable && FindNamedContainer(4096) != null;		// VEST_AND_WAIST
		bool backpack = stackable && FindNamedContainer(128) != null;	// BACKPACK
		bool equip = !stackable;										// clothing/weapon auto-slot

		SetAddButtonsEnabled(vest, backpack, equip);
	}

	//------------------------------------------------------------------------------------------------
	//! Enable/disable the three ADD buttons and update their captions.
	protected void SetAddButtonsEnabled(bool vest, bool backpack, bool equip)
	{
		Widget root = GetRootWidget();
		if (!root)
			return;

		SetButtonEnabled(root, WIDGET_BTN_ADD_VEST, vest);
		SetButtonEnabled(root, WIDGET_BTN_ADD_BACKPACK, backpack);
		SetButtonEnabled(root, WIDGET_BTN_ADD_EQUIP, equip);
	}

	//------------------------------------------------------------------------------------------------
	//! Find the preview's container storage owned by an item of the given arsenal-type bit (e.g. VEST
	//! 4096, BACKPACK 128). Returns null if that garment isn't equipped.
	protected BaseInventoryStorageComponent FindNamedContainer(int arsenalTypeBit)
	{
		return FindContainerStorage(arsenalTypeBit, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Find a container storage among the preview's cargo containers. If `anyWithRoom` is true, return
	//! the first with a free slot regardless of type; else return the one whose owner arsenal type has
	//! `typeMask` bit set.
	protected BaseInventoryStorageComponent FindContainerStorage(int typeMask, bool anyWithRoom)
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!m_PreviewCharacter || !service || !service.GetCatalogIndex())
			return null;

		array<ref GRAD_ContainerRef> containers = {};
		GRAD_InventoryLib.CollectDestinationContainers(m_PreviewCharacter, service.GetCatalogIndex(), containers);

		foreach (GRAD_ContainerRef c : containers)
		{
			if (!c || !c.m_Storage || !c.m_Owner)
				continue;

			if (anyWithRoom)
			{
				if (GRAD_InventoryLib.StorageHasFreeSlot(c.m_Storage))
					return c.m_Storage;
				continue;
			}

			ResourceName ownerPrefab = GRAD_InventoryLib.GetPrefabResourceName(c.m_Owner);
			int ownerType = service.GetCatalogIndex().GetArsenalTypeForPrefab(ownerPrefab);
			if ((ownerType & typeMask) != 0)
				return c.m_Storage;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! ADD TO VEST button: add the selected item into the vest storage. No-op if no vest is worn (the
	//! button is dimmed in that case, but guard anyway so a stray click doesn't fall through to equip).
	void OnAddToVest()
	{
		BaseInventoryStorageComponent vest = FindNamedContainer(4096);	// VEST_AND_WAIST
		if (vest)
			AddSelectedToContainer(vest);
	}

	//------------------------------------------------------------------------------------------------
	//! ADD TO BACKPACK button: add the selected item into the backpack storage. No-op if no backpack.
	void OnAddToBackpack()
	{
		BaseInventoryStorageComponent bag = FindNamedContainer(128);	// BACKPACK
		if (bag)
			AddSelectedToContainer(bag);
	}

	//------------------------------------------------------------------------------------------------
	//! EQUIP button: add the selected apparel/weapon (auto-slots to its loadout slot).
	void OnEquipSelected()
	{
		AddSelectedToContainer(null);	// null = engine auto-slot / equip
	}

	//------------------------------------------------------------------------------------------------
	//! Apply the selected record to the preview, routed into `storage` (null = auto/equip). Refreshes
	//! the grid (counts) and the loadout panel.
	protected void AddSelectedToContainer(BaseInventoryStorageComponent storage)
	{
		if (!m_SelectedRecord || !m_PreviewCharacter)
			return;

		GRAD_LoadoutEntry entry = GRAD_LoadoutEntry.Create(m_SelectedRecord.m_sPrefab, -1, string.Empty, 1);
		GRAD_LoadoutData single = new GRAD_LoadoutData();
		single.m_Root.AddChild(entry);
		ApplyToPreview(single, storage);

		PopulateItems();			// refresh counts + newly-available containers
		RefreshSelectedPanel();		// re-evaluate ADD buttons (new container may now exist)
		RefreshLoadoutPanel();		// update fill bars + contents
	}

	//------------------------------------------------------------------------------------------------
	//! Recompute prefab->count over the whole preview inventory once (into m_mPreviewCounts), so each
	//! card can look up its count in O(1). Reuses the shared CountPrefabInstances helper.
	protected void RebuildPreviewCounts()
	{
		m_mPreviewCounts.Clear();
		if (!m_PreviewCharacter)
			return;

		array<IEntity> items = {};
		GRAD_InventoryLib.CollectAllItems(m_PreviewCharacter, items);
		GRAD_InventoryLib.CountPrefabInstances(items, m_mPreviewCounts);
	}

	//------------------------------------------------------------------------------------------------
	//! Number of instances of a prefab currently equipped on the preview character (single lookup).
	protected int CountOnPreview(ResourceName prefab)
	{
		if (!m_PreviewCharacter)
			return 0;

		array<IEntity> items = {};
		GRAD_InventoryLib.CollectAllItems(m_PreviewCharacter, items);

		int count = 0;
		foreach (IEntity item : items)
		{
			if (item && GRAD_InventoryLib.GetPrefabResourceName(item) == prefab)
				count++;
		}
		return count;
	}

	//------------------------------------------------------------------------------------------------
	//! Remove a single instance of a prefab from the preview character, preferring an instance sitting
	//! directly in `preferredStorage` (the container the clicked contents line belongs to — a loadout
	//! line's [-] should remove from THAT container, not from wherever CollectAllItems happens to find
	//! the prefab first elsewhere on the preview). Falls back to a preview-wide search if the prefab
	//! isn't found in `preferredStorage` (defensive; the line's storage should normally still hold it).
	//! Returns true if one was actually removed.
	protected bool RemoveOneFromPreview(ResourceName prefab, BaseInventoryStorageComponent preferredStorage = null)
	{
		SCR_InventoryStorageManagerComponent manager =
			SCR_InventoryStorageManagerComponent.Cast(m_PreviewCharacter.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!manager)
			return false;

		IEntity target = null;
		BaseInventoryStorageComponent targetStorage = null;

		// Prefer an instance directly in the storage the clicked line belongs to.
		if (preferredStorage)
		{
			int total = preferredStorage.GetSlotsCount();
			for (int i = 0; i < total; i++)
			{
				IEntity item = preferredStorage.Get(i);
				if (item && GRAD_InventoryLib.GetPrefabResourceName(item) == prefab)
				{
					target = item;
					targetStorage = preferredStorage;
					break;
				}
			}
		}

		// Fallback: search the whole preview (covers nested/other containers if the preferred one no
		// longer has it, e.g. a stale line after an external change).
		if (!target)
		{
			array<IEntity> items = {};
			GRAD_InventoryLib.CollectAllItems(m_PreviewCharacter, items);
			for (int i = items.Count() - 1; i >= 0; i--)
			{
				IEntity item = items[i];
				if (item && GRAD_InventoryLib.GetPrefabResourceName(item) == prefab)
				{
					target = item;
					break;
				}
			}
		}

		if (!target)
		{
			GRAD_Log.Debug(string.Format("RemoveOne: no removable instance of '%1' found on preview", prefab));
			RefreshPreviewRender();
			return false;
		}

		bool removed = false;

		// Pass the storage explicitly (verified API: TryRemoveItemFromInventory(item, storage=null, cb=null))
		// so the manager doesn't have to guess/resolve which storage currently owns the item — omitting
		// it is what produced the "TryRemoveItemFromInventory failed" warning.
		if (manager.TryRemoveItemFromInventory(target, targetStorage))
		{
			SCR_EntityHelper.DeleteEntityAndChildren(target);
			removed = true;
		}
		else
		{
			GRAD_Log.Warn(string.Format("RemoveOne: TryRemoveItemFromInventory failed for '%1'", prefab));
		}

		RefreshPreviewRender();
		return removed;
	}

	//------------------------------------------------------------------------------------------------
	//! Create a labelled row button under parent from the row layout. Returns the row widget.
	protected Widget CreateRow(notnull Widget parent, string label)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return null;

		Widget row = workspace.CreateWidgets(ROW_LAYOUT, parent);
		if (!row)
			return null;

		SCR_ButtonTextComponent text = SCR_ButtonTextComponent.FindButtonTextComponent(row);
		if (text)
			text.SetText(label);

		return row;
	}

	//------------------------------------------------------------------------------------------------
	//! Set the caption of a named WLib text button inside a root.
	protected void SetButtonText(notnull Widget root, string buttonName, string caption)
	{
		Widget btn = root.FindAnyWidget(buttonName);
		if (!btn)
			return;

		SCR_ButtonTextComponent text = SCR_ButtonTextComponent.FindButtonTextComponent(btn);
		if (text)
			text.SetText(caption);
	}

	//------------------------------------------------------------------------------------------------
	//! Enable/disable a named button (dims + blocks clicks when disabled).
	protected void SetButtonEnabled(notnull Widget root, string buttonName, bool enabled)
	{
		Widget btn = root.FindAnyWidget(buttonName);
		if (!btn)
			return;

		// Dim a not-applicable button via opacity. (The ADD handlers already no-op when the target
		// container / selected record is invalid, so a click on a dim button is harmless.)
		float opacity = 0.35;
		if (enabled)
			opacity = 1.0;
		btn.SetOpacity(opacity);
	}

	//------------------------------------------------------------------------------------------------
	//! Refresh the right loadout summary panel: for Uniform / Vest / Backpack show a fill bar (by slot
	//! occupancy), a percent label, and the list of contained items. Slots the preview isn't wearing
	//! render as "[Empty]".
	protected void RefreshLoadoutPanel()
	{
		// Drop the previous frame's line handlers before rebuilding the contents lists, so a click on a
		// now-destroyed line can't fire a stale invoker (that crashes the menu).
		m_aLoadoutLineHandlers.Clear();

		RefreshLoadoutSlot(2048, WIDGET_SLOT_UNIFORM_BAR, WIDGET_SLOT_UNIFORM_PCT, WIDGET_SLOT_UNIFORM_CONTENTS);	// TORSO (uniform)
		RefreshLoadoutSlot(4096, WIDGET_SLOT_VEST_BAR, WIDGET_SLOT_VEST_PCT, WIDGET_SLOT_VEST_CONTENTS);			// VEST
		RefreshLoadoutSlot(128,  WIDGET_SLOT_BACKPACK_BAR, WIDGET_SLOT_BACKPACK_PCT, WIDGET_SLOT_BACKPACK_CONTENTS);	// BACKPACK
	}

	//------------------------------------------------------------------------------------------------
	//! Refresh one loadout-panel slot block from the container owned by the given arsenal-type bit.
	protected void RefreshLoadoutSlot(int arsenalTypeBit, string barName, string pctName, string contentsName)
	{
		Widget root = GetRootWidget();
		if (!root)
			return;

		BaseInventoryStorageComponent storage = FindNamedContainer(arsenalTypeBit);

		float frac = 0;
		if (storage)
			frac = GRAD_InventoryLib.GetStorageFillFraction(storage);

		// Fill bar: fade the bar toward empty as the fraction drops (opacity is a reliable Widget API;
		// a proportional-width bar would need a FrameSlot-anchored fill, a later visual refinement). The
		// numeric percent below is the precise readout.
		Widget bar = root.FindAnyWidget(barName);
		if (bar)
		{
			float op = 0.25 + 0.75 * frac;	// 0.25 (empty) .. 1.0 (full)
			bar.SetOpacity(op);
		}

		TextWidget pct = TextWidget.Cast(root.FindAnyWidget(pctName));
		if (pct)
		{
			if (storage)
				pct.SetText(string.Format("%1%%", Math.Round(frac * 100)));
			else
				pct.SetText("");
		}

		VerticalLayoutWidget contents = VerticalLayoutWidget.Cast(root.FindAnyWidget(contentsName));
		if (contents)
			FillSlotContents(contents, storage);
	}

	//------------------------------------------------------------------------------------------------
	//! List a container storage's direct items into a contents layout (simple text lines), GROUPED by
	//! prefab: several same-prefab item entities in the storage's direct slots (e.g. separate flare
	//! stacks that never merged into one entity) collapse into ONE line with a summed count, instead
	//! of one line per entity. "[Empty]" when the garment isn't worn or holds nothing.
	//!
	//! NOTE: this inventory model has no internal "quantity" on a stackable item entity — each unit is
	//! its own entity occupying its own slot (see GRAD_LoadoutEntry.m_iQuantity, which is captured/
	//! applied as a constant 1 everywhere). Grouping here is purely a DISPLAY concern; the underlying
	//! entities remain separate so [-] must still remove exactly one entity, not decrement a quantity.
	protected void FillSlotContents(notnull VerticalLayoutWidget contents, BaseInventoryStorageComponent storage)
	{
		ClearChildren(contents);

		if (!storage)
		{
			CreateEmptyLine(contents, "[Empty]");
			return;
		}

		// Collect this storage's DIRECT items only (matches the old behavior's scope — nested pouch
		// contents are not flattened in here) and group by prefab so repeats collapse to one line.
		array<IEntity> directItems = {};
		int total = storage.GetSlotsCount();
		for (int i = 0; i < total; i++)
		{
			IEntity item = storage.Get(i);
			if (item)
				directItems.Insert(item);
		}

		map<ResourceName, int> counts = new map<ResourceName, int>();
		GRAD_InventoryLib.CountPrefabInstances(directItems, counts);

		// One representative item entity per distinct prefab (the first one encountered) — enough to
		// resolve a display name and to anchor the line's handler; the count comes from the map.
		map<ResourceName, IEntity> representative = new map<ResourceName, IEntity>();
		foreach (IEntity item : directItems)
		{
			ResourceName prefab = GRAD_InventoryLib.GetPrefabResourceName(item);
			if (prefab == ResourceName.Empty)
				continue;
			if (!representative.Contains(prefab))
				representative.Set(prefab, item);
		}

		int shown = 0;
		foreach (ResourceName prefab, int count : counts)
		{
			IEntity repItem = null;
			representative.Find(prefab, repItem);
			if (!repItem)
				continue;

			CreateItemLine(contents, repItem, storage, count);
			shown++;
		}

		if (shown == 0)
			CreateEmptyLine(contents, "[Empty]");
	}

	//------------------------------------------------------------------------------------------------
	//! A plain (non-interactive) contents line, e.g. the "[Empty]" placeholder. No +/- buttons.
	protected void CreateEmptyLine(notnull Widget parent, string text)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		Widget line = ws.CreateWidget(WidgetType.TextWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 0, parent);
		TextWidget tw = TextWidget.Cast(line);
		if (tw)
		{
			tw.SetText(text);
			tw.SetExactFontSize(18);
			tw.SetColor(new Color(0.8, 0.85, 0.9, 1));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One interactive contents line: [-] [+] <item name>. The buttons decrement / increment the
	//! quantity of this item's prefab within the given container (preview only). A per-line handler is
	//! kept alive in m_aLoadoutLineHandlers so its invokers stay valid until the next panel rebuild.
	//! `count` is the number of same-prefab entities directly in `storage` (already grouped by the
	//! caller, FillSlotContents) — NOT the whole-preview total, so the line reflects what's actually in
	//! THIS container.
	protected void CreateItemLine(notnull Widget parent, notnull IEntity item, notnull BaseInventoryStorageComponent storage, int count)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		Widget line = ws.CreateWidgets(LINE_LAYOUT, parent);
		if (!line)
		{
			// Layout missing/unregistered: fall back to a plain label so contents still list.
			CreateEmptyLine(parent, GRAD_InventoryLib.GetEntityShortName(item));
			return;
		}

		TextWidget label = TextWidget.Cast(line.FindAnyWidget(WIDGET_LINE_LABEL));
		if (label)
		{
			label.SetText(GRAD_InventoryLib.GetEntityShortName(item));
			label.SetExactFontSize(18);
			label.SetColor(new Color(0.8, 0.85, 0.9, 1));
		}

		ResourceName prefab = GRAD_InventoryLib.GetPrefabResourceName(item);

		// Show the count of this prefab WITHIN THIS STORAGE (passed in by FillSlotContents, already
		// grouped) between the -/+ buttons, so the number matches what's actually in this container —
		// not CountOnPreview's whole-preview total, which would show the same total on every duplicate
		// line instead of this container's share.
		TextWidget countW = TextWidget.Cast(line.FindAnyWidget(WIDGET_LINE_COUNT));
		if (countW)
		{
			countW.SetText(count.ToString());
			countW.SetExactFontSize(18);
			countW.SetColor(new Color(1, 0.85, 0.4, 1));	// amber, stands out
		}

		GRAD_LoadoutLineHandler h = new GRAD_LoadoutLineHandler(this, prefab, storage);
		m_aLoadoutLineHandlers.Insert(h);	// keep alive across this refresh cycle

		Widget minus = line.FindAnyWidget(WIDGET_LINE_MINUS);
		if (minus)
		{
			SCR_InputButtonComponent btn = SCR_InputButtonComponent.FindComponent(minus);
			if (btn)
				btn.m_OnActivated.Insert(h.OnMinus);
		}

		Widget plus = line.FindAnyWidget(WIDGET_LINE_PLUS);
		if (plus)
		{
			SCR_InputButtonComponent btn = SCR_InputButtonComponent.FindComponent(plus);
			if (btn)
				btn.m_OnActivated.Insert(h.OnPlus);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! [-] on a loadout line: remove one instance of `prefab` from `storage` (the container this line
	//! belongs to), then refresh.
	void OnLoadoutLineMinus(ResourceName prefab, BaseInventoryStorageComponent storage)
	{
		RemoveOneFromPreview(prefab, storage);
		PopulateItems();
		RefreshSelectedPanel();
		RefreshLoadoutPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! [+] on a loadout line: add one more of `prefab` into the same container `storage`, then refresh.
	void OnLoadoutLinePlus(ResourceName prefab, BaseInventoryStorageComponent storage)
	{
		if (prefab == string.Empty || !m_PreviewCharacter)
			return;

		GRAD_LoadoutEntry entry = GRAD_LoadoutEntry.Create(prefab, -1, string.Empty, 1);
		GRAD_LoadoutData single = new GRAD_LoadoutData();
		single.m_Root.AddChild(entry);
		ApplyToPreview(single, storage);

		PopulateItems();
		RefreshSelectedPanel();
		RefreshLoadoutPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! Remove all children of a layout widget (and drop the row handlers tied to them).
	protected void ClearChildren(notnull Widget parent)
	{
		Widget child = parent.GetChildren();
		while (child)
		{
			Widget next = child.GetSibling();
			child.RemoveFromHierarchy();
			child = next;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! OK: serialize the preview character's loadout and apply it to the real target(s) via RPC.
	protected void OnConfirm()
	{
		if (!m_PreviewCharacter || !m_Context)
		{
			Close();
			return;
		}

		GRAD_LoadoutData result = GRAD_LoadoutCapture.Capture(m_PreviewCharacter, "ArsenalResult", true);
		if (!result)
		{
			GRAD_Log.Error("ArsenalMenu: failed to capture preview loadout on confirm");
			Close();
			return;
		}

		SCR_PlayerController mgr = SCR_PlayerController.GradGetLocal();
		if (!mgr)
		{
			GRAD_Log.Error("ArsenalMenu: no local controller to apply through");
			Close();
			return;
		}

		// Apply to every target (multi-target GM apply supported).
		array<IEntity> targets = m_Context.GetTargets();
		foreach (IEntity target : targets)
		{
			RplId rplId = SCR_PlayerController.GradGetEntityRplId(target);
			if (rplId.IsValid())
				mgr.GradApplyLoadout(rplId, result);
			else
				GRAD_Log.Warn(string.Format("ArsenalMenu: target %1 has no RplId; skipped", GRAD_InventoryLib.GetEntityShortName(target)));
		}

		// Remember name for the "load previous" quick action.
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (service)
			service.SetLastUsedLoadoutName(result.m_sName);

		Close();
	}

	//------------------------------------------------------------------------------------------------
	//! Cancel: discard the preview, change nothing on the real target.
	protected void OnCancel()
	{
		Close();
	}

	//------------------------------------------------------------------------------------------------
	//! Export: serialize the preview character's current loadout to JSON and push it to the clipboard.
	protected void OnExportClicked()
	{
		if (!m_PreviewCharacter)
		{
			GRAD_Log.Warn("ArsenalMenu: export requested with no preview character");
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutCapture.Capture(m_PreviewCharacter, "Export", true);
		if (!data)
		{
			GRAD_Log.Error("ArsenalMenu: failed to capture preview loadout on export");
			return;
		}

		string json = data.ToJsonString();
		System.ExportToClipboard(json);

		GRAD_Log.Info("ArsenalMenu: exported loadout to clipboard");
	}

	//------------------------------------------------------------------------------------------------
	//! Import: read a loadout from the clipboard and REPLACE the preview character's current loadout
	//! with it (clearFirst=true), then refresh every panel that reflects the preview's contents.
	protected void OnImportClicked()
	{
		if (!m_PreviewCharacter)
		{
			GRAD_Log.Warn("ArsenalMenu: import requested with no preview character");
			return;
		}

		string json = System.ImportFromClipboard();
		if (GRAD_CommonUtils.IsBlank(json))
		{
			GRAD_Log.Warn("ArsenalMenu: import failed — clipboard empty or no text");
			return;
		}

		GRAD_LoadoutData data = GRAD_LoadoutData.FromJsonString(json);
		if (!data)
		{
			GRAD_Log.Warn("ArsenalMenu: import failed — invalid or unsupported loadout on clipboard");
			return;
		}

		GenericEntity ge = GenericEntity.Cast(m_PreviewCharacter);
		if (ge)
			ge.Activate();

		array<IEntity> created = {};
		GRAD_LoadoutApply.Apply(m_PreviewCharacter, data, true, false, created, true);
		foreach (IEntity e : created)
			m_aPreviewCreated.Insert(e);

		RefreshPreviewRender();
		PopulateItems();
		RefreshSelectedPanel();
		RefreshLoadoutPanel();

		GRAD_Log.Info("ArsenalMenu: imported loadout from clipboard");
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		// Tear down the preview character + helpers. The networked target is never touched here.
		if (m_PreviewCameraHelper)
		{
			m_PreviewCameraHelper.Destroy();
			m_PreviewCameraHelper = null;
		}

		// Stop listening for a catalog build that may outlive this menu.
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (service && service.GetCatalogIndex())
			service.GetCatalogIndex().GetOnComplete().Remove(OnCatalogReady);

		GRAD_LoadoutApply.CleanupCreated(m_aPreviewCreated);

		// The clone is ours (SpawnLocal) — release the widget binding, then delete it.
		if (m_PreviewManager && m_wPreview)
			m_PreviewManager.SetPreviewItem(m_wPreview, null);

		if (m_PreviewCharacter)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_PreviewCharacter);
			m_PreviewCharacter = null;
		}
		m_PreviewAttribs = null;

		super.OnMenuClose();
		GRAD_Log.Info("ArsenalMenu: closed");
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		// Q/E tab cycling comes from SCR_TabViewComponent's built-in m_sActionLeft/m_sActionRight
		// paging (see GRAD_ArsenalMenu.layout's CategoryTabView) — no manual input polling here. A
		// hand-rolled per-frame GetActionTriggered() poll on an unregistered action name previously
		// crashed the engine natively; the vanilla component owns this safely.
		//
		// We still need to REACT to the tab change (repopulate the shared grid, reframe the preview).
		// PollTabChange() compares GetShownTab() against our cached index and reacts on change; this
		// call was missing (defined but never invoked), which is why the grid stayed frozen on tab 0.
		PollTabChange();

		// Drive preview orbit/zoom: feed the SAME persistent attribs the manager renders from, so the
		// helper's mouse-driven RotateItemCamera/ZoomCamera actually show. Re-push on change.
		//
		// SAFETY: re-Deactivate() the clone each frame to keep the character-lifetime system from
		// reaping it (a script-spawned character clone gets deleted after a timeout otherwise, and the
		// next SetPreviewItem on the freed entity is a native crash — observed ~20s after open). Also
		// re-fetch its inventory component as a liveness probe; if it's gone, the entity was reaped, so
		// null our handle and stop touching it rather than dereference freed memory.
		if (m_PreviewCharacter)
		{
			GenericEntity ge = GenericEntity.Cast(m_PreviewCharacter);
			if (!ge || !ge.FindComponent(SCR_InventoryStorageManagerComponent))
			{
				m_PreviewCharacter = null;	// reaped — drop the dangling handle
			}
			else
			{
				ge.Deactivate();	// keep it pinned alive this frame
			}
		}

		if (m_PreviewCameraHelper && m_PreviewAttribs && m_PreviewManager && m_wPreview && m_PreviewCharacter)
		{
			bool changed = m_PreviewCameraHelper.Update(tDelta, m_PreviewAttribs);
			if (changed)
				m_PreviewManager.SetPreviewItem(m_wPreview, m_PreviewCharacter, m_PreviewAttribs);
		}
	}
}

//------------------------------------------------------------------------------------------------
//! Describes what the arsenal menu operates on: one or more target characters. For players this
//! is their own character; for GMs it may be several selected units (multi-target apply).
class GRAD_ArsenalMenuContext
{
	protected ref array<IEntity> m_aTargets = {};

	//------------------------------------------------------------------------------------------------
	void AddTarget(IEntity target)
	{
		if (target)
			m_aTargets.Insert(target);
	}

	//------------------------------------------------------------------------------------------------
	array<IEntity> GetTargets()
	{
		return m_aTargets;
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetPrimaryTarget()
	{
		if (m_aTargets.IsEmpty())
			return null;

		return m_aTargets[0];
	}

	//------------------------------------------------------------------------------------------------
	bool HasTargets()
	{
		return !m_aTargets.IsEmpty();
	}
}

//------------------------------------------------------------------------------------------------
//! One clickable rail/list row. Bridges a row button's activation to the menu, carrying the
//! per-row context (which category, or which item) the menu needs to react.
class GRAD_ArsenalRowHandler
{
	protected GRAD_ArsenalMenu m_Menu;

	int m_iCategoryIndex = -1;			//!< meaningful when m_bIsCategory (tab index)
	ref GRAD_ArsenalItemRecord m_Record;	//!< meaningful for item cards
	bool m_bIsCategory;					//!< top-tab button
	bool m_bIsGroupHeader;				//!< collapsible base-name group header card
	string m_sGroupKey;					//!< group label this header toggles (when m_bIsGroupHeader)
	Widget m_wRow;						//!< the card/row widget itself, for selection-highlight recolor

	//------------------------------------------------------------------------------------------------
	//! Binds the widget's single SCR_InputButtonComponent to OnActivated.
	void GRAD_ArsenalRowHandler(GRAD_ArsenalMenu menu, notnull Widget rowWidget, bool bindSingleButton = true)
	{
		m_Menu = menu;
		m_wRow = rowWidget;

		if (bindSingleButton)
		{
			// FindComponent(w) only looks at components attached directly to w, not descendants. The card
			// layout's root is now the size-capping wrapper "TileSize" (GRAD_ItemCard.layout), with the
			// actual SCR_InputButtonComponent living one level down on its child "CardButton" — so this
			// must search by name, not assume rowWidget itself owns the component.
			SCR_InputButtonComponent button = SCR_InputButtonComponent.GetInputButtonComponent("CardButton", rowWidget);
			if (button)
				button.m_OnActivated.Insert(OnActivated);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnActivated()
	{
		if (!m_Menu)
			return;

		if (m_bIsGroupHeader)
			m_Menu.OnGroupHeaderClicked(m_sGroupKey);
		else if (m_bIsCategory)
			m_Menu.SelectCategoryByIndex(m_iCategoryIndex);
		else
			m_Menu.OnItemRowClicked(m_Record, m_wRow);
	}
}

//------------------------------------------------------------------------------------------------
//! One loadout-panel contents line's [-]/[+] bridge. Carries the line's prefab and target container
//! so the parameterless invoker callbacks can route back into the menu with the right context.
//! Kept alive in the menu's m_aLoadoutLineHandlers (cleared each RefreshLoadoutPanel).
class GRAD_LoadoutLineHandler
{
	protected GRAD_ArsenalMenu m_Menu;
	protected ResourceName m_sPrefab;
	protected BaseInventoryStorageComponent m_Storage;

	//------------------------------------------------------------------------------------------------
	void GRAD_LoadoutLineHandler(GRAD_ArsenalMenu menu, ResourceName prefab, BaseInventoryStorageComponent storage)
	{
		m_Menu = menu;
		m_sPrefab = prefab;
		m_Storage = storage;
	}

	//------------------------------------------------------------------------------------------------
	void OnMinus()
	{
		if (m_Menu)
			m_Menu.OnLoadoutLineMinus(m_sPrefab, m_Storage);
	}

	//------------------------------------------------------------------------------------------------
	void OnPlus()
	{
		if (m_Menu)
			m_Menu.OnLoadoutLinePlus(m_sPrefab, m_Storage);
	}
}

//------------------------------------------------------------------------------------------------
//! Maps an arsenal item-type value (a SCR_EArsenalItemType BITFLAG) to a display label for the
//! category rail.
//!
//! SCR_EArsenalItemType is a power-of-two bitflag enum. The bit -> meaning mapping below matches
//! the live engine enum (RIFLE = 1<<1 .. VEHICLE = 1<<22); the case values are the decimal bit
//! values (1<<1 = 2, 1<<2 = 4, ...). These names are easily edited; for full localization, replace
//! the returned strings with `#`-prefixed stringtable keys (see docs/WORKBENCH_TASKS.md).
class GRAD_ArsenalCategoryLabels
{
	//------------------------------------------------------------------------------------------------
	static string LabelFor(int arsenalType)
	{
		switch (arsenalType)
		{
			case 2:       return "Rifles";				// RIFLE = 1<<1
			case 4:       return "Pistols";				// PISTOL = 1<<2
			case 8:       return "Grenades";			// LETHAL_THROWABLE = 1<<3
			case 16:      return "Launchers";			// ROCKET_LAUNCHER = 1<<4
			case 32:      return "Machine Guns";		// MACHINE_GUN = 1<<5
			case 64:      return "Medical";				// HEAL = 1<<6
			case 128:     return "Backpacks";			// BACKPACK = 1<<7
			case 256:     return "Sniper Rifles";		// SNIPER_RIFLE = 1<<8
			case 512:     return "Smokes & Flares";		// NON_LETHAL_THROWABLE = 1<<9
			case 1024:    return "Headgear";			// HEADWEAR = 1<<10
			case 2048:    return "Jackets";				// TORSO = 1<<11
			case 4096:    return "Vests & Belts";		// VEST_AND_WAIST = 1<<12
			case 8192:    return "Trousers";			// LEGS = 1<<13
			case 16384:   return "Footwear";			// FOOTWEAR = 1<<14
			case 32768:   return "Radio Backpacks";		// RADIO_BACKPACK = 1<<15
			case 65536:   return "Equipment";			// EQUIPMENT = 1<<16
			case 131072:  return "Weapon Attachments";	// WEAPON_ATTACHMENT = 1<<17
			case 262144:  return "Explosives";			// EXPLOSIVES = 1<<18
			case 524288:  return "Gloves";				// HANDWEAR = 1<<19
			case 1048576: return "Mortars";				// MORTARS = 1<<20
			case 2097152: return "Helicopters";			// HELICOPTER = 1<<21
			case 4194304: return "Vehicles";			// VEHICLE = 1<<22
		}

		return string.Format("Type %1", arsenalType);
	}

	//------------------------------------------------------------------------------------------------
	//! Whether a category holds STACKABLE items (where carrying several makes sense — ammo, grenades,
	//! meds, explosives) versus single-slot items (one weapon per slot, one helmet, etc.). Stackable
	//! categories get a [-] N [+] quantity control; single-slot categories get plain click-to-equip.
	//!
	//! Bit values are SCR_EArsenalItemType (RIFLE=1<<1 .. VEHICLE=1<<22). Adjust the set here after a
	//! live check if a category feels wrong.
	static bool IsStackable(int arsenalType)
	{
		const int STACKABLE =
			  (1 << 3)    // LETHAL_THROWABLE (grenades)
			| (1 << 6)    // HEAL (medical)
			| (1 << 9)    // NON_LETHAL_THROWABLE (smokes/flares)
			| (1 << 16)   // EQUIPMENT (misc carryables)
			| (1 << 18);  // EXPLOSIVES (mines/charges)

		return (arsenalType & STACKABLE) != 0;
	}
}

//------------------------------------------------------------------------------------------------
//! The five top tabs of the redesigned arsenal, each mapping to a SET of SCR_EArsenalItemType bits
//! (see GRAD_ArsenalCategoryLabels for the bit meanings). Selecting a tab shows every record whose
//! m_iArsenalType is in the tab's mask.
//!
//!   Primary   = Rifles, MGs, Sniper, Launchers
//!   Secondary = Pistols
//!   Throwables= Grenades, Smokes/Flares, Explosives, Medical
//!   Apparel   = Headgear, Jackets, Vests, Trousers, Footwear, Gloves
//!   Container = Backpacks, Radio Backpacks
class GRAD_ArsenalTabs
{
	static const int PRIMARY   = (1<<1) | (1<<5) | (1<<8) | (1<<4);					// RIFLE|MG|SNIPER|LAUNCHER
	static const int SECONDARY = (1<<2);											// PISTOL
	static const int THROWABLES= (1<<3) | (1<<9) | (1<<18) | (1<<6);					// GRENADE|SMOKE|EXPLOSIVE|HEAL
	static const int APPAREL   = (1<<10)|(1<<11)|(1<<12)|(1<<13)|(1<<14)|(1<<19);	// HEAD|TORSO|VEST|LEGS|FOOT|HAND
	static const int CONTAINER = (1<<7) | (1<<15);									// BACKPACK|RADIO_BACKPACK

	//------------------------------------------------------------------------------------------------
	//! Number of tabs.
	static int Count()
	{
		return 5;
	}

	//------------------------------------------------------------------------------------------------
	//! Display label for a tab index (0..4).
	static string LabelFor(int tabIndex)
	{
		switch (tabIndex)
		{
			case 0: return "Primary";
			case 1: return "Secondary";
			case 2: return "Throwables";
			case 3: return "Apparel";
			case 4: return "Container";
		}
		return string.Format("Tab %1", tabIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! Arsenal-type bit mask for a tab index (0..4). 0 for out-of-range.
	static int MaskFor(int tabIndex)
	{
		switch (tabIndex)
		{
			case 0: return PRIMARY;
			case 1: return SECONDARY;
			case 2: return THROWABLES;
			case 3: return APPAREL;
			case 4: return CONTAINER;
		}
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether items in this tab are stackable (quantity control). Throwables tab is the stackable one.
	static bool IsStackableTab(int tabIndex)
	{
		return tabIndex == 2;
	}
}
