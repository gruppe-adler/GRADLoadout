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
//!   - the preview character (local-only clone of the target), rendered into a RenderTargetWidget by
//!     a real spawned camera entity we position ourselves (see the m_PreviewCharacter field comment
//!     for why this replaced the earlier ItemPreviewManagerEntity/ItemPreviewWidget approach),
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
	//
	// REPLACED (see git history / docs for the old approach): the preview pane used to be an
	// ItemPreviewWidget driven by ItemPreviewManagerEntity.SetPreviewItem(widget, entity,
	// PreviewRenderAttributes). That API is verified to expose EXACTLY 3 methods on
	// PreviewRenderAttributes (RotateItemCamera, ResetDeltaRotation, ZoomCamera-as-relative-FOV-
	// increment) and NO distance/pivot/offset control — the camera sits at a fixed distance from the
	// model origin with no lever to pull it back for a full-body shot. FOV was already maxed
	// (ZoomCamera(1000, 25, 120) deterministically clamps to 120) and still cropped tight. There is no
	// fix available within that API, so the preview pane is now a RenderTargetWidget (a DIFFERENT,
	// unrelated widget hierarchy — confirmed via API search, not a subclass of ItemPreviewWidget) fed
	// by a real, positionable camera ENTITY we spawn and place ourselves.
	// ROOT CAUSE FOUND (via vanilla source, arexplorer.zeroy.com — see reforger-api-gotchas memory): the
	// "int camera" parameter of RenderTargetWidget.SetWorld(world, camera) is a BASEWORLD CAMERA SLOT
	// INDEX, not a camera entity handle. Vanilla PIP sights (SCR_2DPIPSightsComponent) use slot 8 for
	// this exact "render a second view into a UI widget" purpose; slot 0 is the main game camera — which
	// is what CameraBase.GetCameraIndex() returned for our never-registered spawned camera (0, a
	// meaningless/unassigned default in this context, not our camera's own handle), producing a
	// transparent preview. No camera ENTITY is required at all: a slot is configured and moved directly
	// on BaseWorld (SetCameraType/SetCameraVerticalFOV/SetCameraNearPlane/SetCameraFarPlane/SetCameraEx),
	// exactly like vanilla's SCR_PIPCamera.ApplyProps()/UpdatePIPCamera() do every frame.
	protected IEntity m_PreviewCharacter;
	protected RenderTargetWidget m_wPreview;

	// Still used for the item-grid CARD thumbnails (CreateItemCardWidget's ItemPreviewWidget icons,
	// via SetPreviewItemFromPrefab) — that auto-framed path works fine for small item icons; it is only
	// the CENTER CHARACTER preview that moved to the RenderTargetWidget/camera approach above. Do not
	// remove this field, and do not route the card thumbnails through the new camera.
	protected ItemPreviewManagerEntity m_PreviewManager;

	// Current framing distance, in meters, applied to the preview camera SLOT every frame (see
	// PREVIEW_CAMERA_INDEX below). Set by FrameFullBody/FrameForCategory; re-applied each OnMenuUpdate
	// via PositionCamera so the slot's transform survives whatever else in the engine touches world
	// camera slots between frames (vanilla's own PIP camera re-applies every frame for the same reason).
	protected float m_fCameraDistance = 0.0;

	// The prefab the preview character was spawned from (kept for parity with the old field; not
	// currently re-read, but cheap to keep around for future re-resolve needs).
	protected ResourceName m_sPreviewPrefab;

	// Tunable: camera distance from the character's pivot for a full-body shot, in meters. Live-tested
	// at 3.0 and confirmed too far away/small (see PositionCamera's comment for the full diagnosis of
	// why — short version: 65 deg was the real culprit, not this). Tightened to 2.2m now that FOV is
	// fixed, so a full-body shot fills more of the frame without clipping the head/feet at typical
	// aspect ratios. FrameForCategory scales this down further for the Apparel tab's closer shot.
	protected const float PREVIEW_CAMERA_DISTANCE = 2.2;
	// Tunable: character eye height above the character's feet/pivot origin, in meters. Used for BOTH
	// the camera's height AND the look-at target's height (see PositionCamera) so the camera looks
	// roughly straight across at the character's face instead of up/down at a mismatched target — the
	// previous version used two different fractions of a "height" constant for camera vs. look-at,
	// which tilted the shot and pushed the character off-center. ~1.65m is a reasonable average-adult
	// eye height for a ~1.75-1.8m-tall character (Reforger's default human proportions); not exact per
	// character, but far closer than the old hip-height look-at target.
	protected const float PREVIEW_CAMERA_EYE_HEIGHT = 1.65;
	// Tunable: vertical FOV in degrees (CameraBase.SetFOVDegree — verified API via api_search: "Set
	// full symmetrical vertical FOV in degrees", i.e. this IS the full vertical FOV, not a half-angle
	// or horizontal/diagonal FOV). 65 degrees full vertical FOV is WIDE for a close character portrait
	// (think GoPro/wide-angle) — at close range a wide FOV makes the subject look smaller and farther
	// away than it is, and was the primary confirmed cause of the "tiny distant figure" symptom.
	// Narrowed to 40 degrees, a normal/portrait-lens full vertical FOV, so the character reads as
	// close and centered instead of shrunk in a wide field of view.
	protected const float PREVIEW_CAMERA_FOV = 40.0;

	// BaseWorld camera SLOT rendered into the preview RenderTargetWidget. Slot 0 is the main game
	// camera; vanilla PIP sights (SCR_2DPIPSightsComponent) use slot 8 for their scope/optics render.
	// 7 avoids both known-used slots without needing a full enumeration of every vanilla user.
	protected const int PREVIEW_CAMERA_INDEX = 7;

	// Entities created on the preview character by the last apply (for cleanup).
	protected ref array<IEntity> m_aPreviewCreated = {};

	// The category currently in focus (drives the item browser).
	protected int m_iSelectedCategory = -1;

	// DIAGNOSTIC: last GetShownTab() value logged by PollTabChange, so it only logs on change instead
	// of spamming every frame. Remove alongside the diagnostic log line once tab selection is fixed.
	protected int m_iLastLoggedShownTab = -999;

	// FAILED ATTEMPT (kept as a warning, not removed blindly): a time-based debounce was tried here
	// first, theorizing GetShownTab() would show two separate settled values in quick succession
	// that could be collapsed. Live log disproved this: GetShownTab() went 0 -> 2 in ONE clean poll
	// read with no intermediate value ever observed (confirmed via the exact log sequence: FrameFor
	// Category tab=0, then next poll already reads GetShownTab()=2). The double-fire happens INSIDE
	// vanilla SCR_TabViewComponent/SCR_PagingButtonComponent before our poll ever runs — there is
	// nothing to debounce against. See SetupCategoryRail's AddActionListener comment for the actual
	// fix (bypass the vanilla paging buttons for Q/E entirely).

	// True once SetupCategoryRail successfully registered OnTabLeftPressed/OnTabRightPressed on the
	// InputManager — gates the matching RemoveActionListener calls in OnMenuClose so cleanup can't
	// double-remove or remove listeners that were never added (e.g. if InputManager was null at setup).
	protected bool m_bTabActionListenersAdded = false;

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
	// UniformGridLayoutWidget does not auto-wrap, so we place each card at (cell % cols, cell / cols).
	// Was 2 — with fixed 220px cards and the grid pane now widened (SetupCategoryRail's FillWeight
	// call), 2 columns left roughly a third of the pane as dead space (confirmed live via screenshot).
	// 3 fits the wider pane without needing per-resolution measurement.
	protected const int GRID_COLUMNS = 3;
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
	//! Spawn a local preview character clone, configure the preview's BaseWorld camera SLOT, and bind
	//! the render-target preview widget to that slot.
	//!
	//! Character spawn/identity/loadout-mirroring is UNCHANGED (this part already worked correctly) —
	//! only the render plumbing changed. The auto-framed SetPreviewItemFromPrefab/
	//! ResolvePreviewEntityForPrefab path was already tried for the character preview in an earlier
	//! session and rendered blank (a bare character prefab has no inventory/clothing until items are
	//! applied, so that path likely resolves an effectively-invisible entity); do not revert to it here.
	//!
	//! Widget/camera binding: RenderTargetWidget.SetWorld(BaseWorld world, int camera) takes a BASEWORLD
	//! CAMERA SLOT INDEX, not a camera entity handle (confirmed via vanilla SCR_2DPIPSightsComponent
	//! source on arexplorer.zeroy.com — it configures/reads slot 8 directly on BaseWorld for its PIP
	//! sight render, no camera entity involved). No camera entity is spawned any more; the slot
	//! (PREVIEW_CAMERA_INDEX) is driven straight on `world` below and every frame by PositionCamera.
	protected void SetupPreview(notnull Widget root)
	{
		m_wPreview = RenderTargetWidget.Cast(root.FindAnyWidget(WIDGET_PREVIEW));
		if (!m_wPreview)
		{
			GRAD_Log.Warn("ArsenalMenu: preview widget not found in layout");
			return;
		}

		ChimeraWorld world = GetGame().GetWorld();

		// Still needed for the item-grid card thumbnails (CreateItemCardWidget), which keep using the
		// engine-managed auto-framed preview path — see m_PreviewManager's field comment.
		if (world)
			m_PreviewManager = world.GetItemPreviewManager();

		IEntity primary = m_Context.GetPrimaryTarget();
		m_sPreviewPrefab = GRAD_InventoryLib.GetPrefabResourceName(primary);

		if (m_sPreviewPrefab != ResourceName.Empty)
		{
			// Spawn position: GRAD_InventoryLib.SpawnLocal defaults to vector.Zero (world origin) when
			// no position is given — harmless for the OLD ItemPreviewWidget render path (an isolated
			// preview space, unaffected by the entity's real world position), but now that the preview
			// is a real BaseWorld camera slot looking at the ACTUAL world, world origin is often open
			// ocean/void on Reforger maps (confirmed live: camera showed water/horizon, not the
			// character).
			//
			// BUG FIX (was the root cause of the "underside of a wooden structure" report): a first
			// attempt spawned the clone 500m straight up. A freshly spawned ChimeraCharacter is NOT
			// physics-frozen — it free-falls under gravity/ragdoll for the ~10s it takes to reach the
			// ground, landing wherever it happens to (clipped into terrain/a building), and the camera
			// (which tracks the clone's LIVE transform every frame) showed whatever mid-fall/post-landing
			// mess resulted. There is no verified freeze/kinematic-disable API for this in the indexed
			// script API, so instead avoid falling entirely: spawn at the SAME ground level as the
			// primary target, offset sideways so it doesn't overlap the real player's own body/camera.
			vector spawnPos = vector.Zero;
			if (primary)
			{
				vector primaryTransform[4];
				primary.GetWorldTransform(primaryTransform);
				vector primaryRight = primaryTransform[0];
				primaryRight.Normalize();
				spawnPos = primaryTransform[3] + primaryRight * 5.0;
			}

			// Spawn a local clone — this is the render path that actually shows the character (the
			// from-prefab/resolve path rendered blank, see class-level comment above).
			m_PreviewCharacter = GRAD_InventoryLib.SpawnLocal(m_sPreviewPrefab, spawnPos);

			if (m_PreviewCharacter)
			{
				// Match the edited unit's face/appearance (a fresh spawn gets a random identity).
				CopyIdentity(primary, m_PreviewCharacter);

				// Mirror the target's current loadout so the player edits from their real starting kit.
				// force=false: keep the prefab's locked cosmetic body/clothing nodes; only mirror the
				// editable items on top.
				GRAD_LoadoutData current = GRAD_LoadoutCapture.Capture(primary, "PreviewBase", true);
				if (current)
					GRAD_LoadoutApply.Apply(m_PreviewCharacter, current, true, false, m_aPreviewCreated);

				PinPreviewAlive();

				if (world && m_wPreview)
				{
					world.SetCameraType(PREVIEW_CAMERA_INDEX, CameraType.PERSPECTIVE);
					world.SetCameraVerticalFOV(PREVIEW_CAMERA_INDEX, PREVIEW_CAMERA_FOV);
					world.SetCameraNearPlane(PREVIEW_CAMERA_INDEX, 0.05);
					world.SetCameraFarPlane(PREVIEW_CAMERA_INDEX, 500.0);

					FrameFullBody();

					m_wPreview.SetWorld(world, PREVIEW_CAMERA_INDEX);
					GRAD_Log.Info(string.Format("ArsenalMenu: preview bound to world camera slot %1", PREVIEW_CAMERA_INDEX));
				}
			}
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
	//! Refresh the preview render after a mutation. With a real camera + RenderTargetWidget there is no
	//! per-mutation re-bind needed (the widget keeps rendering the same world/camera live every frame,
	//! unlike the old ItemPreviewManager which needed an explicit SetPreviewItem push to pick up
	//! changes) — this now only re-pins the character against the lifetime reaper.
	protected void RefreshPreviewRender()
	{
		PinPreviewAlive();
	}

	//------------------------------------------------------------------------------------------------
	//! Frame the whole character: position the camera slot in front of the character, level with its
	//! eyes, looking straight back at its face (PREVIEW_CAMERA_DISTANCE/PREVIEW_CAMERA_EYE_HEIGHT,
	//! tunable constants declared with the fields above). Replaces the old PreviewRenderAttributes
	//! pitch+FOV hack — the camera slot can just be placed in space like anything else instead of
	//! fighting a fixed-distance, no-pivot render API.
	protected void FrameFullBody()
	{
		m_fCameraDistance = PREVIEW_CAMERA_DISTANCE;
		PositionCamera(m_fCameraDistance, PREVIEW_CAMERA_EYE_HEIGHT);
	}

	//------------------------------------------------------------------------------------------------
	//! Approximate a body-region focus for the given tab by moving the camera closer, matching the
	//! intent of the old pitch/FOV hack but via camera transform. tabIndex: 0 Primary, 1 Secondary,
	//! 2 Throwables, 3 Apparel, 4 Container. Apparel gets a closer shot; the rest use the same
	//! full-body eye-level framing as FrameFullBody. Tune the constants live.
	protected void FrameForCategory(int tabIndex)
	{
		if (tabIndex == 3)
		{
			// Apparel: closer shot for a clothing-focused view. Still eye-level (not scaled down like
			// the old height*0.7) — PositionCamera aims the look-at target at the SAME eye height as
			// the camera regardless of distance, so closing the distance alone tightens the framing
			// without re-introducing the camera/look-at height mismatch that caused the original bug.
			m_fCameraDistance = PREVIEW_CAMERA_DISTANCE * 0.6;
		}
		else
		{
			m_fCameraDistance = PREVIEW_CAMERA_DISTANCE;
		}

		PositionCamera(m_fCameraDistance, PREVIEW_CAMERA_EYE_HEIGHT);

		GRAD_Log.Info(string.Format("FrameForCategory: tab=%1", tabIndex));
	}

	//------------------------------------------------------------------------------------------------
	//! Place the preview camera SLOT `distance` meters in front of the preview character (along the
	//! character's forward axis, so it faces the character head-on) and `eyeHeight` meters above the
	//! character's feet/origin, then aim it back at that SAME height on the character (not a
	//! different, lower height) so the shot looks straight across at the face instead of down at the
	//! hips. No-op if the character or world is missing.
	//!
	//! BUG FIX (was the root cause of the "tiny distant figure in a field" report): the previous
	//! version lifted the camera to `height` (1.3m) but aimed the look-at target at only `height * 0.6`
	//! (0.78m) — camera at roughly chest height staring down at hip height, tilting the shot off the
	//! character's centerline. Combined with PREVIEW_CAMERA_FOV being a much wider lens than intended
	//! (see that constant's comment — SetFOVDegree is a FULL vertical FOV, and 65 deg full is a wide/
	//! GoPro-like lens, not a portrait lens), the character ended up small and low in a wide, tilted
	//! frame. Both are fixed now: single eye-height constant for camera AND look-at, plus a much
	//! narrower 40 deg FOV.
	//!
	//! Called every frame from OnMenuUpdate (not just on tab-change) because the slot's transform is
	//! NOT persistent state we own — vanilla's own PIP camera re-applies its transform every frame
	//! (SCR_PIPCamera.UpdatePIPCamera -> ApplyTransform) for the same reason: nothing guarantees a
	//! world camera slot's matrix survives untouched between frames.
	protected void PositionCamera(float distance, float eyeHeight)
	{
		if (!m_PreviewCharacter)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		vector charTransform[4];
		m_PreviewCharacter.GetWorldTransform(charTransform);
		vector charPos = charTransform[3];
		vector charForward = charTransform[2];

		// Defensive normalize: charForward is the transform's Z-basis column, which is only guaranteed
		// unit-length if the entity has no non-uniform/non-1.0 scale baked into its world transform. No
		// scale is explicitly set anywhere on the preview clone (GRAD_InventoryLib.SpawnLocal spawns
		// from an identity transform), so this should already be a unit vector in practice — but
		// normalizing here is cheap insurance against `distance` silently becoming
		// distance*|charForward| (e.g. a 3m intended offset quietly turning into much more) if that
		// ever changes.
		charForward.Normalize();

		// Stand the camera out along the character's forward axis so it looks at the character's front
		// (not its back), then lift it to eye height.
		vector camPos = charPos - charForward * distance;
		camPos[1] = camPos[1] + eyeHeight;

		// Look-at target uses the SAME eyeHeight as the camera (see method comment above for why the
		// old *0.6 mismatch was a bug) so the shot is level, centered on the face, not angled downward.
		vector lookTarget = charPos;
		lookTarget[1] = charPos[1] + eyeHeight;

		// World up axis as a literal (Y-up, matching this engine's convention — e.g. charTransform[1]/
		// camPos[1] above are the height/Y component). Cross-checked against the wiki's New Terrain Setup
		// page (light source "Angle Y = yaw" / "Angle X = pitch", i.e. yaw rotates about Y) and the FBX
		// Import page ("align assets pointing along Z+ axis in Enfusion") — both confirm Y-up, Z-forward,
		// so vector index 1 genuinely means height here; there is no XZY component-order swap in play.
		// "vector.Up" is not a confirmed API member, so a literal is used instead of guessing at a
		// constant that might not exist.
		vector worldUp = "0 1 0";

		// BUG FIX (root cause of the "straight-down grass shot" regression): this used to hand-build the
		// matrix via Math3D.DirectionAndUpMatrix(toTarget, worldUp, camTransform), which only documents
		// itself as "creates rotation matrix from direction and up vector" — it does NOT document which
		// output column (mat[0]/right, mat[1]/up, mat[2]/forward) the input `dir` actually lands in, nor
		// whether a camera's look axis is +mat[2] or -mat[2]. Guessing that convention wrong is exactly
		// what produced the straight-down shot (a 90-degrees-off basis reinterpreted as "forward" tips the
		// whole camera to point along what was actually the up/right axis).
		// VERIFIED via api_search: SCR_Math3D (Arma Reforger script API, distinct from the raw Enfusion
		// Math3D used elsewhere in this file for MatrixIdentity4) exposes
		//   static void LookAt(vector source, vector destination, vector up, out vector rotMat[4])
		//   -- "Returns a rotation matrix that makes object positioned at source position face the point
		//   at destination."
		// This is a purpose-built look-at helper that sidesteps the column/sign ambiguity entirely: it is
		// documented in terms of "object at source faces destination," which is precisely this camera's
		// requirement, instead of a generic direction/up basis whose forward-column convention is
		// unstated. (Also cross-confirmed the engine's matrix column layout separately via
		// SCR_Math3D.IsMatrixIdentity's doc string, "(right, up, forward, zero vectors)" — column 2 is
		// forward, matching charForward = charTransform[2] used above, so that part was already correct.)
		vector camTransform[4];
		SCR_Math3D.LookAt(camPos, lookTarget, worldUp, camTransform);

		world.SetCameraEx(PREVIEW_CAMERA_INDEX, camTransform);
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
		// ROOT CAUSE (found via vanilla source, arexplorer.zeroy.com — see reforger-api-gotchas memory):
		// CategoryTabView IMPORTS vanilla WLib_TabViewCoreMenus.layout (which already carries its own
		// SCR_TabViewComponent) and our layout's `components { SCR_TabViewComponent "{...}" {...} }`
		// block declares a DIFFERENT instance GUID — that ADDS a second component instead of overriding
		// the inherited one. Widget.FindHandler(type) returns only the FIRST handler of that type, which
		// is the inherited (zombie) instance: it never receives m_aElements (that's only set on OUR
		// instance's data block), so its ShowTab() validation `i >= m_aElements.Count()` always fails and
		// GetShownTab() (== m_iSelectedTab) is stuck at -1 forever — exactly the frozen-tracking symptom.
		// Meanwhile BOTH instances subscribe OnTabLeft/OnTabRight to the SAME shared PagingLeft/PagingRight
		// buttons (resolved by widget name in Init()), so any instance with real tabs advances itself on
		// every Q/E press — a second live instance is the double-advance culprit.
		// Fix: enumerate every handler via Widget.GetNumHandlers()/GetHandler(int) (both verified: "return
		// number of all handlers attached to widget" / indexed accessor), keep the first instance whose
		// GetShownTab() is ever >= 0 (only an instance that successfully completed a ShowTab() can be),
		// and mute action listening on any OTHER live instance so it stops double-firing on Q/E.
		Widget tabViewWidget = root.FindAnyWidget(WIDGET_CATEGORY_TABVIEW);
		m_TabView = null;
		if (tabViewWidget)
		{
			int numHandlers = tabViewWidget.GetNumHandlers();
			for (int hi = 0; hi < numHandlers; hi++)
			{
				SCR_TabViewComponent tv = SCR_TabViewComponent.Cast(tabViewWidget.GetHandler(hi));
				if (!tv)
					continue;

				int shownTab = tv.GetShownTab();
				GRAD_Log.Info(string.Format("ArsenalMenu: TabView handler #%1 GetShownTab()=%2", hi, shownTab));

				if (shownTab >= 0)
				{
					if (!m_TabView)
					{
						m_TabView = tv;
					}
					else
					{
						tv.SetListenToActions(false);
						GRAD_Log.Info(string.Format("ArsenalMenu: muted extra live TabView handler #%1 (double-advance culprit)", hi));
					}
				}
			}

			// All handlers read -1: fall back to the old resolution rather than leaving m_TabView null.
			if (!m_TabView)
				m_TabView = SCR_TabViewComponent.Cast(tabViewWidget.FindHandler(SCR_TabViewComponent));

			// DIAGNOSTIC: settles whether the duplicate instance also duplicated its 5 tab BUTTONS (10
			// total, clipped to look like 5) vs. just existing as a dataless zombie (still 5 buttons).
			// Remove once the tab-selection bug is fully confirmed fixed live.
			Widget tabsContainer = tabViewWidget.FindAnyWidget("Tabs");
			if (tabsContainer)
			{
				int buttonCount = 0;
				Widget tabChild = tabsContainer.GetChildren();
				while (tabChild)
				{
					buttonCount++;
					tabChild = tabChild.GetSibling();
				}
				GRAD_Log.Info(string.Format("ArsenalMenu: 'Tabs' container holds %1 tab buttons (5 expected)", buttonCount));
			}
		}

		if (!m_TabView)
			GRAD_Log.Warn("ArsenalMenu: CategoryTabView / SCR_TabViewComponent not found");

		// Q/E DOUBLE-ADVANCE FIX (root-caused via vanilla source, arexplorer.zeroy.com):
		// SCR_PagingButtonComponent.SetAction() registers BOTH an InputManager.AddActionListener
		// (EActionTrigger.DOWN -> OnMenuSelect) AND relies on the button's own OnClick, and BOTH call
		// the identical m_OnActivated.Invoke(...) -> SCR_TabViewComponent.OnTabRight()/OnTabLeft() ->
		// ShowTab(GetNextValidItem(...)). One E/Q press can fire both, synchronously, before our poll
		// ever runs — confirmed live: GetShownTab() went 0 -> 2 with no intermediate value ever
		// observed (a time-based debounce was tried first and removed; there was nothing to debounce,
		// see PollTabChange's comment). This is vanilla script we don't own and SetListenToActions only
		// gates the component's OWN listener (one of the two firing paths, not the button's OnClick),
		// so it cannot fix it either.
		//
		// Fix: stop relying on the vanilla paging buttons for Q/E entirely. Silence the component's own
		// listener (removes one of the two firing paths) and register OUR OWN single DOWN-triggered
		// listener directly on the same already-proven-valid action names (MenuTabLeft/MenuTabRight —
		// these are NOT unregistered/guessed names; they already drive m_sActionLeft/m_sActionRight on
		// this exact component, confirmed working), calling ShowTab(GetNextValidItem(...)) ourselves
		// exactly once per press. InputManager.AddActionListener(name, EActionTrigger.DOWN, callback)
		// is the verified, real, edge-triggered API (confirmed identical usage in vanilla
		// SCR_PagingButtonComponent.SetAction's own source) — NOT a per-frame GetActionTriggered() poll
		// (the pattern that previously crashed the engine on an UNREGISTERED action name; these two
		// names are proven registered/valid here, so that risk does not apply).
		if (m_TabView)
		{
			m_TabView.SetListenToActions(false);

			InputManager inputMgr = GetGame().GetInputManager();
			if (inputMgr)
			{
				inputMgr.AddActionListener("MenuTabLeft", EActionTrigger.DOWN, OnTabLeftPressed);
				inputMgr.AddActionListener("MenuTabRight", EActionTrigger.DOWN, OnTabRightPressed);
				m_bTabActionListenersAdded = true;
			}
		}

		// Tab-change reaction is via PollTabChange() in OnMenuUpdate, not GetOnChanged() — see
		// PollTabChange's comment for why.

		m_wItemList = root.FindAnyWidget(WIDGET_ITEM_GRID);
		if (!m_wItemList)
			GRAD_Log.Warn(string.Format("ArsenalMenu: item grid widget '%1' not found in layout", WIDGET_ITEM_GRID));

		// The three top-level panes (PaneLeftCategories/PaneCenterPreview/PaneRightContainer) are all
		// plain SizeMode Fill in the layout, so they split available width EVENLY (1/3 each). With
		// GRID_COLUMNS=2 fixed-220px cards, an even 1/3 share left roughly a third of the grid pane as
		// dead space (confirmed live via screenshot). Give the grid pane more of the width at runtime
		// via the verified LayoutSlot.SetFillWeight(widget, weight) static (distinct from the
		// GridLayoutWidget row/column fill-weight trap documented elsewhere in this file — this is a
		// plain proportional-share API for HorizontalLayoutWidget children) rather than guess at an
		// unverified static ".layout" property token.
		Widget paneCategories = root.FindAnyWidget("PaneLeftCategories");
		Widget panePreview = root.FindAnyWidget(WIDGET_PREVIEW);
		Widget paneRight = root.FindAnyWidget("PaneRightContainer");
		if (paneCategories)
			LayoutSlot.SetFillWeight(paneCategories, 1.6);
		if (panePreview)
			LayoutSlot.SetFillWeight(panePreview, 1.0);
		if (paneRight)
			LayoutSlot.SetFillWeight(paneRight, 1.0);

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

		// RebuildBrowser() also runs from OnCatalogReady() — the catalog index builds amortized over
		// frames and can finish AFTER the menu has opened and the user has already switched tabs. This
		// method replaces m_Browser with a brand-new GRAD_ItemBrowser (fresh m_iCategoryMask = 0, "off"),
		// so unconditionally forcing tab 0 here silently desynced the grid's filter from the tab strip's
		// visual highlight: the user would click/page to e.g. Apparel (SCR_TabViewComponent's own
		// highlight updates correctly, it owns that independent of our code), then a late catalog-ready
		// callback would reset the BROWSER's mask back to Primary while the highlight stayed on Apparel
		// — exactly the "Apparel selected but Primary items shown" bug reported live. Re-apply whatever
		// tab was already selected (m_iSelectedCategory, default -1 only on first-ever build) instead of
		// always resetting to 0, so a late rebuild can't silently desync from what's visually shown.
		int tabToShow = m_iSelectedCategory;
		if (tabToShow < 0 || tabToShow >= GRAD_ArsenalTabs.Count())
			tabToShow = 0;

		if (m_TabView)
			m_TabView.ShowTab(tabToShow);
		SelectCategoryByIndex(tabToShow);
		RefreshLoadoutPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! Poll the TabView's shown-tab index once per frame (called from OnMenuUpdate) and react on
	//! change. Avoids binding SCR_TabViewComponent.GetOnChanged() directly — its callback prototype
	//! (ScriptInvokerTabViewIndexMethod) rejects both a 1-arg and a 0-arg handler at compile time, and
	//! its exact required signature isn't in the indexed API docs. A cheap int-compare poll sidesteps
	//! the whole guessing game while still reacting the same frame a click or Q/E paging changes tabs.
	protected void PollTabChange(float tDelta)
	{
		if (!m_TabView)
			return;

		int shown = m_TabView.GetShownTab();

		// DIAGNOSTIC: log every raw read that differs from the last logged value, regardless of whether
		// it's valid, so we can see the actual GetShownTab() sequence during a tab switch (does it ever
		// leave -1? does it reach the clicked index at all?). Remove once the tab-selection bug is fully
		// root-caused.
		if (shown != m_iLastLoggedShownTab)
		{
			GRAD_Log.Info(string.Format("PollTabChange: raw GetShownTab()=%1 (m_iSelectedCategory=%2)", shown, m_iSelectedCategory));
			m_iLastLoggedShownTab = shown;
		}

		// GetShownTab() can transiently report -1 (no tab "settled" yet, e.g. mid-transition on a fast
		// Q/E press). Reacting to that wiped the category mask via MaskFor(-1) -> 0 -> "mask mode off",
		// which combined with m_iCategoryType also defaulting to -1 ("all categories") made every tab
		// show every item — confirmed live via the log line "SelectTab -1 mask=0". Only ever select a
		// genuinely valid tab index; ignore -1 (or any other out-of-range read) and wait for the next
		// poll to catch the real settled index instead.
		if (shown < 0 || shown >= GRAD_ArsenalTabs.Count())
			return;

		// The Q/E double-advance is now prevented at the SOURCE (see SetupCategoryRail's
		// AddActionListener comment — our own single DOWN-triggered listener drives ShowTab directly,
		// bypassing the vanilla paging buttons entirely), so GetShownTab() only ever reflects one step
		// per press by the time we poll it. No debounce needed here any more (a debounce was tried and
		// removed — see the m_iLastLoggedShownTab field comment for why it couldn't have worked: there
		// was never an intermediate value to catch, only the already-doubled final one).
		if (shown != m_iSelectedCategory)
			SelectCategoryByIndex(shown);
	}

	//------------------------------------------------------------------------------------------------
	//! Our own single, edge-triggered (EActionTrigger.DOWN) handlers for MenuTabLeft/MenuTabRight,
	//! registered in SetupCategoryRail to bypass the vanilla paging buttons' double-fire (see that
	//! method's comment). Each press calls ShowTab exactly once via GetNextValidItem — both verified
	//! real methods already used elsewhere in this file's investigation of SCR_TabViewComponent.
	protected void OnTabLeftPressed()
	{
		if (!m_TabView)
			return;

		int next = m_TabView.GetNextValidItem(true);
		if (next >= 0)
			m_TabView.ShowTab(next);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTabRightPressed()
	{
		if (!m_TabView)
			return;

		int next = m_TabView.GetNextValidItem(false);
		if (next >= 0)
			m_TabView.ShowTab(next);
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

		// CategoryItems switched from GridLayoutWidget to UniformGridLayoutWidget (diagnostic logging
		// proved GridLayoutWidget's fill-weight model was the actual bug: SetRowFillWeight divides the
		// grid's total HEIGHT evenly across every weighted row, with no scrolling/growth — measured live
		// at 66 rows in a ~1086px-tall grid, each row got ~16px versus the tile's fixed 180px, squashing
		// every card into a sliver). UniformGridLayoutWidget exposes no fill-weight API at all — cells
		// size from content, not a forced division — and CategoryItems is now wrapped in a
		// ScrollLayoutWidget (CategoryItemsScroll) so the grid can grow past the pane's fixed height with
		// the scroll container handling overflow.
		//
		// BUG FIX ("items only appear on scroll"): populating via SetColumn/SetRow does not itself
		// trigger a relayout — the grid/scroll container kept its stale (often zero-height at first
		// populate) bounds until some OTHER event forced a relayout, e.g. the user scrolling. Widget.
		// Update() ("proto external void Update()", confirmed on the base Widget/ScrollLayoutWidget
		// interface, not something added for this fix) forces that recompute immediately after the grid
		// is filled, so the cards are visible without needing any user interaction first.
		m_wItemList.Update();
		if (m_wItemList.GetParent())
			m_wItemList.GetParent().Update();	// the ScrollLayoutWidget wrapper's own content-size cache
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

		// No single prefab represents a multi-variant group header, but showing SOMETHING beats a blank
		// tile — use the first variant's prefab as a representative preview.
		ResourceName previewPrefab = ResourceName.Empty;
		if (group.m_aItems.Count() > 0 && group.m_aItems[0])
			previewPrefab = group.m_aItems[0].m_sPrefab;

		Widget card = CreateItemCardWidget(marker + group.m_sLabel, string.Format("%1", group.GetCount()), "", previewPrefab);
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

		Widget card = CreateItemCardWidget(name, countText, "", rec.m_sPrefab);
		if (!card)
			return;

		GRAD_ArsenalRowHandler handler = new GRAD_ArsenalRowHandler(this, card);
		handler.m_Record = rec;
		handler.m_bIsCategory = false;
		m_aItemRowHandlers.Insert(handler);
	}

	//! Build an icon tile card from GRAD_ItemCard.layout and place it in the next grid cell (wrapping
	//! into GRID_COLUMNS columns). Fills the icon (a live 3D preview of `prefab`, empty for group
	//! headers), name, and count child widgets.
	protected Widget CreateItemCardWidget(string name, string count, string weight, ResourceName prefab)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return null;

		Widget card = workspace.CreateWidgets(CARD_LAYOUT, m_wItemList);
		if (!card)
			return null;

		// Wrap into a grid: place at (cell % cols, cell / cols), then advance. CategoryItems is a
		// UniformGridLayoutWidget (see PopulateItems' comment for why), so its slot type is
		// UniformGridSlot, not GridSlot.
		UniformGridSlot.SetColumn(card, m_iGridCell % GRID_COLUMNS);
		UniformGridSlot.SetRow(card, m_iGridCell / GRID_COLUMNS);
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

		// Real 3D model thumbnail (not a flat SCR_UIInfo icon) — every catalog item has a resolvable
		// prefab, so this renders correctly regardless of whether the item happens to have hand-authored
		// icon art. Same manager instance already driving the center character preview; a single call at
		// creation is enough (unlike the character preview, cards don't need per-frame updates).
		ItemPreviewWidget iconW = ItemPreviewWidget.Cast(card.FindAnyWidget(WIDGET_CARD_ICON));
		if (iconW && prefab != ResourceName.Empty && m_PreviewManager)
			m_PreviewManager.SetPreviewItemFromPrefab(iconW, prefab, null, false);

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
	//! ADD TO BACKPACK button: add the selected item into the backpack storage. No-op if no backpack
	//! is worn (the button is dimmed/disabled in that case via SetAddButtonsEnabled — this guard is
	//! only reached on a stray click, or if the preview's mirrored loadout came back empty, e.g. from
	//! the character-falling bug fixed in SetupPreview's spawn-position comment).
	void OnAddToBackpack()
	{
		BaseInventoryStorageComponent bag = FindNamedContainer(128);	// BACKPACK
		if (bag)
		{
			AddSelectedToContainer(bag);
		}
		else
		{
			GRAD_Log.Warn("ArsenalMenu: ADD TO BACKPACK clicked but preview has no backpack container " +
				"(FindNamedContainer(128) found none) — check the loadout panel actually mirrored the " +
				"target's real backpack.");
		}
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
		// Tear down the preview character. The networked target is never touched here. There is no
		// camera ENTITY any more (PREVIEW_CAMERA_INDEX is a BaseWorld camera slot, not an owned
		// entity — see SetupPreview's comment) so there is nothing to delete for it; the slot simply
		// stops being read once m_wPreview is destroyed with the rest of this menu's widget tree.

		// Stop listening for a catalog build that may outlive this menu.
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (service && service.GetCatalogIndex())
			service.GetCatalogIndex().GetOnComplete().Remove(OnCatalogReady);

		// Un-register our own Q/E listeners (see SetupCategoryRail's AddActionListener comment) — an
		// InputManager-level listener outlives this menu instance otherwise, and would keep calling
		// OnTabLeftPressed/OnTabRightPressed (touching a by-then-destroyed m_TabView) after close.
		if (m_bTabActionListenersAdded)
		{
			InputManager inputMgr = GetGame().GetInputManager();
			if (inputMgr)
			{
				inputMgr.RemoveActionListener("MenuTabLeft", EActionTrigger.DOWN, OnTabLeftPressed);
				inputMgr.RemoveActionListener("MenuTabRight", EActionTrigger.DOWN, OnTabRightPressed);
			}
			m_bTabActionListenersAdded = false;
		}

		GRAD_LoadoutApply.CleanupCreated(m_aPreviewCreated);

		// The clone is ours (SpawnLocal) — delete it directly (no widget-binding release call needed;
		// unlike the old ItemPreviewManagerEntity path, RenderTargetWidget doesn't own the character).
		if (m_PreviewCharacter)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_PreviewCharacter);
			m_PreviewCharacter = null;
		}

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
		PollTabChange(tDelta);

		// SAFETY: re-Deactivate() the clone each frame to keep the character-lifetime system from
		// reaping it (a script-spawned character clone gets deleted after a timeout otherwise). Also
		// re-fetch its inventory component as a liveness probe; if it's gone, the entity was reaped, so
		// null our handle and stop touching it rather than dereference freed memory. Unlike the old
		// ItemPreviewManager path, there is no per-frame SetPreviewItem push to skip if reaped — the
		// RenderTargetWidget just keeps rendering whatever the camera currently sees, so a reaped
		// character simply stops appearing in frame rather than crashing on the next render call.
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

		// Re-apply the camera SLOT's transform every frame — it is not persistent state we own (see
		// PositionCamera's comment: vanilla's own PIP camera re-applies every frame for the same
		// reason). m_fCameraDistance is set by FrameFullBody/FrameForCategory and stays 0 until the
		// preview is set up, so this is a no-op before SetupPreview ever runs.
		if (m_fCameraDistance > 0.0)
			PositionCamera(m_fCameraDistance, PREVIEW_CAMERA_EYE_HEIGHT);

		// No per-frame mouse-orbit/zoom handling anymore (SCR_InventoryCharacterWidgetHelper was tied to
		// ItemPreviewWidget/PreviewRenderAttributes, which no longer apply here). The camera is static
		// once FrameFullBody/FrameForCategory places it; live re-orbiting via mouse drag would need a
		// new input handler driving PositionCamera(), not implemented in this pass.
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
