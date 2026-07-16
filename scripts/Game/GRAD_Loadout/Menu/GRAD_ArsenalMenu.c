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
//!   - the preview character (local-only clone of the target) spawned into a dedicated ISOLATED
//!     BaseWorld and rendered into a RenderTargetWidget via that world's own camera slot (written
//!     directly with BaseWorld.SetCameraEx — no camera entity involved; see the
//!     m_PreviewSharedItemRef field comment for the full history of why the previous
//!     camera-entity-in-the-live-world approach was abandoned),
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
	protected const string WIDGET_SUBCATEGORY_ROW	= "SubCategoryRow";	// second-tier filter pills within a top tab
	protected const string WIDGET_FACTION_ROW		= "FactionRow";		// top-of-screen faction filter pills
	protected const string WIDGET_SEARCH_BOX		= "SearchBox";		// live text filter
	protected const string WIDGET_BTN_LIST_VIEW		= "ButtonListView";	// toggles CategoryItems <-> CategoryList
	protected const string WIDGET_ITEM_LIST			= "CategoryList";	// list-view container, sibling of CategoryItems
	protected const string WIDGET_ITEM_LIST_SCROLL	= "CategoryListScroll";
	protected const string WIDGET_ITEM_GRID_SCROLL	= "CategoryItemsScroll";
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

	// Trousers (LEGS, 8192): NOT a container — no contents list, just a worn/not-worn indicator and a
	// REMOVE button, added 2026-07-15 per user request ("trousers should be removable too, same as
	// headgear, even if they are not containers"). WIDGET_SLOT_TROUSERS_PCT doubles as the worn/empty
	// text readout (no fill-fraction concept applies, unlike the container slots above).
	protected const string WIDGET_SLOT_TROUSERS_BAR		= "TrousersBar";
	protected const string WIDGET_SLOT_TROUSERS_PCT		= "TrousersPercent";
	protected const string WIDGET_BTN_REMOVE_TROUSERS	= "ButtonRemoveTrousers";

	// Headgear (HEADWEAR, 1024): same non-container shape as Trousers — no contents list, just a
	// worn/not-worn indicator and a REMOVE button, added 2026-07-16 per user request.
	protected const string WIDGET_SLOT_HEADGEAR_BAR		= "HeadgearBar";
	protected const string WIDGET_SLOT_HEADGEAR_PCT		= "HeadgearPercent";
	protected const string WIDGET_BTN_REMOVE_HEADGEAR	= "ButtonRemoveHeadgear";

	// Per-slot "REMOVE" buttons (unequip the garment itself, distinct from the [-]/[+] lines that edit
	// its CONTENTS) — added 2026-07-14 alongside their layout widgets.
	protected const string WIDGET_BTN_REMOVE_UNIFORM	= "ButtonRemoveUniform";
	protected const string WIDGET_BTN_REMOVE_VEST		= "ButtonRemoveVest";
	protected const string WIDGET_BTN_REMOVE_BACKPACK	= "ButtonRemoveBackpack";

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
	// this exact "render a second view into a UI widget" purpose; slot 0 is the main game camera.
	//
	// ABANDONED APPROACH (2026-07-14, kept as a warning — do NOT re-try any of this): a SCR_PIPCamera
	// entity spawned into the LIVE game world, driving world camera SLOT 7, pushed per frame via
	// UpdatePIPCamera(tDelta) (== CameraBase.ApplyTransform). Multiple sessions were burned on it. Every
	// finding below is LIVE-CONFIRMED, not theory:
	//   - RenderTargetWidget + the slot mechanism are both FINE: binding the widget to slot 0 (the main
	//     camera) rendered correctly. The bug was never the widget.
	//   - An UNPARENTED SCR_PIPCamera rendered nothing into its slot.
	//   - Parenting it to the preview character regressed to "camera ~1km away, underwater".
	//   - Parenting it to a boneless GenericEntity anchor gave IDENTICAL results to the character-parent
	//     attempt — which DISPROVED the bone-pivot/TNodeId theory that motivated the anchor.
	//   - ROOT CAUSE (found via the CameraPositionDiag read-back-after-set diagnostic, never resolved):
	//     SetWorldTransform SILENTLY NEVER TOOK EFFECT on the SCR_PIPCamera in any variant. The transform
	//     read back as <0,0,0> (spawn-time identity) immediately after every set, while the intended
	//     position logged correctly. The camera sat at its spawn transform the entire time; parenting was
	//     never the real variable.
	// The whole camera-ENTITY approach is therefore gone. Its one durable lesson is kept: a read-back-
	// after-set diagnostic is the only thing that found the real bug, so the new code keeps one
	// (CameraSetupDiag / CameraPositionDiag below).
	//
	// CURRENT APPROACH (2026-07-15): an ISOLATED BaseWorld, copied from vanilla's own
	// SCR_InventoryInspectionUI.CreatePreview() (real source, arexplorer.zeroy.com — this is the exact
	// pattern the vanilla inventory item-inspection screen uses, verbatim):
	//     m_PreviewSharedItemRef = BaseWorld.CreateWorld("InspectionPreview", "InspectionPreview");
	//     m_PreviewWorld = m_PreviewSharedItemRef.GetRef();
	//     m_PreviewCamera = 0;
	//     m_PreviewWorld.SetCameraType(m_PreviewCamera, CameraType.PERSPECTIVE);   ... near/far plane ...
	//     Resource rsc = Resource.Load("{4391FE7994EE6FE2}worlds/Sandbox/InventoryPreviewWorld10.et");
	//     if (rsc.IsValid()) GetGame().SpawnEntityPrefab(rsc, m_PreviewWorld);
	//     m_wItemRender.SetWorld(m_PreviewWorld, m_PreviewCamera);
	// Why this sidesteps everything above: there is NO camera ENTITY at all. The camera is the world's
	// own camera slot, positioned by BaseWorld.SetCameraEx(index, mat) — a direct write to the slot, not
	// a transform on an entity that some engine system may silently refuse to commit. That single fact
	// removes the exact failure that killed the old approach. And because the world is isolated there is
	// no competing main camera, so slot 0 is free (vanilla uses 0 here) — the whole "which slot is safe"
	// question disappears too.
	protected IEntity m_PreviewCharacter;
	protected RenderTargetWidget m_wPreview;

	// The isolated preview world + the ref that KEEPS IT ALIVE. m_PreviewSharedItemRef is the owning
	// handle: BaseWorld.CreateWorld returns a SharedItemRef and the world lives exactly as long as a ref
	// to it is held (vanilla's own DeletePreview() does nothing but null these two fields, in this exact
	// order — that IS the documented teardown; see OnMenuClose). `ref` is mandatory here: dropping it
	// would let the world be collected while the panel is still open. m_PreviewWorld is just the
	// non-owning convenience pointer, exactly as vanilla holds both side by side.
	protected ref SharedItemRef m_PreviewSharedItemRef;
	protected BaseWorld m_PreviewWorld;

	// The last camera matrix PositionCamera computed, re-pushed into the world's camera slot EVERY frame
	// from OnMenuUpdate. This is not an optimisation — a live read-back diagnostic proved the slot does
	// NOT retain a one-shot SetCameraEx write (it read back <0,0,0> every time), and vanilla's own
	// SCR_InventoryInspectionUI.UpdateView likewise re-writes its slot every frame. m_bPreviewCamTransform
	// Valid gates the push until PositionCamera has actually produced a matrix, so we never push a
	// garbage/uninitialised one on the first frames.
	protected vector m_PreviewCamTransform[4];
	protected bool m_bPreviewCamTransformValid;

	// The VISUAL-ONLY copy shown in the isolated world — this is what the RenderTargetWidget actually
	// shows. STEP-1 EXPERIMENT (2026-07-15): rebuilt from the REAL target's own InventoryItemComponent
	// (m_Context.GetPrimaryTarget()), NOT m_PreviewCharacter, to isolate whether CreatePreviewEntity
	// renders a fully-dressed character at all from whether it specifically renders the CLONE (two
	// prior clone attempts both logged itemComp=1 visual=1 with an empty pane). m_PreviewCharacter is
	// still the mutable clone edits apply to at this step; only the rendered subject changed. Do not
	// merge a mutable entity into the isolated world: a mutable clone placed there has a
	// non-functional inventory (live-proven).
	protected IEntity m_PreviewVisual;

	// DIAGNOSTIC counter for PreviewVisualDiag; shares CAMERA_DIAG_LOG_MAX's cap.
	protected int m_iPreviewVisualDiagCount = 0;

	// Item-grid CARD thumbnails only (CreateItemCardWidget's ItemPreviewWidget icons, via
	// SetPreviewItemFromPrefab). Do not remove this field, and do not route the card thumbnails through
	// the isolated preview world.
	//
	// These are TWO ENTIRELY SEPARATE, NON-OVERLAPPING PREVIEW MECHANISMS — conflating them has already
	// misled one diagnosis on this project, so, explicitly:
	//   - ItemPreviewManagerEntity (this field, from ChimeraWorld.GetItemPreviewManager()) is a vanilla
	//     ENGINE-LEVEL service for small icon thumbnails. The reference loadout-editor mod uses it the
	//     same way: per catalog item, ResolvePreviewEntityForPrefab(resourceName) spawns a throwaway
	//     entity just long enough to read name/type, then deletes it immediately. Not mutable, not a
	//     character, not persistent.
	//   - The CENTER CHARACTER pane is the isolated BaseWorld + RenderTargetWidget + CreatePreviewEntity
	//     path (see m_PreviewSharedItemRef / m_PreviewVisual). Nothing to do with this manager.
	protected ItemPreviewManagerEntity m_PreviewManager;

	// The prefab the preview character was spawned from (kept for parity with the old field; not
	// currently re-read, but cheap to keep around for future re-resolve needs).
	protected ResourceName m_sPreviewPrefab;

	// Tunable: camera distance from the character for a full-body shot, as a MULTIPLIER of the visual
	// entity's own measured height (see PositionCamera's 2026-07-15 bug-fix comment — CreatePreviewEntity's
	// output measured only ~1.52m tall via GetBounds, not the ~1.8m a fixed-meters constant assumed, so
	// framing off an absolute distance put the camera at the wrong scale entirely). Old absolute value was
	// 2.2m tuned against an assumed ~1.8m character (2.2/1.8 ≈ 1.22), carried forward as the multiplier so
	// the on-screen framing ratio is unchanged for a normally-sized model; the fix is that it now scales
	// with whatever height CreatePreviewEntity's output ACTUALLY measures. FrameForCategory scales this
	// down further for the Apparel tab's closer shot.
	protected const float PREVIEW_CAMERA_DISTANCE = 1.22;
	// Tunable: look-at/camera height as a FRACTION of the visual entity's own measured height, counted up
	// from its measured feet (GetBounds' min Y — see PositionCamera). Used for BOTH the camera's height
	// AND the look-at target's height so the camera looks roughly straight across at the character's face
	// instead of up/down at a mismatched target. Old absolute value was 1.65m tuned against an assumed
	// ~1.8m character (1.65/1.8 ≈ 0.92), carried forward as the fraction for the same on-screen ratio,
	// now correctly scaled to the model's REAL measured height instead of an assumed one.
	protected const float PREVIEW_CAMERA_EYE_HEIGHT = 0.92;
	// Tunable: vertical FOV in degrees. Applied via BaseWorld.SetCameraVerticalFOV(slot, fovy) on the
	// isolated preview world (CreatePreviewWorld) — it used to go through the camera ENTITY's
	// CameraBase.SetVerticalFOV + ApplyProps, which no longer exists here. The VALUE and its reasoning
	// below are unchanged and still correct; only the call that applies it moved.
	// This is the FULL vertical FOV, not a half-angle or horizontal/diagonal
	// FOV. 65 degrees full vertical FOV is WIDE for a close character portrait
	// (think GoPro/wide-angle) — at close range a wide FOV makes the subject look smaller and farther
	// away than it is, and was the primary confirmed cause of the "tiny distant figure" symptom.
	// Narrowed to 40 degrees, a normal/portrait-lens full vertical FOV, so the character reads as
	// close and centered instead of shrunk in a wide field of view.
	protected const float PREVIEW_CAMERA_FOV = 40.0;

	// Camera SLOT inside the ISOLATED preview world rendered into the preview RenderTargetWidget.
	// CHANGED 7 -> 0 (2026-07-15): slot 7 was chosen back when the camera lived in the LIVE game world,
	// where slot 0 is the main game camera and slot 8 is vanilla's PIP-sights slot, so 7 was picked to
	// dodge both. In an isolated world NONE of that applies — the world has no main camera competing for
	// slot 0, so slot 0 is free. Vanilla's own SCR_InventoryInspectionUI.CreatePreview() likewise sets
	// `m_PreviewCamera = 0;` in its isolated world. Using 0 keeps us verbatim-aligned with the vanilla
	// reference rather than carrying over a constant whose entire justification no longer exists.
	protected const int PREVIEW_CAMERA_INDEX = 0;

	// TUNABLE (2026-07-15, live-diagnosed): the camera's POSITION in the isolated world is NOT under our
	// control (see RebuildPreviewVisual's bug-fix comment — a live orbit test proved rotation writes
	// commit but position writes are pinned near the subject by something else, very likely native
	// machinery behind CreatePreviewEntity). Since the camera can't be moved, the SUBJECT is moved
	// instead, to whatever offset actually produces a full-body framing given wherever the camera is
	// really anchored.
	//
	// LIVE-CONFIRMED (2026-07-15): +3 on Z renders a full, correctly-lit, correctly-proportioned
	// character — the fix works. Two follow-up corrections from that same screenshot: the subject faced
	// away from the camera (fixed separately in RebuildPreviewVisual by building an explicit 180°-yawed
	// rotation) and appeared too HIGH in frame (feet cut off at the bottom, head near the top) — lowered
	// here by -0.8 on Y as a first correction; adjust further live if still not centered.
	protected const vector PREVIEW_SUBJECT_OFFSET = "0 -0.8 3";

	// The isolated preview world's "studio set": ground plane + sky + lights, so the subject is rendered
	// against a proper backdrop rather than an empty void. This is a VANILLA-SHIPPED asset and this exact
	// GUID+path is lifted VERBATIM from vanilla SCR_InventoryInspectionUI.CreatePreview() real source
	// (arexplorer.zeroy.com) — it is NOT invented, and not a GUID this project made up. The load is
	// guarded by Resource.IsValid() exactly as vanilla guards it, so if this asset is ever absent the
	// preview degrades to an unlit void instead of hard-failing at load.
	protected const ResourceName PREVIEW_WORLD_PREFAB = "{4391FE7994EE6FE2}worlds/Sandbox/InventoryPreviewWorld10.et";

	// Entities created on the preview character by the last apply (for cleanup).
	protected ref array<IEntity> m_aPreviewCreated = {};

	// The category currently in focus (drives the item browser).
	protected int m_iSelectedCategory = -1;

	// Second-tier filter within the active top tab (e.g. Apparel -> Headgear). 0 = "All" (the top tab's
	// full mask, today's default behavior) — matches GRAD_ItemBrowser's own "0 = mask mode off" for a
	// single type, so this doubles directly as the exact arsenal-type bit passed to SetCategory.
	protected int m_iSelectedSubCategory = 0;

	// The sub-category pill row widget ("SubCategoryRow" in the layout, a sibling of CategoryTabView and
	// CategoryItemsScroll inside PaneLeftCategories). Resolved once in SetupCategoryRail, rebuilt on every
	// top-tab switch (SelectCategoryByIndex) since each tab has a different set of constituent types.
	protected Widget m_wSubCategoryRow;

	// Faction filter: empty = "All" (every faction's items shown — matches GRAD_ItemBrowser's own
	// "blank = all factions" semantics for m_sFactionKey, so this doubles directly as the key passed to
	// SetFactionKey). The row is built ONCE (SetupCategoryRail) since the faction list doesn't change
	// per top-tab, unlike the sub-category row.
	protected string m_sSelectedFactionKey = string.Empty;
	protected Widget m_wFactionRow;
	protected ref array<ref GRAD_FactionPillHandler> m_aFactionHandlers = {};

	// Parallel to m_aFactionHandlers (same index = same pill) — each pill's OWN border image, hand-
	// recolored on selection (GRAD_FactionPill.layout does not inherit WLib_ButtonText.layout, so there
	// is no SCR_ButtonTextComponent toggle state to piggyback on here, unlike the sub-category pills).
	protected ref array<ImageWidget> m_aFactionBorders = {};

	// Faction pill layout (icon-only toggle button, self-contained — does NOT inherit
	// WLib_ButtonText.layout; see CreateFactionPill's comment for why). GUID must match
	// GRAD_FactionPill.layout.meta.
	protected const ResourceName FACTION_PILL_LAYOUT = "{D2A3B4C5D6E74001}UI/Layouts/GRAD_FactionPill.layout";
	protected const string WIDGET_FACTION_PILL_ICON   = "FactionPillIcon";
	protected const string WIDGET_FACTION_PILL_BORDER = "FactionPillBorder";
	protected const string WIDGET_FACTION_PILL_BG     = "FactionPillBg";

	// DIAGNOSTIC: last GetShownTab() value logged by PollTabChange, so it only logs on change instead
	// of spamming every frame. Remove alongside the diagnostic log line once tab selection is fixed.
	protected int m_iLastLoggedShownTab = -999;

	// DIAGNOSTIC: caps how many times PositionCamera dumps its CameraDiag numbers (it runs on every
	// framing change — menu open, tab switch — and would otherwise spam on rapid tab cycling). Remove
	// once camera framing is confirmed correct live.
	protected int m_iCameraDiagLogCount = 0;
	protected const int CAMERA_DIAG_LOG_MAX = 2;

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

	// Search box: no confirmed live-text-changed event exists on the base EditBoxWidget (verified via
	// api_search — only SCR_EditBoxComponent/SCR_ChangeableComponentBase, both vanilla components this
	// project deliberately isn't depending on here, expose m_OnChanged). Poll-and-diff instead, same
	// proven pattern PollTabChange already uses for SCR_TabViewComponent's own unconfirmed callback
	// signature: read GetText() every frame (PollSearchChange, called from OnMenuUpdate) and react only
	// when it differs from the last-seen value.
	protected EditBoxWidget m_wSearchBox;
	protected string m_sLastPolledSearch = string.Empty;

	// List-view toggle: false = the original icon-tile grid (CategoryItems), true = the new thumb-left/
	// title-right list (CategoryList). Both containers exist in the layout simultaneously; only one is
	// ever visible — PopulateItems fills whichever is active and leaves the other empty, rather than
	// keeping both in sync for a view the user isn't looking at.
	protected bool m_bListView = false;
	protected Widget m_wItemListView;		// "CategoryList" — the list-mode container
	protected Widget m_wItemGridScroll;	// "CategoryItemsScroll" — visibility-toggled opposite m_wItemListScroll
	protected Widget m_wItemListScroll;	// "CategoryListScroll"

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

	// List-view row layout (thumb-left, title-right). GUID must match GRAD_ItemListRow.layout.meta.
	protected const ResourceName LIST_ROW_LAYOUT = "{E1F2A3B4C5D61001}UI/Layouts/GRAD_ItemListRow.layout";
	protected const string WIDGET_LIST_ROW_ICON  = "ListRowIcon";
	protected const string WIDGET_LIST_ROW_NAME  = "ListRowName";
	protected const string WIDGET_LIST_ROW_COUNT = "ListRowCount";
	protected const string WIDGET_LIST_ROW_BG    = "ListRowBg";

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

	// Sub-category pill handlers. Rebuilt on every top-tab switch (RebuildSubCategoryRow) — MUST be
	// cleared each rebuild, same reasoning as m_aItemRowHandlers above.
	protected ref array<ref GRAD_SubCategoryHandler> m_aSubCategoryHandlers = {};

	// Parallel to m_aSubCategoryHandlers (same index = same pill) — each pill's own SCR_ButtonTextComponent,
	// kept so OnSubCategoryClicked can toggle exactly one pill active without re-walking child widgets.
	protected ref array<SCR_ButtonTextComponent> m_aSubCategoryButtons = {};

	// Loadout-panel line handlers ([-]/[+] per contained item). Rebuilt on every RefreshLoadoutPanel
	// (cleared there), so no stale invoker fires on a freed line widget.
	protected ref array<ref GRAD_LoadoutLineHandler> m_aLoadoutLineHandlers = {};

	// The 3 per-slot REMOVE-garment button handlers, created once in BindButtons and kept alive for
	// the menu's whole lifetime (these buttons are fixed layout widgets, never rebuilt).
	protected ref array<ref GRAD_UnequipHandler> m_aUnequipHandlers = {};

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

		// Unequip garment buttons (one per loadout-panel slot header) — lets the user take off a worn
		// uniform/vest/backpack, not just edit its contents. Bound generically via each button's own
		// arsenal-type bit so OnUnequipGarment can resolve the same FindNamedContainer(bit) already used
		// everywhere else in this file.
		BindUnequipButton(root, WIDGET_BTN_REMOVE_UNIFORM, 2048);	// TORSO (uniform)
		BindUnequipButton(root, WIDGET_BTN_REMOVE_VEST, 4096);		// VEST
		BindUnequipButton(root, WIDGET_BTN_REMOVE_BACKPACK, 128);	// BACKPACK
		BindUnequipButton(root, WIDGET_BTN_REMOVE_TROUSERS, 8192);	// LEGS (trousers) — not a container, see OnUnequipGarment's non-container branch
		BindUnequipButton(root, WIDGET_BTN_REMOVE_HEADGEAR, 1024);	// HEADWEAR — not a container, same non-container branch as Trousers
		SetButtonText(root, WIDGET_BTN_REMOVE_UNIFORM, "REMOVE");
		SetButtonText(root, WIDGET_BTN_REMOVE_VEST, "REMOVE");
		SetButtonText(root, WIDGET_BTN_REMOVE_BACKPACK, "REMOVE");
		SetButtonText(root, WIDGET_BTN_REMOVE_TROUSERS, "REMOVE");
		SetButtonText(root, WIDGET_BTN_REMOVE_HEADGEAR, "REMOVE");

		// Start with nothing selected → ADD buttons disabled.
		SetAddButtonsEnabled(false, false, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Bind one REMOVE-garment button to OnUnequipGarment via a small persistent handler carrying the
	//! arsenal-type bit — the same GRAD_UnequipHandler.OnActivated -> menu bridge pattern already used
	//! for other multi-instance callbacks in this file (GRAD_LoadoutLineHandler).
	protected void BindUnequipButton(notnull Widget root, string buttonName, int arsenalTypeBit)
	{
		SCR_InputButtonComponent btn = SCR_InputButtonComponent.GetInputButtonComponent(buttonName, root);
		if (!btn)
			return;

		GRAD_UnequipHandler handler = new GRAD_UnequipHandler(this, arsenalTypeBit);
		m_aUnequipHandlers.Insert(handler);
		btn.m_OnActivated.Insert(handler.OnActivated);
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
	//! CAMERA SLOT INDEX, not a camera entity handle. Here that slot lives in our OWN isolated preview
	//! world (see the m_PreviewSharedItemRef field comment), so it is driven by writing the slot directly
	//! via BaseWorld.SetCameraEx — there is no camera entity and no per-frame push any more.
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
		// engine-managed auto-framed preview path — see m_PreviewManager's field comment. This is the
		// LIVE world's preview manager and is deliberately unrelated to the isolated preview world below.
		if (world)
			m_PreviewManager = world.GetItemPreviewManager();

		// Create the isolated preview world BEFORE spawning the clone, since the clone is spawned INTO it.
		if (!CreatePreviewWorld())
			return;

		// BUG FIX (2026-07-15, per reference-mod comparison): SetWorld used to be called LAST, after the
		// visual entity was created and framed — the reference mod ("bacon") instead binds the widget to
		// the world IMMEDIATELY after world+camera setup, before its subject entity even exists (relying
		// on the placeholder camera write in CreatePreviewWorld to give the slot a valid shot in the
		// meantime). Matching that ordering here in case the widget only starts actually rendering once
		// bound, and late-binding after the camera was already repositioned left it presenting a stale
		// pre-bind state. CreatePreviewWorld's placeholder SetCameraEx write ensures this bind never
		// shows a literally-uninitialized camera in the gap before FrameFullBody runs.
		m_wPreview.SetWorld(m_PreviewWorld, PREVIEW_CAMERA_INDEX);
		GRAD_Log.Info(string.Format("ArsenalMenu: preview bound to ISOLATED world camera slot %1 (early bind)", PREVIEW_CAMERA_INDEX));

		IEntity primary = m_Context.GetPrimaryTarget();
		m_sPreviewPrefab = GRAD_InventoryLib.GetPrefabResourceName(primary);

		if (m_sPreviewPrefab != ResourceName.Empty)
		{
			// THE SPLIT (2026-07-14, live-diagnosed — read this before changing anything here):
			//
			// The mutable clone lives in the LIVE world; only a VISUAL COPY goes in the isolated preview
			// world. These are two different entities with two different jobs, and merging them does not
			// work:
			//
			//  - A first pass spawned the mutable clone directly INTO the isolated world (origin), on the
			//    theory that this deletes the old placement problems (ocean/free-fall/overlap). It rendered
			//    a dark, empty pane AND silently broke the inventory: "Apply: spawned 8 items" where 30 were
			//    expected (no failures logged), then "Capture: 'ArsenalResult' -> 0 nodes" with ALL FIVE
			//    storage roots reporting "0 occupied", which made Confirm refuse (ok=0, "empty loadout").
			//    A freshly BaseWorld.CreateWorld'd world contains no game systems — the clone's inventory
			//    manager is simply not functional in there, so TrySpawnPrefabToStorage no-ops exactly the
			//    way SpawnLocal+insert did in the earlier bug. This is the SAME failure class, new cause.
			//  - Vanilla never does this either: SCR_InventoryInspectionUI keeps the real item in the real
			//    world and puts only a CreatePreviewEntity() VISUAL clone in the preview world.
			//
			// So: clone spawns in the LIVE world (where its inventory actually works — this was working
			// before the isolated world was introduced, and the placement workaround below is still needed
			// there), and RefreshPreviewRender builds/rebuilds the visual copy in the isolated world.
			//
			// Placement workaround, still required for the LIVE-world clone: world origin is open
			// ocean/void on Reforger maps (live-confirmed), and spawning 500m up makes it FREE-FALL (a
			// freshly spawned ChimeraCharacter is not physics-frozen; no verified freeze/kinematic API
			// exists in the indexed script API). Spawn at the primary target's own ground level, offset 5m
			// along their right axis, so it neither drowns nor falls nor overlaps the real player's body.
			// It is never rendered directly, so exactly where it stands no longer matters visually.
			vector spawnPos = vector.Zero;
			if (primary)
			{
				vector primaryTransform[4];
				primary.GetWorldTransform(primaryTransform);
				vector primaryRight = primaryTransform[0];
				primaryRight.Normalize();
				spawnPos = primaryTransform[3] + primaryRight * 5.0;
			}

			// Note the omitted third arg: SpawnLocal defaults to the LIVE world, which is what we want.
			m_PreviewCharacter = GRAD_InventoryLib.SpawnLocal(m_sPreviewPrefab, spawnPos);

			if (m_PreviewCharacter)
			{
				// RESOLVED (2026-07-14): the long-suspected "dead/ragdolled clone" theory was wrong.
				// ECharacterLifeState's real declaration (arexplorer) is { ALIVE, INCAPACITATED, DEAD } —
				// ALIVE = 0, so the live log's "GetLifeState()=0" always meant the clone was perfectly
				// healthy. The wonky preview was the CAMERA (the old live-world camera slot never received
				// our transform at all — see the m_PreviewSharedItemRef field comment for the full
				// post-mortem). The earlier ActivateAI() attempt is deliberately GONE: it targeted
				// a non-problem, and a preview mannequin must never have live AI anyway (an activated agent
				// could move/react on its own). ForceStanceUp stays as cheap insurance that the clone is in
				// the STAND pose regardless of what state the prefab spawned in.
				ChimeraCharacter previewChimera = ChimeraCharacter.Cast(m_PreviewCharacter);
				if (previewChimera)
				{
					CharacterControllerComponent previewController = previewChimera.GetCharacterController();
					if (previewController)
					{
						previewController.ForceStanceUp(ECharacterStance.STAND);
						GRAD_Log.Info(string.Format("ArsenalMenu: preview clone lifeState=%1 (0=ALIVE) stance forced to STAND", previewController.GetLifeState()));
					}
					else
					{
						GRAD_Log.Warn("ArsenalMenu: preview clone has no CharacterControllerComponent");
					}
				}

				// Match the edited unit's face/appearance (a fresh spawn gets a random identity).
				CopyIdentity(primary, m_PreviewCharacter);

				// Mirror the target's current loadout so the player edits from their real starting kit.
				// force=false: keep the prefab's locked cosmetic body/clothing nodes; only mirror the
				// editable items on top.
				GRAD_LoadoutData current = GRAD_LoadoutCapture.Capture(primary, "PreviewBase", true);
				if (current)
					GRAD_LoadoutApply.Apply(m_PreviewCharacter, current, true, false, m_aPreviewCreated);

				PinPreviewAlive();

				// STEP-1 EXPERIMENT (2026-07-15): render the REAL target's own InventoryItemComponent
				// instead of the clone's. Two isolated-world attempts at rendering a dressed CLONE both
				// reported green diagnostics (PreviewVisualDiag: itemComp=1 visual=1) yet the pane stayed
				// empty both times — the one thing never actually proven is whether CreatePreviewEntity
				// renders a fully-dressed character clone the same way it renders vanilla's single
				// inspected item. The real target is already correctly rendered every frame in the live
				// world, so there is nothing to prove about IT rendering — only about whether
				// CreatePreviewEntity accepts it. This is a single-axis change (see docs/HANDOFF_2026-07-15.md
				// "how I misled this investigation" — stop varying an axis that produces the same symptom
				// twice); the clone/RPC/live-edit rework is a separate, later step, NOT done here.
				RebuildPreviewVisual();

				// Frame the shot. The widget is ALREADY bound to the isolated world (see the early
				// SetWorld call right after CreatePreviewWorld, above) — this just re-positions the
				// camera slot the widget is already watching.
				FrameFullBody();
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Create the ISOLATED preview world, configure its camera slot, and populate it with the studio set.
	//! Returns false if the world could not be created (caller then skips all preview setup).
	//!
	//! This follows vanilla SCR_InventoryInspectionUI.CreatePreview() (real source, arexplorer.zeroy.com)
	//! call-for-call — see the m_PreviewSharedItemRef field comment for that verbatim reference and for
	//! why the whole camera-ENTITY approach it replaces was abandoned.
	//!
	//! VERIFIED SIGNATURES (api_search, Enfusion > World — every one of these was confirmed to exist
	//! before being written, per this project's hard rule that an unverified API guess once crashed the
	//! engine natively):
	//!   static proto SharedItemRef BaseWorld.CreateWorld(string type, string name)
	//!   proto external void BaseWorld.SetCameraType(int cam, CameraType type)
	//!   proto external void BaseWorld.SetCameraNearPlane(int cam, float nearplane)
	//!   proto external void BaseWorld.SetCameraFarPlane(int cam, float farplane)
	//!   proto external void BaseWorld.SetCameraVerticalFOV(int cam, float fovy)
	//!   proto external void BaseWorld.SetCameraEx(int cam, const vector mat[4])
	//!   proto external IEntity Game.SpawnEntityPrefab(notnull Resource templateResource, BaseWorld world = null, EntitySpawnParams params = null)
	//!   proto external bool SharedItemRef.IsValid()
	//! The "InspectionPreview" world TYPE string is likewise taken verbatim from that vanilla source — it
	//! is not a guess, and it is the one argument here with no way to validate by signature alone.
	protected bool CreatePreviewWorld()
	{
		m_PreviewSharedItemRef = BaseWorld.CreateWorld("InspectionPreview", "InspectionPreview");
		if (!m_PreviewSharedItemRef || !m_PreviewSharedItemRef.IsValid())
		{
			GRAD_Log.Error("ArsenalMenu: BaseWorld.CreateWorld('InspectionPreview') failed; preview disabled");
			m_PreviewSharedItemRef = null;
			return false;
		}

		// SharedItemRef.GetRef() is typed as returning SharedItem; vanilla assigns it straight into a
		// BaseWorld field (`m_PreviewWorld = m_PreviewSharedItemRef.GetRef();`) with no cast, so the
		// engine's binding resolves it. Mirroring vanilla exactly rather than inventing a cast.
		m_PreviewWorld = m_PreviewSharedItemRef.GetRef();
		if (!m_PreviewWorld)
		{
			GRAD_Log.Error("ArsenalMenu: preview world ref resolved to null; preview disabled");
			m_PreviewSharedItemRef = null;
			return false;
		}

		// Configure the isolated world's camera slot. Vanilla sets type/far/near only; we additionally set
		// the vertical FOV because this project's framing math depends on PREVIEW_CAMERA_FOV being the
		// real lens (see that constant's comment — a too-wide FOV was a confirmed cause of the old "tiny
		// distant figure" symptom). Near plane 0.05 (not vanilla's 0.001) because our subject is a
		// character metres away, not an item held centimetres from the lens — a deliberate, known
		// divergence from the reference mod's 0.001 (confirmed against its source), not an oversight.
		m_PreviewWorld.SetCameraType(PREVIEW_CAMERA_INDEX, CameraType.PERSPECTIVE);
		m_PreviewWorld.SetCameraFarPlane(PREVIEW_CAMERA_INDEX, 500.0);
		m_PreviewWorld.SetCameraNearPlane(PREVIEW_CAMERA_INDEX, 0.05);
		m_PreviewWorld.SetCameraVerticalFOV(PREVIEW_CAMERA_INDEX, PREVIEW_CAMERA_FOV);

		// PLACEHOLDER CAMERA WRITE (2026-07-15, added per reference-mod comparison): the reference mod
		// ("bacon") ALWAYS has a valid matrix in its camera slot from the moment SetWorld binds the
		// widget — even before its real subject is framed, it writes a fixed placeholder shot (source
		// {5,1,5} looking at {0,0.5,0}). Our code only ever wrote the slot later, inside PositionCamera
		// (called from FrameFullBody in SetupPreview), which depends on m_PreviewVisual already existing.
		// This was a real, unexplained gap: without it, "camera never got a first write" and "camera got
		// written but framed wrong" are visually indistinguishable from outside — both would show
		// whatever the engine's default/identity camera state renders as. Writing a placeholder here
		// closes that ambiguity for future diagnosis, even though it isn't proven to be THIS bug (our
		// diagnostics already show PositionCamera DOES run and DOES write a real, changing matrix on
		// every test so far — this placeholder covers the brief window before that first real write, and
		// guards against any future path where RebuildPreviewVisual/PositionCamera silently doesn't run).
		vector placeholderPos = "0 1 5";
		vector placeholderLook = "0 0.5 0";
		vector placeholderTransform[4];
		SCR_Math3D.LookAt(placeholderPos, placeholderLook, "0 1 0", placeholderTransform);
		m_PreviewWorld.SetCameraEx(PREVIEW_CAMERA_INDEX, placeholderTransform);

		// Spawn the studio set (ground/sky/lights). A brand-new BaseWorld is COMPLETELY empty — no ground,
		// no sky, and crucially no LIGHT — so without this the subject renders against a void and is very
		// likely to be an unlit silhouette. Guarded by IsValid() exactly as vanilla guards it, so a missing
		// asset degrades the shot instead of failing the menu.
		//
		// A temporary diagnostic skip of this spawn was tried here (2026-07-15) to rule out the studio set
		// dominating the frame, after two rounds of camera-math fixes both produced verified-different
		// transforms with a pixel-identical render. Superseded before that test ran: the real root cause
		// was found instead (GetBounds vs GetWorldBounds — see PositionCamera's bug-fix comment), so the
		// studio set was reinstated rather than tested as a red herring.
		Resource studioSet = Resource.Load(PREVIEW_WORLD_PREFAB);
		bool studioSetLoaded = studioSet && studioSet.IsValid();
		if (studioSetLoaded)
			GetGame().SpawnEntityPrefab(studioSet, m_PreviewWorld);
		else
			GRAD_Log.Warn(string.Format("ArsenalMenu: studio-set prefab '%1' failed to load; preview will render unlit/void", PREVIEW_WORLD_PREFAB));

		// TEMPORARY DIAGNOSTIC (2026-07-15) — RE-ASSERT FOV/CAMERA CONFIG AFTER STUDIO-SET SPAWN: a small,
		// correctly-bounded, correctly-positioned object (M9 handgun, ~20cm, confirmed via GetWorldBounds)
		// viewed from a confirmed-correct 4m camera distance (backdrop responds to camera moves) STILL
		// rendered as if filling the frame from centimeters away — object size and camera position are
		// both ruled out, which points at the FOV/projection itself being wrong. Theory: the vanilla
		// studio-set prefab (PREVIEW_WORLD_PREFAB, an ITEM-inspection backdrop whose whole purpose is a
		// tight macro shot of a small held item) may itself contain a camera-configuration entity that
		// silently re-applies its own narrow FOV to slot 0 AFTER we set ours above, since it's spawned
		// right after our own SetCameraVerticalFOV call. Re-asserting our config here, AFTER the spawn,
		// tests whether ours then wins. Revert (fold back into the block above) once the test result is
		// known either way.
		m_PreviewWorld.SetCameraType(PREVIEW_CAMERA_INDEX, CameraType.PERSPECTIVE);
		m_PreviewWorld.SetCameraFarPlane(PREVIEW_CAMERA_INDEX, 500.0);
		m_PreviewWorld.SetCameraNearPlane(PREVIEW_CAMERA_INDEX, 0.05);
		m_PreviewWorld.SetCameraVerticalFOV(PREVIEW_CAMERA_INDEX, PREVIEW_CAMERA_FOV);

		// DIAGNOSTIC (CameraSetupDiag, adapted from the old camera-entity version): confirms the world was
		// created, the ref is valid, and the studio set loaded. If the preview renders black, this line
		// distinguishes "world/camera never came up" from "world is fine, framing or lighting is wrong".
		GRAD_Log.Info(string.Format("CameraSetupDiag: previewWorld=%1 refValid=%2 cameraSlot=%3 fov=%4 studioSet=%5",
			m_PreviewWorld != null, m_PreviewSharedItemRef.IsValid(), PREVIEW_CAMERA_INDEX,
			PREVIEW_CAMERA_FOV, studioSetLoaded));

		return true;
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
	//! Refresh the preview render after a loadout mutation: re-pin the (live-world) clone against the
	//! lifetime reaper, then REBUILD the visual copy shown in the isolated preview world.
	//!
	//! ARCHITECTURE (2026-07-14, live-diagnosed — see SetupPreview's spawn comment for the full evidence):
	//! two entities, two jobs.
	//!   - m_PreviewCharacter: a REAL, MUTABLE ChimeraCharacter clone in the LIVE world. Every mutation
	//!     path in this menu (GRAD_LoadoutApply, the [-]/[+] lines, OnUnequipGarment) edits THIS entity's
	//!     real inventory. It must be in the live world: a freshly BaseWorld.CreateWorld'd world has no
	//!     game systems, and a clone placed there has a NON-FUNCTIONAL inventory manager (live-proven:
	//!     8 of 30 items silently placed, then Capture read 0 occupied across all five storage roots and
	//!     Confirm refused with "empty loadout"). It is never rendered directly.
	//!   - m_PreviewVisual: a visual-only copy in the ISOLATED world, created by calling
	//!     InventoryItemComponent.CreatePreviewEntity on m_PreviewCharacter's OWN item component. This is
	//!     what the RenderTargetWidget actually shows.
	//!
	//! This split mirrors a real, working loadout-editor mod: it keeps the live edited character in the
	//! real world and calls CreatePreviewEntity on that character's InventoryItemComponent to spawn a
	//! disconnected visual clone into its isolated preview world. (Vanilla's SCR_InventoryInspectionUI
	//! was the reference for the WORLD/CAMERA plumbing only — it previews a single static ITEM, so its
	//! entity handling does not generalise to a character loadout editor. An earlier revision of this
	//! comment claimed the split was "what vanilla does"; that was an extrapolation, not a finding.)
	//!
	//! GRAD_Loadout's own twist vs. that reference: it edits a CLONE of the target rather than the target
	//! itself, so the player can cancel without touching their real character. So our m_PreviewCharacter
	//! is the analogue of the reference's live edited character, and CreatePreviewEntity is called on the
	//! clone's item component. That mapping is an inference about OUR design, not a claim about theirs.
	//!
	//! Because the visual copy is a disconnected SNAPSHOT, it must be rebuilt after every mutation —
	//! hence this method being called from every mutation path. Rebuild-per-mutation is the reference's
	//! own design (its UpdatePreview() tears down and recreates the clone on each post-mutation
	//! SetPreviewedEntity), NOT a workaround or an unverified gamble, as an earlier note here hedged.
	protected void RefreshPreviewRender()
	{
		PinPreviewAlive();
		RebuildPreviewVisual();
	}

	//------------------------------------------------------------------------------------------------
	//! Destroy and re-create the visual-only copy shown in the isolated world. Safe to call repeatedly;
	//! no-op without a world or a source entity.
	//!
	//! STEP-1 EXPERIMENT (2026-07-15): sourced from the REAL target's own InventoryItemComponent
	//! (`m_Context.GetPrimaryTarget()`), NOT the clone's. Two prior attempts rendering a dressed CLONE
	//! both logged itemComp=1 visual=1 yet the pane stayed empty — an unproven "does CreatePreviewEntity
	//! accept a fully-dressed character clone" risk that a real, already-correctly-rendered entity
	//! sidesteps entirely. The clone (m_PreviewCharacter) still exists and is still what gets EDITED at
	//! this step — only the rendered subject changed. See SetupPreview's call site comment.
	//!
	//! Uses InventoryItemComponent.CreatePreviewEntity(world, camera) — verified real:
	//!   proto external IEntity CreatePreviewEntity (BaseWorld world, int camera)
	//! `CreatePreviewEntity` ALSO exists on BaseInventoryStorageComponent and on
	//! SCR_CharacterInventoryStorageComponent — an earlier pass here used the character-storage overload
	//! purely because the signature existed and sounded character-shaped. That was signature-driven
	//! guessing, not evidence; the working reference uses the plain item-component overload for a whole
	//! dressed character. Do not "upgrade" this back to a storage overload without live proof.
	protected void RebuildPreviewVisual()
	{
		if (!m_PreviewWorld)
			return;

		// Drop the previous snapshot first — this is a rebuild, not an addition. The reference mod does
		// the same on every mutation (its UpdatePreview() tears down and recreates), so this is the
		// intended lifecycle, not a workaround.
		if (m_PreviewVisual)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_PreviewVisual);
			m_PreviewVisual = null;
		}

		// BUG FIX (2026-07-15, live-diagnosed): this rendered m_Context.GetPrimaryTarget() — the REAL,
		// unedited target character — ever since the "STEP-1 EXPERIMENT" at the very start of this
		// session (isolating whether CreatePreviewEntity could render a dressed character at all, before
		// the camera-position bug was found and fixed). That experiment was never reverted: every click
		// edits m_PreviewCharacter (the clone), but the preview pane kept showing the REAL target's
		// unchanged state the whole time — live-confirmed by the user removing their uniform on the clone
		// and the preview still showing it worn, right up until Confirm (which correctly captured the
		// CLONE's real state and applied it, producing a "naked after confirm, but preview showed
		// clothed" mismatch). Now that the camera/subject-positioning bug is independently fixed and
		// confirmed working, render the entity actually being edited again.
		IEntity renderSource = m_PreviewCharacter;

		if (!renderSource)
			return;

		bool DIAG_USE_SIMPLE_ITEM = false;
		IEntity itemSourceEntity = null;
		if (DIAG_USE_SIMPLE_ITEM)
		{
			Resource diagResource = Resource.Load("{1353C6EAD1DCFE43}Prefabs/Weapons/Handguns/M9/Handgun_M9.et");
			if (diagResource && diagResource.IsValid())
				itemSourceEntity = GetGame().SpawnEntityPrefabLocal(diagResource, m_PreviewWorld);

			if (!itemSourceEntity)
				GRAD_Log.Warn("ArsenalMenu: DIAG_USE_SIMPLE_ITEM failed to spawn M9 into preview world");
		}
		else
		{
			itemSourceEntity = renderSource;
		}

		InventoryItemComponent itemComp = null;
		if (itemSourceEntity)
			itemComp = InventoryItemComponent.Cast(itemSourceEntity.FindComponent(InventoryItemComponent));
		if (itemComp)
			m_PreviewVisual = itemComp.CreatePreviewEntity(m_PreviewWorld, PREVIEW_CAMERA_INDEX);

		// The diagnostic M9 was spawned directly into the isolated world as a REAL entity (not just a
		// preview copy) — delete it now that CreatePreviewEntity has produced the actual preview subject,
		// so only the preview copy remains visible (matching the character path, which never keeps a
		// second real copy in this world either).
		if (DIAG_USE_SIMPLE_ITEM && itemSourceEntity && itemSourceEntity != m_PreviewVisual)
			SCR_EntityHelper.DeleteEntityAndChildren(itemSourceEntity);

		// BUG FIX (2026-07-15, concept found via a real, working reference implementation's own
		// UpdatePreview(): it calls entity.Update() right after CreatePreviewEntity, which is the concept
		// borrowed here — the call site, casts, and surrounding code below are this project's own, not
		// copied). This project has already hit the identical class of bug once before in this same file
		// (PopulateItems' comment: populating a grid via SetColumn/SetRow does not itself trigger a
		// relayout — Widget.Update() was required to force it) — a freshly created/positioned object not
		// committing its state until something explicitly forces an update is a recurring engine
		// behavior here, not a one-off. GenericEntity.Update() (cast the same way visualGe is obtained
		// just below) closes that same gap for the entity's own transform/render state, in case it
		// likewise never commits without an explicit call.
		GenericEntity previewVisualGe = GenericEntity.Cast(m_PreviewVisual);
		if (previewVisualGe)
			previewVisualGe.Update();

		// BUG FIX (2026-07-15, live-diagnosed via the orbit test in OnMenuUpdate): a continuously-
		// rotating camera transform showed the view's ROTATION visibly respond to our SetCameraEx writes,
		// but the camera's POSITION never moved relative to the subject (the boot stayed fixed in frame
		// the whole time, only the backdrop swept past as the view rotated). This means the camera's
		// position is NOT under our control here — something else (very likely the native subsystem
		// behind CreatePreviewEntity) pins it near the subject at a fixed, close distance. Fighting that
		// from the camera side is a dead end; instead, move the SUBJECT itself away from that fixed
		// camera anchor so a full-body shot becomes visible. PREVIEW_SUBJECT_OFFSET pushes the preview
		// entity along +Z (the camera's observed forward axis, charForward=<0,0,1> read consistently all
		// session), using the already-verified SetWorldTransform (proto external bool
		// SetWorldTransform(vector mat[4]), confirmed on the IEntity-family base). The exact offset is
		// UNVERIFIED and the first thing to tune live if the next test isn't framed correctly yet.
		if (previewVisualGe)
		{
			// LIVE-CONFIRMED (2026-07-15): moving the subject to PREVIEW_SUBJECT_OFFSET renders a full,
			// correctly-lit, correctly-proportioned character — the fix works. Two adjustments needed on
			// top of the plain position write: (1) the subject faced AWAY from the camera (its back was
			// shown) — rotate 180° about the Y (up) axis so it faces back toward the camera instead of
			// keeping whatever forward CreatePreviewEntity originally assigned; (2) it appeared too high
			// in frame (feet cut off at the bottom) — PREVIEW_SUBJECT_OFFSET's height needs lowering,
			// tuned there directly rather than duplicated here.
			//
			// Build a level, camera-facing rotation directly (right = -X, up = +Y, forward = -Z — a
			// straight 180° yaw from the identity's default +Z forward) rather than reusing whatever
			// CreatePreviewEntity assigned, since that's exactly the orientation that was live-confirmed
			// wrong.
			vector visualTransform[4];
			visualTransform[0] = "-1 0 0";
			visualTransform[1] = "0 1 0";
			visualTransform[2] = "0 0 -1";
			visualTransform[3] = PREVIEW_SUBJECT_OFFSET;
			previewVisualGe.SetWorldTransform(visualTransform);
			previewVisualGe.Update();
		}

		// BUG FIX (2026-07-15, found via a genuine, disciplined line-by-line diff against the reference
		// implementation's own SetEntityQuality(), called on its preview entity right after
		// CreatePreviewEntity — a call this project had never made at all until now):
		//   SetVComponentFlags(VCFlags) and SetFixedLOD(int) are both VERIFIED real (api_search:
		//   proto external int SetVComponentFlags(VCFlags flags) / proto external void SetFixedLOD(int lod),
		//   both present on the IEntity-family base, not something invented for this project).
		// SetFixedLOD(0) pins the entity to its highest level of detail regardless of the engine's normal
		// distance-based LOD selection. This is a genuine, concrete candidate for the "distance changes the
		// backdrop but never the subject" symptom: if LOD selection in a freshly-created isolated BaseWorld
		// has no valid "normal" distance reference to select against (there being no established camera
		// history/scale the LOD system expects), the engine could plausibly default to some LOW, FIXED-
		// looking LOD regardless of camera distance — a fixed-appearing render is exactly what was observed.
		// Applying this is a direct, verified, minimal difference from a WORKING reference, not a guess.
		if (previewVisualGe)
		{
			previewVisualGe.SetFixedLOD(0);
			previewVisualGe.SetVComponentFlags(VCFlags.NOFILTER & VCFlags.NOLIGHT);
		}

		// DIAGNOSTIC (PreviewVisualDiag): capped, so repeated mutations don't spam. If visual=0 the
		// isolated world has nothing to render and the pane will be empty regardless of camera framing —
		// that distinction (nothing to see vs. camera pointed wrong) is exactly what cost this project
		// several sessions on the old architecture, so it is worth logging explicitly. itemComp=0 would
		// mean the real target has no InventoryItemComponent at all, which would itself be the story.
		if (m_iPreviewVisualDiagCount < CAMERA_DIAG_LOG_MAX)
		{
			m_iPreviewVisualDiagCount++;
			GRAD_Log.Info(string.Format("PreviewVisualDiag: itemComp=%1 visual=%2 (source=REAL target)",
				itemComp != null, m_PreviewVisual != null));

			// READ-BACK DIAGNOSTIC (2026-07-15): a live screenshot showed only a sliver of the character
			// (a helmet) at the far-left edge of an otherwise black pane — visual=1 but clearly NOT framed
			// where PositionCamera expects it. PositionCamera assumes m_PreviewVisual sits at the SOURCE
			// entity's transform (charPos=<0,0,0>, charForward=<0,0,1> was being logged, i.e. the world
			// origin — never verified against the actual spawned visual). CreatePreviewEntity may place its
			// output at its OWN origin/orientation inside the isolated world rather than mirroring the
			// source's live-world transform. Log what the visual actually reports before touching
			// PositionCamera's math, per this project's own rule: read back actual state instead of
			// guessing again at the same untested assumption.
			//
			// CONFIRMED LIVE: visualPos=<0,0,0> visualForward=<0,0,1> — the visual DOES sit at the isolated
			// world's origin (not the source's live-world position), so PositionCamera's framing around
			// <0,0,0> is targeting the right point. A SECOND live test then showed the visible sliver was a
			// BOOT CAP, extremely close and low — i.e. the model's actual body extends well ABOVE and
			// AWAY from <0,0,0>, not centered on it the way the old clone (whose ROOT was at its own
			// standing-character pivot) was. GetWorldBounds (verified: GenericEntity.GetWorldBounds(out
			// vector mins, out vector maxs), proto external — NOTE: this diagnostic used the LOCAL-space
			// GetBounds until 2026-07-15, which is why its logged numbers looked identical across tests
			// even after PositionCamera itself was fixed to use GetWorldBounds; this was a diagnostic-only
			// staleness, not a camera-framing bug, since PositionCamera reads its own bounds separately)
			// reads the model's actual WORLD-space extents, so the real pivot offset can be measured
			// instead of guessed a third time.
			if (m_PreviewVisual)
			{
				vector visualTransform[4];
				m_PreviewVisual.GetWorldTransform(visualTransform);
				GRAD_Log.Info(string.Format("PreviewVisualTransformDiag: visualPos=%1 visualForward=%2",
					visualTransform[3].ToString(), visualTransform[2].ToString()));

				GenericEntity visualGe = GenericEntity.Cast(m_PreviewVisual);
				if (visualGe)
				{
					vector boundsMin, boundsMax;
					visualGe.GetWorldBounds(boundsMin, boundsMax);
					GRAD_Log.Info(string.Format("PreviewVisualBoundsDiag: min=%1 max=%2",
						boundsMin.ToString(), boundsMax.ToString()));
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Frame the whole character: position the camera slot in front of the character, level with its
	//! eyes, looking straight back at its face (PREVIEW_CAMERA_DISTANCE/PREVIEW_CAMERA_EYE_HEIGHT,
	//! tunable MULTIPLIER/FRACTION-of-measured-height constants declared with the fields above — see
	//! PositionCamera's 2026-07-15 bug-fix comment for why these are no longer absolute meters). Replaces
	//! the old PreviewRenderAttributes pitch+FOV hack — the camera slot can just be placed in space like
	//! anything else instead of fighting a fixed-distance, no-pivot render API.
	protected void FrameFullBody()
	{
		PositionCamera(PREVIEW_CAMERA_DISTANCE, PREVIEW_CAMERA_EYE_HEIGHT);
	}

	//------------------------------------------------------------------------------------------------
	//! Approximate a body-region focus for the given tab by moving the camera closer, matching the
	//! intent of the old pitch/FOV hack but via camera transform. tabIndex: 0 Primary, 1 Secondary,
	//! 2 Throwables, 3 Apparel, 4 Container. Apparel gets a closer shot; the rest use the same
	//! full-body eye-level framing as FrameFullBody. Tune the constants live.
	protected void FrameForCategory(int tabIndex)
	{
		float distance = PREVIEW_CAMERA_DISTANCE;

		// Apparel: closer shot for a clothing-focused view. Still eye-level (not scaled down like the
		// old height*0.7) — PositionCamera aims the look-at target at the SAME eye height as the camera
		// regardless of distance, so closing the distance alone tightens the framing without
		// re-introducing the camera/look-at height mismatch that caused the original bug.
		if (tabIndex == 3)
			distance = PREVIEW_CAMERA_DISTANCE * 0.6;

		PositionCamera(distance, PREVIEW_CAMERA_EYE_HEIGHT);

		GRAD_Log.Info(string.Format("FrameForCategory: tab=%1", tabIndex));
	}

	//------------------------------------------------------------------------------------------------
	//! Place the preview camera in the isolated world. `distanceMul`/`eyeHeightFrac` scale a nominal
	//! starting pose (see the FINAL ROOT CAUSE note for why the camera's POSITION is no longer the real
	//! lever — the subject is what actually gets moved now, in RebuildPreviewVisual).
	//!
	//! HISTORY OF THIS INVESTIGATION (2026-07-15, several superseded theories on the way to the real
	//! answer — kept because each ruled something out that would otherwise be re-tried):
	//!  1. First suspected the subject's world-space bounds were read wrong (local vs. world space via
	//!     GetBounds vs. GetWorldBounds) — real bug, fixed, but not sufficient on its own.
	//!  2. Then suspected SetWorld bind timing / a missing initial placeholder camera write — both
	//!     genuine gaps vs. a working reference mod, fixed, still not sufficient.
	//!  3. Then suspected the subject is rendered at a FIXED OFFSET RELATIVE TO THE CAMERA (an extreme
	//!     <0,10,0>-looking-down test changed the backdrop but not the subject's apparent size/position,
	//!     which looked exactly like "moving the anchor moves the subject by the same amount").
	//!  4. FINAL, CONCLUSIVE TEST: a per-frame LIVE ORBIT (continuously changing position AND rotation,
	//!     written every frame) proved theory 3 wrong in its specifics — the camera's ROTATION visibly
	//!     swept the view (the backdrop moved past), but its POSITION never actually moved relative to the
	//!     subject (the boot/character stayed fixed in frame throughout). So SetCameraEx's rotation
	//!     columns are honored; its position column is not — something else (very likely native machinery
	//!     inside CreatePreviewEntity/the preview camera slot) pins the camera's position near the subject.
	//!
	//! CONSEQUENTLY: computing/writing camPos here is largely symbolic now — the engine does not appear to
	//! honor it. The actual fix is moving the SUBJECT away from the camera's fixed anchor point
	//! (RebuildPreviewVisual's PREVIEW_SUBJECT_OFFSET + SetWorldTransform) and pointing lookTarget at that
	//! new position, so whatever the camera's real (engine-owned) position turns out to be, its rotation —
	//! which IS under our control — still faces the subject.
	protected void PositionCamera(float distanceMul, float eyeHeightFrac)
	{
		if (!m_PreviewWorld)
			return;

		// FINAL ROOT CAUSE (2026-07-15, conclusively live-diagnosed via a per-frame live-orbit test): the
		// camera's ROTATION responds correctly to SetCameraEx (a continuously-rotating write visibly swept
		// the view), but its POSITION never moves relative to the subject — something else (very likely
		// native machinery behind CreatePreviewEntity) pins the camera's position near the subject. So
		// camPos here is NOT a real lever any more — it's left at a nominal, roughly-plausible pose purely
		// so the matrix isn't degenerate, but the actual fix is moving the SUBJECT instead (see
		// RebuildPreviewVisual's PREVIEW_SUBJECT_OFFSET) and pointing lookTarget at wherever the subject
		// now actually is, so the camera's (fixed, engine-owned) position still ends up facing it.
		vector camPos = "0 1.6 -2.2";
		camPos[0] = camPos[0] * 1.0;			// placeholder for future left/right tuning
		camPos[1] = 1.6 * eyeHeightFrac / 0.92;	// keep the old constant's rough scale as a starting point
		camPos[2] = -2.2 * distanceMul / 1.22;	// keep the old constant's rough scale as a starting point

		// Look at the SUBJECT's new position (PREVIEW_SUBJECT_OFFSET, applied in RebuildPreviewVisual),
		// not a hardcoded world point — the whole point of moving the subject is for the camera to still
		// be pointed at it.
		vector lookTarget = PREVIEW_SUBJECT_OFFSET;
		lookTarget[1] = camPos[1];

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
		// forward.)
		vector camTransform[4];
		SCR_Math3D.LookAt(camPos, lookTarget, worldUp, camTransform);

		// Commit the shot by writing the isolated world's camera slot DIRECTLY. This single line is the
		// entire point of the isolated-world rewrite: the old code set a transform on a camera ENTITY and
		// hoped the engine would commit it into the slot — which live diagnostics proved it silently never
		// did, in any parenting variant (see the m_PreviewSharedItemRef field comment). SetCameraEx has no
		// entity, no parent, and no activation state in the way; it writes the slot the widget renders.
		//
		// SCR_Math3D.LookAt already produced a full transform (rotation basis + camPos in column 3), which
		// is exactly the `const vector mat[4]` SetCameraEx wants, so it goes straight in. Vanilla builds
		// its own matrix differently (Math3D.AnglesToMatrix + a distance offset along column 2, an orbit
		// camera for a centred item) — our LookAt framing is kept instead, deliberately: the framing math
		// and its tunables were never the bug, and LookAt sidesteps the column/sign convention ambiguity
		// documented above.
		m_PreviewWorld.SetCameraEx(PREVIEW_CAMERA_INDEX, camTransform);

		// BUG FIX (2026-07-15, live-diagnosed): the rendered pane looked STATIC/FROZEN across an entire
		// menu session — camera distance changed by 5-12x (0.8m/4m for a diagnostic item test, 2.2m/10m
		// for the character) with ZERO visible change to the subject's apparent size, while the STUDIO-SET
		// BACKDROP correctly differed between separate menu-open sessions. That split (backdrop differs
		// across sessions, subject never changes within a session even across many camera writes) is only
		// consistent with the widget capturing a render infrequently (e.g. once per SetWorld bind) rather
		// than continuously — the same class of bug this file already hit once for the item grid
		// (PopulateItems' comment: populating via SetColumn/SetRow does not itself trigger a relayout;
		// Widget.Update() was required to force it). Force the SAME kind of explicit refresh here on the
		// RenderTargetWidget itself, not just the world's camera slot, so a real camera change actually
		// gets re-captured instead of assuming SetCameraEx alone triggers a fresh frame.
		if (m_wPreview)
			m_wPreview.Update();

		// Cache the matrix so OnMenuUpdate can re-push it EVERY frame. A one-shot write here is NOT
		// enough: the read-back below proved the slot does not retain it (see m_PreviewCamTransform's
		// field comment, and vanilla's UpdateView which re-writes its own slot every frame).
		m_PreviewCamTransform = camTransform;
		m_bPreviewCamTransformValid = true;

		// DIAGNOSTIC (CameraPositionDiag): a READ-BACK-AFTER-SET check — this exact pattern is what finally
		// exposed the old approach's root cause (transform read back as <0,0,0> after every set), so it is
		// deliberately preserved against the new commit path. GetCamera(index, mat) is the verified BaseWorld
		// counterpart to SetCameraEx.
		//
		// NOTE ON READING THIS LINE: `match=0` here is EXPECTED and not necessarily a failure. Live testing
		// showed the slot reads back <0,0,0> immediately after the write — the isolated world evidently only
		// latches the value on its own render tick, not synchronously inside SetCameraEx. What actually
		// matters is whether the PANE renders the character correctly once the per-frame re-push in
		// OnMenuUpdate is running. Treat a persistently black/wrong pane as the real signal, not this line.
		vector camReadBack[4];
		m_PreviewWorld.GetCamera(PREVIEW_CAMERA_INDEX, camReadBack);
		GRAD_Log.Info(string.Format("CameraPositionDiag: intendedCamPos=%1 slotCamPosAfterSet=%2 match=%3 (match=0 may be benign — see comment)",
			camPos.ToString(), camReadBack[3].ToString(), vector.Distance(camPos, camReadBack[3]) < 0.5));

		// DIAGNOSTIC (see m_iCameraDiagLogCount's field comment): dump the fixed pose actually written,
		// now that it's no longer derived from the subject's transform at all (see this method's ROOT
		// CAUSE comment). Capped to a couple of calls so this doesn't spam every frame.
		if (m_iCameraDiagLogCount < CAMERA_DIAG_LOG_MAX)
		{
			m_iCameraDiagLogCount++;
			GRAD_Log.Info(string.Format("CameraDiag: fixedCamPos=%1 fixedLookTarget=%2 distanceMul=%3 eyeHeightFrac=%4",
				camPos.ToString(), lookTarget.ToString(), distanceMul, eyeHeightFrac));
		}
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

		m_wSubCategoryRow = root.FindAnyWidget(WIDGET_SUBCATEGORY_ROW);
		if (!m_wSubCategoryRow)
			GRAD_Log.Warn(string.Format("ArsenalMenu: sub-category row widget '%1' not found in layout", WIDGET_SUBCATEGORY_ROW));

		m_wFactionRow = root.FindAnyWidget(WIDGET_FACTION_ROW);
		if (!m_wFactionRow)
			GRAD_Log.Warn(string.Format("ArsenalMenu: faction row widget '%1' not found in layout", WIDGET_FACTION_ROW));
		else
			RebuildFactionRow();

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

		// Faction scope: re-apply whatever the faction ROW currently shows as selected, not a fresh
		// "target's own faction" derivation — this method also runs from OnCatalogReady() (see the
		// tab-mask comment just below for the exact same "late rebuild silently desyncs the visible
		// selection" bug class), so re-deriving from the target here would silently override a user's
		// faction pill click with the character's default faction. m_sSelectedFactionKey starts at ""
		// (ALL) per the user's explicit ask to see every faction's gear by default, with the row itself
		// as the way to narrow — not an invisible auto-scope like before.
		m_Browser.SetFactionKey(m_sSelectedFactionKey);

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
		m_iSelectedSubCategory = 0;	// "All" — a sub-selection from the previous tab means nothing here
		if (m_Browser)
			m_Browser.SetCategoryMask(GRAD_ArsenalTabs.MaskFor(tabIndex));

		GRAD_Log.Info(string.Format("SelectTab %1 mask=%2 itemGridFound=%3",
			tabIndex, GRAD_ArsenalTabs.MaskFor(tabIndex), m_wItemList != null));

		RebuildSubCategoryRow(tabIndex);
		FrameForCategory(tabIndex);
		PopulateItems();
	}

	//------------------------------------------------------------------------------------------------
	//! Rebuild the sub-category pill row for the given top tab: one "All" pill (restores the tab's full
	//! mask) plus one pill per constituent arsenal type (GRAD_ArsenalTabs.SubTypesFor). Hidden entirely
	//! when the tab has only one constituent type — a sub-row offering a single choice plus "All" would
	//! be pure noise (e.g. Secondary is Pistols alone).
	protected void RebuildSubCategoryRow(int tabIndex)
	{
		if (!m_wSubCategoryRow)
			return;

		m_aSubCategoryHandlers.Clear();
		m_aSubCategoryButtons.Clear();
		ClearChildren(m_wSubCategoryRow);

		array<int> subTypes = {};
		GRAD_ArsenalTabs.SubTypesFor(tabIndex, subTypes);

		if (subTypes.Count() <= 1)
		{
			m_wSubCategoryRow.SetVisible(false);
			return;
		}

		m_wSubCategoryRow.SetVisible(true);

		CreateSubCategoryPill(0, "All");	// "All" always selected by default (m_iSelectedSubCategory == 0)
		foreach (int subType : subTypes)
			CreateSubCategoryPill(subType, GRAD_ArsenalCategoryLabels.LabelFor(subType));
	}

	//! Sub-category pills read twice as large as the loadout-line buttons ROW_LAYOUT/CreateRow was
	//! originally sized for (per user feedback, "twice as large") — bumped at runtime via
	//! SizeLayoutWidget.SetMinDesiredWidth/Height (verified real API) on the button's own inherited
	//! "SizeLayout" child, plus a larger font on its "Text" child, rather than editing
	//! GRAD_ListButtonRow.layout directly: that layout is also used verbatim elsewhere in this file
	//! (CreateRow's other callers), so a layout-level size change would resize things beyond the
	//! sub-category row too. Doubling here is scoped to sub-category pills only.
	protected const float SUBCATEGORY_PILL_MIN_WIDTH = 160;
	protected const float SUBCATEGORY_PILL_MIN_HEIGHT = 64;
	protected const float SUBCATEGORY_PILL_FONT_SIZE = 24;

	//------------------------------------------------------------------------------------------------
	//! One sub-category pill button. `arsenalType` is 0 for the "All" pill (restores the tab's mask).
	//! Uses SCR_ButtonTextComponent's own built-in toggle state (m_bCanBeToggled/SetToggled) instead of
	//! hand-managing background colors — WLib_ButtonText.layout already defines m_BackgroundSelected as
	//! the same amber used by the top tab strip, so a toggled pill automatically matches the rest of the
	//! UI's "active tab" look with no new color logic.
	protected void CreateSubCategoryPill(int arsenalType, string label)
	{
		Widget pill = CreateRow(m_wSubCategoryRow, label);
		if (!pill)
			return;

		GRAD_SubCategoryHandler handler = new GRAD_SubCategoryHandler(this, arsenalType);
		m_aSubCategoryHandlers.Insert(handler);

		SCR_ButtonTextComponent text = SCR_ButtonTextComponent.FindButtonTextComponent(pill);
		if (text)
		{
			text.SetToggleable(true);
			text.SetToggled(arsenalType == m_iSelectedSubCategory, false);

			TextWidget textW = text.GetTextWidget();
			if (textW)
				textW.SetExactFontSize(SUBCATEGORY_PILL_FONT_SIZE);
		}
		m_aSubCategoryButtons.Insert(text);

		SizeLayoutWidget sizeW = SizeLayoutWidget.Cast(pill.FindAnyWidget("SizeLayout"));
		if (sizeW)
		{
			sizeW.EnableMinDesiredWidth(true);
			sizeW.SetMinDesiredWidth(SUBCATEGORY_PILL_MIN_WIDTH);
			sizeW.EnableMinDesiredHeight(true);
			sizeW.SetMinDesiredHeight(SUBCATEGORY_PILL_MIN_HEIGHT);
		}

		SCR_InputButtonComponent btn = SCR_InputButtonComponent.FindComponent(pill);
		if (btn)
			btn.m_OnActivated.Insert(handler.OnActivated);
	}

	//------------------------------------------------------------------------------------------------
	//! A sub-category pill was clicked: narrow (or, for "All", un-narrow) the item grid within the
	//! CURRENT top tab. `arsenalType` 0 means "All" — reapply the tab's full mask rather than a single
	//! type, matching SelectCategoryByIndex's own default. Also updates the pills' toggle state so
	//! exactly one shows as active (SCR_ButtonTextComponent doesn't do mutual-exclusion on its own — it
	//! only tracks its own toggle state, not siblings'). m_aSubCategoryHandlers/m_aSubCategoryButtons are
	//! parallel arrays (same index = same pill), built together in CreateSubCategoryPill.
	void OnSubCategoryClicked(int arsenalType)
	{
		m_iSelectedSubCategory = arsenalType;

		for (int i = 0; i < m_aSubCategoryButtons.Count(); i++)
		{
			SCR_ButtonTextComponent pillBtn = m_aSubCategoryButtons[i];
			GRAD_SubCategoryHandler pillHandler = m_aSubCategoryHandlers[i];
			if (!pillBtn || !pillHandler)
				continue;

			pillBtn.SetToggled(pillHandler.m_iArsenalType == arsenalType, true);
		}

		if (!m_Browser)
			return;

		if (arsenalType == 0)
			m_Browser.SetCategoryMask(GRAD_ArsenalTabs.MaskFor(m_iSelectedCategory));
		else
			m_Browser.SetCategory(arsenalType);

		PopulateItems();
	}

	//------------------------------------------------------------------------------------------------
	//! Build the faction filter row ONCE (called from SetupCategoryRail, not per-tab): an "ALL" pill
	//! (empty faction key — matches GRAD_ItemBrowser.SetFactionKey's own "blank = all factions") plus one
	//! icon pill per faction FactionManager knows about, each using that faction's own UIInfo icon
	//! (Faction.GetUIInfo().GetIconPath() — verified real: "proto external UIInfo GetUIInfo()" on Faction,
	//! "proto external ResourceName GetIconPath()" on UIInfo, the same accessor vanilla faction-select UI
	//! uses). Never hidden, unlike the sub-category row — there's always at least the ALL pill plus
	//! whatever factions exist. Starts on ALL (m_sSelectedFactionKey defaults to "") per the user's
	//! explicit ask to see every faction's gear at once by default, replacing the previous invisible
	//! auto-scope-to-target's-faction behavior (see RebuildBrowser's own comment) with an explicit,
	//! visible, user-controlled filter.
	protected void RebuildFactionRow()
	{
		if (!m_wFactionRow)
			return;

		m_aFactionHandlers.Clear();
		m_aFactionBorders.Clear();
		ClearChildren(m_wFactionRow);

		CreateFactionPill(string.Empty, "ALL", ResourceName.Empty);

		FactionManager factionMgr = GetGame().GetFactionManager();
		if (!factionMgr)
			return;

		array<Faction> factions = {};
		factionMgr.GetFactionsList(factions);

		foreach (Faction faction : factions)
		{
			if (!faction)
				continue;

			string key = faction.GetFactionKey();
			string name = faction.GetFactionName();
			ResourceName iconPath = ResourceName.Empty;

			UIInfo info = faction.GetUIInfo();
			if (info)
				iconPath = info.GetIconPath();

			CreateFactionPill(key, name, iconPath);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One faction pill: a plain 64x64 icon button (GRAD_FactionPill.layout). `factionKey` empty means
	//! the "ALL" pill; `iconPath` empty (the ALL pill, or a faction with no icon authored) leaves the
	//! icon blank rather than guessing a placeholder texture.
	//!
	//! Recolors its OWN border/bg on selection instead of using SCR_ButtonTextComponent's toggle state
	//! (unlike the sub-category pills) — GRAD_FactionPill.layout deliberately does NOT inherit
	//! WLib_ButtonText.layout: a first attempt that did hit a live GUI error, "Cannot add a child, the
	//! ButtonWidget FactionPillButton does not accept more children" — the inherited Button root already
	//! carries WLib_ButtonText's own single child (SizeLayout) and a plain ButtonWidgetClass only holds
	//! ONE child slot, confirmed by every OTHER WLib_ButtonText-inheriting button in this project
	//! (GRAD_ListQtyRow.layout's ButtonMinus/ButtonPlus, GRAD_ListButtonRow.layout) adding zero extra
	//! children of their own. The fix mirrors GRAD_ItemCard.layout's own CardButton instead: a plain
	//! `style blank` button holding a hand-built OverlayWidget stack (bg + border + icon), same
	//! amber-highlight-on-select pattern already used for the selected item card
	//! (m_wSelectedCardBg.SetColor in OnItemRowClicked).
	protected void CreateFactionPill(string factionKey, string displayName, ResourceName iconPath)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		Widget pill = workspace.CreateWidgets(FACTION_PILL_LAYOUT, m_wFactionRow);
		if (!pill)
			return;

		ImageWidget iconW = ImageWidget.Cast(pill.FindAnyWidget(WIDGET_FACTION_PILL_ICON));
		if (iconW && iconPath != ResourceName.Empty)
			iconW.LoadImageTexture(0, iconPath);

		GRAD_FactionPillHandler handler = new GRAD_FactionPillHandler(this, factionKey);
		m_aFactionHandlers.Insert(handler);

		ImageWidget border = ImageWidget.Cast(pill.FindAnyWidget(WIDGET_FACTION_PILL_BORDER));
		m_aFactionBorders.Insert(border);
		SetFactionPillActive(border, factionKey == m_sSelectedFactionKey);

		SCR_InputButtonComponent btn = SCR_InputButtonComponent.FindComponent(pill);
		if (btn)
			btn.m_OnActivated.Insert(handler.OnActivated);
	}

	//------------------------------------------------------------------------------------------------
	//! Show/hide a faction pill's amber selection border (0 alpha when inactive, matching the layout's
	//! authored default — see GRAD_FactionPill.layout's FactionPillBorder Color).
	protected void SetFactionPillActive(ImageWidget border, bool active)
	{
		if (!border)
			return;

		if (active)
			border.SetColor(new Color(0.761, 0.386, 0.078, 1));
		else
			border.SetColor(new Color(0.761, 0.386, 0.078, 0));
	}

	//------------------------------------------------------------------------------------------------
	//! A faction pill was clicked: re-scope the item browser to that faction ("" = ALL), keep the pill
	//! row in sync (mutual-exclusion, same reasoning as OnSubCategoryClicked), and repopulate. Does NOT
	//! touch the top-tab/sub-category selection — faction is an orthogonal filter layered on top.
	void OnFactionClicked(string factionKey)
	{
		m_sSelectedFactionKey = factionKey;

		for (int i = 0; i < m_aFactionBorders.Count(); i++)
		{
			GRAD_FactionPillHandler pillHandler = m_aFactionHandlers[i];
			if (!pillHandler)
				continue;

			SetFactionPillActive(m_aFactionBorders[i], pillHandler.m_sFactionKey == factionKey);
		}

		if (!m_Browser)
			return;

		m_Browser.SetFactionKey(factionKey);
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
	//! Find the preview's WORN GARMENT ENTITY (not its storage) of the given arsenal-type bit — e.g.
	//! the backpack/vest/uniform item itself, so it can be unequipped. Returns null if not worn.
	protected IEntity FindNamedGarmentOwner(int arsenalTypeBit)
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!m_PreviewCharacter || !service || !service.GetCatalogIndex())
			return null;

		array<ref GRAD_ContainerRef> containers = {};
		GRAD_InventoryLib.CollectDestinationContainers(m_PreviewCharacter, service.GetCatalogIndex(), containers);

		foreach (GRAD_ContainerRef c : containers)
		{
			if (!c || !c.m_Owner)
				continue;

			ResourceName ownerPrefab = GRAD_InventoryLib.GetPrefabResourceName(c.m_Owner);
			int ownerType = service.GetCatalogIndex().GetArsenalTypeForPrefab(ownerPrefab);
			if ((ownerType & arsenalTypeBit) != 0)
				return c.m_Owner;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Find ANY worn/equipped item of the given arsenal-type bit, whether or not it owns a container
	//! storage — unlike FindNamedGarmentOwner (which only finds items CollectDestinationContainers
	//! enumerates, i.e. items with their OWN storage, like a vest or backpack). Trousers (LEGS, 8192)
	//! and other non-container garments (e.g. gloves) have no storage of their own and would never be
	//! found by FindNamedGarmentOwner, so this walks every item on the preview via CollectAllItems
	//! instead (already the primitive RemoveOneFromPreview uses for the same "search everything" need).
	//! Returns null if nothing of that type is currently worn.
	protected IEntity FindNamedEquippedItem(int arsenalTypeBit)
	{
		GRAD_ArsenalService service = GRAD_ArsenalService.GetInstance();
		if (!m_PreviewCharacter || !service || !service.GetCatalogIndex())
			return null;

		array<IEntity> items = {};
		GRAD_InventoryLib.CollectAllItems(m_PreviewCharacter, items);

		foreach (IEntity item : items)
		{
			if (!item)
				continue;

			ResourceName prefab = GRAD_InventoryLib.GetPrefabResourceName(item);
			int itemType = service.GetCatalogIndex().GetArsenalTypeForPrefab(prefab);
			if ((itemType & arsenalTypeBit) != 0)
				return item;
		}

		return null;
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
		if (!m_SelectedRecord)
		{
			GRAD_Log.Warn("ArsenalMenu: ADD TO VEST clicked but no item is selected.");
			return;
		}

		BaseInventoryStorageComponent vest = FindNamedContainer(4096);	// VEST_AND_WAIST
		if (vest)
		{
			AddSelectedToContainer(vest);
		}
		else
		{
			GRAD_Log.Warn(string.Format("ArsenalMenu: ADD TO VEST clicked for '%1' but preview has no vest " +
				"container (FindNamedContainer(4096) found none) — check the loadout panel actually mirrored " +
				"the target's real vest.", m_SelectedRecord.m_sDisplayName));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! ADD TO BACKPACK button: add the selected item into the backpack storage. No-op if no backpack
	//! is worn (the button is dimmed/disabled in that case via SetAddButtonsEnabled — this guard is
	//! only reached on a stray click, or if the preview's mirrored loadout came back empty, e.g. from
	//! the character-falling bug fixed in SetupPreview's spawn-position comment).
	void OnAddToBackpack()
	{
		if (!m_SelectedRecord)
		{
			GRAD_Log.Warn("ArsenalMenu: ADD TO BACKPACK clicked but no item is selected.");
			return;
		}

		BaseInventoryStorageComponent bag = FindNamedContainer(128);	// BACKPACK
		if (bag)
		{
			AddSelectedToContainer(bag);
		}
		else
		{
			GRAD_Log.Warn(string.Format("ArsenalMenu: ADD TO BACKPACK clicked for '%1' but preview has no " +
				"backpack container (FindNamedContainer(128) found none) — check the loadout panel actually " +
				"mirrored the target's real backpack.", m_SelectedRecord.m_sDisplayName));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! EQUIP button: add the selected apparel/weapon (auto-slots to its loadout slot).
	void OnEquipSelected()
	{
		if (!m_SelectedRecord)
		{
			GRAD_Log.Warn("ArsenalMenu: EQUIP clicked but no item is selected.");
			return;
		}

		AddSelectedToContainer(null);	// null = engine auto-slot / equip
	}

	//------------------------------------------------------------------------------------------------
	//! Double-click an item card (see GRAD_ArsenalRowHandler.OnDoubleClick): select it AND immediately
	//! add it to whichever destination fits, bypassing the Selected-Item panel's ADD buttons. Mirrors
	//! the same vest -> backpack -> equip preference a player would reach for manually; logs which path
	//! was taken (or why none were available) the same way the ADD buttons do.
	void OnItemRowDoubleClicked(notnull GRAD_ArsenalItemRecord record)
	{
		m_SelectedRecord = record;
		RefreshSelectedPanel();

		bool stackable = GRAD_ArsenalCategoryLabels.IsStackable(record.m_iArsenalType);

		if (!stackable)
		{
			GRAD_Log.Info(string.Format("ArsenalMenu: double-click '%1' -> EQUIP (not stackable)", record.m_sDisplayName));
			AddSelectedToContainer(null);	// clothing/weapon: auto-slot equip
			return;
		}

		BaseInventoryStorageComponent vest = FindNamedContainer(4096);		// VEST_AND_WAIST
		if (vest)
		{
			GRAD_Log.Info(string.Format("ArsenalMenu: double-click '%1' -> VEST", record.m_sDisplayName));
			AddSelectedToContainer(vest);
			return;
		}

		BaseInventoryStorageComponent backpack = FindNamedContainer(128);	// BACKPACK
		if (backpack)
		{
			GRAD_Log.Info(string.Format("ArsenalMenu: double-click '%1' -> BACKPACK", record.m_sDisplayName));
			AddSelectedToContainer(backpack);
			return;
		}

		BaseInventoryStorageComponent anyWithRoom = FindContainerStorage(GRAD_ContainerTypes.MASK, true);
		if (anyWithRoom)
		{
			GRAD_Log.Info(string.Format("ArsenalMenu: double-click '%1' -> first container with room", record.m_sDisplayName));
			AddSelectedToContainer(anyWithRoom);
			return;
		}

		GRAD_Log.Warn(string.Format("ArsenalMenu: double-click '%1' found NO fitting container (no vest, " +
			"no backpack, no other container with room) — item was only selected, not added. Check the " +
			"loadout panel mirrored the target's real gear.", record.m_sDisplayName));
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
		RefreshTrousersSlot();	// LEGS (8192) — not a container, see RefreshTrousersSlot's own comment
		RefreshHeadgearSlot();	// HEADWEAR (1024) — not a container, same shape as RefreshTrousersSlot
	}

	//------------------------------------------------------------------------------------------------
	//! Refresh the Trousers slot block: unlike Uniform/Vest/Backpack, trousers have no storage of their
	//! own (added 2026-07-15 per user request — "trousers should be removable too, same as headgear,
	//! even if they are not containers"), so there is no fill fraction or contents list, just a binary
	//! worn/not-worn indicator and the REMOVE button (already bound in BindButtons).
	protected void RefreshTrousersSlot()
	{
		Widget root = GetRootWidget();
		if (!root)
			return;

		IEntity trousers = FindNamedEquippedItem(8192);	// LEGS

		Widget bar = root.FindAnyWidget(WIDGET_SLOT_TROUSERS_BAR);
		if (bar)
		{
			float op = 0.25;	// not worn
			if (trousers)
				op = 1.0;	// worn (binary — no fill-fraction concept for a non-container garment)
			bar.SetOpacity(op);
		}

		TextWidget pct = TextWidget.Cast(root.FindAnyWidget(WIDGET_SLOT_TROUSERS_PCT));
		if (pct)
		{
			if (trousers)
				pct.SetText("WORN");
			else
				pct.SetText("[Empty]");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Refresh the Headgear slot block: same non-container shape as RefreshTrousersSlot (worn/not-worn
	//! indicator + REMOVE button, no fill fraction or contents list) — headgear has no storage of its
	//! own either.
	protected void RefreshHeadgearSlot()
	{
		Widget root = GetRootWidget();
		if (!root)
			return;

		IEntity headgear = FindNamedEquippedItem(1024);	// HEADWEAR

		Widget bar = root.FindAnyWidget(WIDGET_SLOT_HEADGEAR_BAR);
		if (bar)
		{
			float op = 0.25;	// not worn
			if (headgear)
				op = 1.0;	// worn (binary — no fill-fraction concept for a non-container garment)
			bar.SetOpacity(op);
		}

		TextWidget pct = TextWidget.Cast(root.FindAnyWidget(WIDGET_SLOT_HEADGEAR_PCT));
		if (pct)
		{
			if (headgear)
				pct.SetText("WORN");
			else
				pct.SetText("[Empty]");
		}
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
	//! REMOVE button on a loadout-panel slot header: unequip the WORN GARMENT ITSELF (uniform/vest/
	//! backpack/trousers), not its contents — distinct from OnLoadoutLineMinus, which only edits what's
	//! INSIDE an already-worn container. No-op (logged) if that slot isn't currently worn.
	void OnUnequipGarment(int arsenalTypeBit)
	{
		if (!m_PreviewCharacter)
			return;

		// FindNamedGarmentOwner only finds items CollectDestinationContainers enumerates (items with
		// their OWN storage — vest, backpack, uniform-with-pockets). Trousers (LEGS, 8192) and other
		// non-container garments have no storage at all and would never be found there, so fall back to
		// the general worn-item search (FindNamedEquippedItem) when the container lookup comes up empty.
		IEntity garment = FindNamedGarmentOwner(arsenalTypeBit);
		if (!garment)
			garment = FindNamedEquippedItem(arsenalTypeBit);

		if (!garment)
		{
			GRAD_Log.Warn(string.Format("ArsenalMenu: REMOVE clicked for arsenal type %1 but nothing is worn there", arsenalTypeBit));
			return;
		}

		ResourceName prefab = GRAD_InventoryLib.GetPrefabResourceName(garment);
		if (prefab == ResourceName.Empty)
			return;

		if (RemoveOneFromPreview(prefab))
			GRAD_Log.Info(string.Format("ArsenalMenu: unequipped '%1'", GRAD_InventoryLib.GetEntityShortName(garment)));

		// Instant refresh: PopulateItems so the item grid's "already owned" state updates, plus the
		// full loadout-panel + selected-panel refresh so [Empty] shows immediately, matching the
		// user's request that this update "instantly."
		PopulateItems();
		RefreshSelectedPanel();
		RefreshLoadoutPanel();
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

		// BUG FIX (live log 2026-07-14: "Capture: 'ArsenalResult' -> 0 nodes" -> "refusing to clear ...
		// EMPTY loadout" -> apply ok=0): the clone is kept Deactivate()d every frame (PinPreviewAlive),
		// and capturing from a deactivated entity walked 0 occupied slots. ApplyToPreview already
		// re-activates the clone for inventory mutations for exactly this reason — do the same around
		// this capture (re-pinning afterward is moot since we Close() right after, but harmless).
		GenericEntity previewGe = GenericEntity.Cast(m_PreviewCharacter);
		if (previewGe)
			previewGe.Activate();

		GRAD_LoadoutData result = GRAD_LoadoutCapture.Capture(m_PreviewCharacter, "ArsenalResult", true);
		PinPreviewAlive();
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

		// Same deactivated-entity capture bug as OnConfirm — activate for the read, re-pin after.
		GenericEntity previewGe = GenericEntity.Cast(m_PreviewCharacter);
		if (previewGe)
			previewGe.Activate();

		GRAD_LoadoutData data = GRAD_LoadoutCapture.Capture(m_PreviewCharacter, "Export", true);
		PinPreviewAlive();
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
		// Tear down the preview character + camera. The networked target is never touched here.

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

		// PREVIEW TEARDOWN — ordering is deliberate (children before parents; this project already had one
		// unexplained native crash around camera teardown):
		//  1. Delete the preview CHARACTER (a real entity we spawned) while its world is still alive.
		//  2. Release the WORLD last, by nulling the refs.
		//
		// DELIBERATELY NOT DONE — unbinding the widget first via m_wPreview.SetWorld(null, ...): that call
		// was written here and then REMOVED. It looks obviously right (the widget renders the world every
		// frame, so releasing the world under it seems like a dangling reference), but NEITHER vanilla
		// implementation of this architecture does it — not SCR_InventoryInspectionUI.DeletePreview(), not
		// PreviewWorld.c's destructor (which only does `delete m_RenderWidget;`). SetWorld's first param is
		// not marked notnull so a null would compile, but "compiles" is not "verified", and passing null to
		// a native render binding that vanilla never passes null to is exactly the sort of unverified guess
		// that has crashed this engine before. The widget is destroyed along with this menu anyway, which is
		// what both vanilla paths rely on. If the next live test crashes on close, THIS is the first thing
		// to reconsider — but reconsider it with evidence, not by re-adding the guess.
		//
		// The studio-set entities are likewise NOT deleted individually: they are owned by the isolated
		// world, and vanilla's own DeletePreview() never touches the entities it spawned — releasing the
		// world is what reclaims them.

		// The clone is ours (SpawnLocal) — delete it directly (no widget-binding release call needed;
		// unlike the old ItemPreviewManagerEntity path, RenderTargetWidget doesn't own the character).
		// SCR_EntityHelper.DeleteEntityAndChildren is `RplComponent.DeleteRplEntity(entity, false)` (verified
		// — that one line is its entire body). Unchanged from the previous architecture: the clone was
		// always a SpawnEntityPrefabLocal entity with no replication, and this call already handled it
		// correctly, so moving it into an isolated world does not change its local, unreplicated nature.
		if (m_PreviewCharacter)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_PreviewCharacter);
			m_PreviewCharacter = null;
		}

		// Drop the visual copy's handle BEFORE the world that owns it, matching vanilla DeletePreview()'s
		// order exactly (entity handle → world pointer → SharedItemRef). It is not deleted explicitly:
		// like the studio-set entities it belongs to the isolated world, and releasing the world reclaims
		// it. Nulling the handle first just guarantees nothing holds a pointer into a released world.
		m_PreviewVisual = null;

		// Release the isolated world. This mirrors vanilla SCR_InventoryInspectionUI.DeletePreview()
		// EXACTLY (real source): it nulls the entity handle, then the world pointer, then the SharedItemRef
		// — and does nothing else. The world is refcounted via the SharedItemRef (that is the entire reason
		// the field is `ref`), so dropping the last ref is what actually frees it and everything inside it.
		// There is no explicit BaseWorld destroy/unload API and none should be guessed at.
		m_PreviewWorld = null;
		m_PreviewSharedItemRef = null;

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

		// MANDATORY per-frame camera slot push. This was briefly removed on the theory that a slot write
		// via SetCameraEx "simply retains its value until the next framing change" — the live read-back
		// diagnostic disproved that outright: CameraPositionDiag reported
		// `slotCamPosAfterSet=<0, 0, 0> match=0` on EVERY framing change, i.e. the slot did not hold the
		// write at all. Vanilla's SCR_InventoryInspectionUI.UpdateView re-writes its slot with
		// SetCameraEx EVERY FRAME (real source, verified) — that was mis-read as being merely incidental
		// to its orbit/zoom lerp animation. It is not incidental: it is how the slot stays set. Our shot
		// is static between framing changes, so we just re-push the last matrix PositionCamera computed.
		if (m_PreviewWorld && m_bPreviewCamTransformValid)
			m_PreviewWorld.SetCameraEx(PREVIEW_CAMERA_INDEX, m_PreviewCamTransform);

		// LIVE ORBIT TEST (2026-07-15, concluded): a per-frame continuously-changing camera transform
		// (orbiting position + LookAt rotation) was written here to test whether SetCameraEx commits at
		// all. Result: the camera's ROTATION visibly responded (the view swept around), but its POSITION
		// never moved relative to the subject — the boot stayed fixed in frame throughout, only the
		// backdrop swept past as the view rotated. Conclusion: SetCameraEx's rotation columns are honored,
		// but the position column (mat[3]) is not — something else (very likely the native preview
		// subsystem behind CreatePreviewEntity) owns/pins the camera's actual position near the subject.
		// See RebuildPreviewVisual/PositionCamera for the resulting fix: reposition the SUBJECT instead of
		// fighting the camera's position.

		// BUG FIX (2026-07-15, see PositionCamera's matching comment): force the RenderTargetWidget itself
		// to refresh every frame too, not just the world's camera slot — the pane looked frozen/static
		// across an entire menu session despite repeated SetCameraEx writes, matching the same "populating
		// a widget doesn't itself trigger a relayout/re-render" class of bug this file already fixed once
		// for the item grid via Widget.Update().
		if (m_wPreview)
			m_wPreview.Update();

		// No per-frame mouse-orbit/zoom handling (SCR_InventoryCharacterWidgetHelper was tied to
		// ItemPreviewWidget/PreviewRenderAttributes, which no longer apply here). Live re-orbiting via
		// mouse drag would need a new input handler driving PositionCamera(), not implemented in this pass.
		// If that is added later, vanilla's UpdateView is the reference: lerp the angles, then re-write the
		// slot with SetCameraEx each frame — which is now exactly what the line above already does.
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
//!
//! Extends ScriptedWidgetEventHandler (verified real base, "attached to any widget using
//! Widget.AddHandler", confirmed real vanilla usage pattern via SCR_FieldManualEntryScriptedWidget
//! EventHandler which does the same thing: a small handler class overriding one event method,
//! constructed with a context reference) so item cards can ALSO react to a double-click — auto-add
//! to a fitting container — without touching the existing SCR_InputButtonComponent.m_OnActivated
//! single-click wiring at all.
class GRAD_ArsenalRowHandler : ScriptedWidgetEventHandler
{
	protected GRAD_ArsenalMenu m_Menu;

	int m_iCategoryIndex = -1;			//!< meaningful when m_bIsCategory (tab index)
	ref GRAD_ArsenalItemRecord m_Record;	//!< meaningful for item cards
	bool m_bIsCategory;					//!< top-tab button
	bool m_bIsGroupHeader;				//!< collapsible base-name group header card
	string m_sGroupKey;					//!< group label this header toggles (when m_bIsGroupHeader)
	Widget m_wRow;						//!< the card/row widget itself, for selection-highlight recolor

	//------------------------------------------------------------------------------------------------
	//! Binds the widget's single SCR_InputButtonComponent to OnActivated, and (for item cards only)
	//! adds this handler to the same button widget for double-click detection.
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
			Widget buttonWidget = rowWidget.FindAnyWidget("CardButton");
			SCR_InputButtonComponent button = SCR_InputButtonComponent.GetInputButtonComponent("CardButton", rowWidget);
			if (button)
				button.m_OnActivated.Insert(OnActivated);

			// Double-click -> auto-add to a fitting container (see OnDoubleClick). Only meaningful for
			// item cards (m_bIsCategory/m_bIsGroupHeader rows don't have a record to add); the handler
			// itself checks that at fire-time since these bool fields are set by the caller AFTER this
			// constructor runs (see CreateItemCard/CreateGroupHeaderCard) — cheap enough to just add it
			// unconditionally and let OnDoubleClick's own guard filter it, rather than deferring the
			// AddHandler call to a second setter method.
			if (buttonWidget)
				buttonWidget.AddHandler(this);
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

	//------------------------------------------------------------------------------------------------
	//! Double-click on an item card: auto-add straight to whichever container fits, bypassing the
	//! Selected-Item panel's ADD buttons entirely. No-op for category tabs / group headers (nothing
	//! to add) and for the same "no record" case OnActivated already tolerates.
	override bool OnDoubleClick(Widget w, int x, int y, int button)
	{
		if (!m_Menu || m_bIsCategory || m_bIsGroupHeader || !m_Record)
			return false;

		m_Menu.OnItemRowDoubleClicked(m_Record);
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! One loadout-panel slot header's REMOVE-garment bridge. Carries the arsenal-type bit
//! (2048 uniform / 4096 vest / 128 backpack) so the parameterless invoker callback can route back
//! into the menu with the right slot. One instance per button, created once in BindButtons and kept
//! alive for the menu's whole lifetime (unlike GRAD_LoadoutLineHandler, these buttons are never
//! rebuilt, so there is no per-refresh churn to manage).
class GRAD_UnequipHandler
{
	protected GRAD_ArsenalMenu m_Menu;
	protected int m_iArsenalTypeBit;

	//------------------------------------------------------------------------------------------------
	void GRAD_UnequipHandler(GRAD_ArsenalMenu menu, int arsenalTypeBit)
	{
		m_Menu = menu;
		m_iArsenalTypeBit = arsenalTypeBit;
	}

	//------------------------------------------------------------------------------------------------
	void OnActivated()
	{
		if (m_Menu)
			m_Menu.OnUnequipGarment(m_iArsenalTypeBit);
	}
}

//------------------------------------------------------------------------------------------------
//! One sub-category pill's bridge. Carries the arsenal-type bit the pill narrows to (0 = "All", the
//! tab's full mask). Rebuilt on every top-tab switch (RebuildSubCategoryRow), kept alive in
//! m_aSubCategoryHandlers — same per-refresh-churn shape as GRAD_LoadoutLineHandler below, not the
//! built-once GRAD_UnequipHandler above (each tab has a different set of pills).
class GRAD_SubCategoryHandler
{
	protected GRAD_ArsenalMenu m_Menu;
	int m_iArsenalType;	// public: the menu reads this back to match a button to its type for toggle sync

	//------------------------------------------------------------------------------------------------
	void GRAD_SubCategoryHandler(GRAD_ArsenalMenu menu, int arsenalType)
	{
		m_Menu = menu;
		m_iArsenalType = arsenalType;
	}

	//------------------------------------------------------------------------------------------------
	void OnActivated()
	{
		if (m_Menu)
			m_Menu.OnSubCategoryClicked(m_iArsenalType);
	}
}

//------------------------------------------------------------------------------------------------
//! One faction pill's bridge. Carries the faction key the pill filters to ("" = ALL). Same
//! per-refresh-churn shape as GRAD_SubCategoryHandler above (rebuilt whenever the row is rebuilt, kept
//! alive in m_aFactionHandlers).
class GRAD_FactionPillHandler
{
	protected GRAD_ArsenalMenu m_Menu;
	string m_sFactionKey;	// public: the menu reads this back to match a button to its key for toggle sync

	//------------------------------------------------------------------------------------------------
	void GRAD_FactionPillHandler(GRAD_ArsenalMenu menu, string factionKey)
	{
		m_Menu = menu;
		m_sFactionKey = factionKey;
	}

	//------------------------------------------------------------------------------------------------
	void OnActivated()
	{
		if (m_Menu)
			m_Menu.OnFactionClicked(m_sFactionKey);
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

	//------------------------------------------------------------------------------------------------
	//! Decompose a tab's mask into its individual constituent bits — the sub-tab pills shown under the
	//! top tab strip (e.g. Apparel -> Headgear/Jackets/Vests/Trousers/Footwear/Gloves). Bit positions
	//! 0..22 cover the full SCR_EArsenalItemType range this file already maps in
	//! GRAD_ArsenalCategoryLabels; only bits actually present in the mask are returned, in ascending
	//! order, so the pill row always matches what LabelFor/MaskFor already agree the tab contains — one
	//! source of truth (the hand-built masks above), no separate per-tab list to keep in sync.
	static void SubTypesFor(int tabIndex, out notnull array<int> outTypes)
	{
		outTypes.Clear();

		int mask = MaskFor(tabIndex);
		for (int bit = 0; bit <= 22; bit++)
		{
			int value = 1 << bit;
			if ((mask & value) != 0)
				outTypes.Insert(value);
		}
	}
}
