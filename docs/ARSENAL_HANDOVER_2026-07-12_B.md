# Arsenal Menu — Handover for Opus (2026-07-12, session B)

Continuation of `docs/ARSENAL_HANDOVER_2026-07-12.md` (that doc is now stale on the specific bugs it
described as open — the item grid rendering issue it left unresolved is FIXED; read this doc instead
for current status). This session made major progress (real 3D item thumbnails, real camera-based
character preview, scrollable/correctly-sized item grid) but ends with two serious open bugs.

## Confirmed WORKING (do not re-litigate)

- **Tab strip renders and highlights correctly** — visually, clicking/paging between Primary / Secondary
  / Throwables / Apparel / Container correctly moves the amber highlight. This is `SCR_TabViewComponent`
  in `UI/Layouts/GRAD_ArsenalMenu.layout` (`CategoryTabView` widget, instances vanilla
  `WLib_TabViewCoreMenus.layout`), fully vanilla-driven, no script painting.
- **Item grid**: `CategoryItems` (`UniformGridLayoutWidgetClass`, NOT `GridLayoutWidgetClass` — this
  matters, see "Item grid architecture" below) wrapped in a `ScrollLayoutWidgetClass "CategoryItemsScroll"`
  in the same layout file. Renders real, correctly-sized, non-overlapping tiles that scroll. Confirmed
  live via screenshot this session.
- **Item thumbnails**: `UI/Layouts/GRAD_ItemCard.layout`'s `CardIcon` is `ItemPreviewWidgetClass` (not a
  flat `ImageWidgetClass`/`SCR_UIInfo` icon). `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c`'s
  `CreateItemCardWidget` calls `m_PreviewManager.SetPreviewItemFromPrefab(iconW, prefab, null, false)` —
  renders the item's real 3D model as its thumbnail. Confirmed live: real gun/ammo/gear models rendering
  correctly in the grid. Group-header cards (multi-variant, e.g. "M16A2" family) show the first
  alphabetical variant's model as a representative thumbnail (`CreateGroupHeaderCard`).
- **Loadout panel** (Uniform/Vest/Backpack contents, +/- quantity controls): confirmed working, including
  a fix this session for duplicate same-prefab lines (now grouped by prefab with a correct per-container
  count) and a fix for `TryRemoveItemFromInventory` failures (now passes the specific storage explicitly).
- **Import/Export** (clipboard JSON): implemented, reviewed, not yet live-tested this session (was
  working per an earlier handover; no reason to believe it regressed, but not re-verified today).
- **Camera-based character preview architecture is sound and does not crash.** This is a major rewrite
  from the old `ItemPreviewWidget`/`PreviewRenderAttributes` approach (which had zero distance-control
  API and could not be zoomed out — confirmed via `mcp__enfusion-mcp__api_search`, exhaustively, this
  session). The new approach: `PaneCenterPreview` is now `RenderTargetWidgetClass` (was
  `ItemPreviewWidgetClass`). `SetupPreview()` in `GRAD_ArsenalMenu.c` spawns a real camera entity
  (`SpawnPreviewCamera()`, prefab `{127C64F4E93A82BC}Prefabs/Editor/Camera/ManualCameraPhoto.et` — a
  vanilla `SCR_ManualCamera`), positions it via `PositionCamera(distance, eyeHeight)` using
  `SCR_Math3D.LookAt(camPos, lookTarget, worldUp, camTransform)` (verified real API — a purpose-built
  look-at helper, chosen specifically because it sidesteps an undocumented column/sign convention issue
  in the raw `Math3D.DirectionAndUpMatrix` that was tried first and caused a "camera pointing straight
  down at the ground" bug), then binds via `BindRenderTarget()` →
  `m_wPreview.SetWorld(world, camBase.GetCameraIndex())` (also verified real, corroborated by vanilla
  `SCR_2DPIPSightsComponent`'s identical pattern). **No crash, no full-screen camera hijack** (there was
  a real risk of this since `SCR_ManualCamera`'s constructor calls `CameraManager.SetCamera(this)`
  internally when a CameraManager is present — mitigated by calling `SwitchToPreviousCamera()`
  immediately after spawn, confirmed this does NOT cause a screen flash/hijack in the live test).

## OPEN BUG 1 (HIGH PRIORITY): Tab selection tracking is completely frozen

**Symptom**: Clicking a tab (mouse) or pressing Q/E does visually move the tab strip's highlight, but
`SCR_TabViewComponent.GetShownTab()` NEVER changes value — confirmed via a diagnostic log line
(`PollTabChange: raw GetShownTab()=X`, in `PollTabChange()`, `GRAD_ArsenalMenu.c` — logs on every value
change) that fired exactly ONCE in an entire test session, reading `-1` right as the menu closed, and
otherwise never fired despite the user directly testing both mouse clicks and E presses. **User has
explicitly confirmed**: a direct mouse click on a tab button does nothing (not just Q/E). This means the
item grid's category filter (`m_Browser.SetCategoryMask(...)`, driven by `SelectCategoryByIndex`, which
is only ever called from `PollTabChange`) never updates — the grid always shows Primary's items
regardless of which tab is visually selected.

### What's been ruled out this session
- **Not a `PollTabChange` bug** — confirmed by direct code reading: it's a passive `GetShownTab()` vs
  cached-value int-compare, correctly called every frame from `OnMenuUpdate()` (verified the call site
  exists and isn't dead code).
- **Not `SetListenToActions(false)`** — this call was added mid-session as a (wrong) theory for a
  DIFFERENT bug (E advancing 2 tabs at once — see Bug 2 below), then reverted after the user reported the
  frozen-`GetShownTab()` symptom persisted regardless of whether that line was present or absent. **Do
  not re-add `SetListenToActions(false)` as a fix attempt for Bug 1** — it's unrelated, already tested
  both ways.
- **Not `m_TabView` being null** — the `GRAD_Log.Warn("ArsenalMenu: CategoryTabView / SCR_TabViewComponent
  not found")` warning does NOT appear in the logs, meaning `SetupCategoryRail()`'s
  `tabViewWidget.FindHandler(SCR_TabViewComponent)` call is successfully resolving SOME
  `SCR_TabViewComponent` instance.
- **Not a `RebuildBrowser` mask-reset race** — a real bug WAS found and fixed here (the catalog index
  builds amortized over several frames and can finish after the menu opens; `RebuildBrowser` used to
  unconditionally force tab 0's mask when this happened, even if the user had already switched tabs —
  fixed to preserve `m_iSelectedCategory` instead). This fix is good and should stay, but it does NOT
  explain Bug 1 (the user confirmed items never change on ANY tab switch, not just after a delayed
  catalog-ready callback).

### Leading hypothesis, NOT YET INVESTIGATED — start here
`FindHandler(SCR_TabViewComponent)` is called on `tabViewWidget` (`root.FindAnyWidget(WIDGET_CATEGORY_TABVIEW)`,
i.e. the widget literally named `"CategoryTabView"`). The visual highlight moving correctly proves SOME
`SCR_TabViewComponent` instance IS processing input and updating internal state — but if `FindHandler`
searches differently than assumed (e.g. only checks components DIRECTLY on that exact widget, and the
real "live" `SCR_TabViewComponent` handling clicks is actually attached to a DIFFERENT widget in the
`WLib_TabViewCoreMenus.layout` subtree — e.g. a nested child widget, given that library layout has its
own internal structure we haven't fully inspected because it's a vanilla asset not present in this repo
— clean-room constraint, never read/copy it, but INSPECTING the live runtime widget tree via Workbench
MCP tools like `wb_entity_inspect` is fair game and has NOT been tried this session), then
`m_TabView` in our script could be a "zombie" component instance that exists but never receives the
actual click/action events, while a DIFFERENT instance (that we never touch) is the one actually
processing input and updating the visual highlight. **This would perfectly explain**: highlight moves
(the real component works fine internally) + `GetShownTab()` on OUR handle never changes (we're reading
the wrong instance).

**Recommended next step**: use Workbench MCP tools (`mcp__enfusion-mcp__wb_entity_inspect`,
`wb_entity_list`, or similar — check what's available) to inspect the LIVE widget tree at runt6ime while
the arsenal menu is open, specifically to see: (a) how many `SCR_TabViewComponent` instances exist in
the `CategoryTabView` subtree, (b) which widget each is attached to, (c) whether `FindAnyWidget("CategoryTabView")`
is even finding the widget you'd expect given `WLib_TabViewCoreMenus.layout`'s actual internal structure.
Do this BEFORE trying more code changes — this bug has already absorbed multiple wrong-theory fix
attempts (`SetListenToActions`, the `RebuildBrowser` race) that didn't address the real cause.

## OPEN BUG 2 (lower priority than Bug 1, same subsystem): Q/E advances the tab TWO steps per press

Reported consistently across the whole session, never fixed. Given Bug 1's discovery that `GetShownTab()`
tracking is fundamentally broken, **Bug 2 cannot be reliably diagnosed until Bug 1 is fixed** — "does it
advance 2 steps" is currently unobservable via script (since our `GetShownTab()` reads are frozen); the
"2 steps" observation has only ever been made by the user watching the VISUAL highlight, which is driven
by whatever the real, correctly-functioning `SCR_TabViewComponent` instance is doing internally
(possibly the same "wrong instance" issue from Bug 1 — if our `m_sActionLeft`/`m_sActionRight` settings
in the layout, or our `SetListenToActions` calls, are being applied to a DIFFERENT component instance
than the one actually handling E/Q, that would explain why nothing we've tried on `m_TabView` has changed
this symptom at all). **Fix Bug 1 first; Bug 2 may turn out to be the same root cause or may resolve
as a side effect.**

## OPEN BUG 3: Character preview renders nothing — fully transparent, GM editor shows through

**User's exact words**: "the preview btw is not working either, its just rendering nothing and the
normal gamemaster interface shines through. this is the misleading part of the screenshot. the GUI of
the middle part is completely transparent, so nothing renders at all."

This means `PaneCenterPreview` (`RenderTargetWidgetClass`) is not rendering ANYTHING — not even a black
frame, it's transparent, so whatever is behind the menu (the GM editor's own 3D view) shows through. This
is NOT a camera-positioning/framing problem (the `SCR_Math3D.LookAt` fix from earlier this session may
well be correct — it can't be verified either way because the widget isn't rendering AT ALL). Log
confirms `BindRenderTarget()` DOES run and DOES log success: `ArsenalMenu: bound preview RenderTargetWidget
to camera index 0`. **"camera index 0" is suspicious** — 0 is very often a default/sentinel/main-camera
value in engines with camera-index systems, not a freshly-spawned secondary camera's real distinct index.
This suggests `camBase.GetCameraIndex()` may be returning 0 for EVERY camera (e.g. if it's not actually
registered into whatever list assigns indices, because we deliberately avoid calling
`CameraManager.SetCamera()` on it to prevent hijacking the main view — but that avoidance may ALSO mean
it never gets a real, non-zero index assigned, if index assignment only happens as a side effect of
`SetCamera`/registration that we're intentionally skipping).

### Leading hypothesis, NOT YET INVESTIGATED
`GetCameraIndex()`'s real semantics were inferred from a vanilla PIP-sight component using the same
METHOD NAME pattern, but never confirmed to require prior camera registration into a `CameraManager`
before returning a meaningful (non-zero/non-default) value. If `RenderTargetWidget.SetWorld(world, 0)`
is literally rendering "whatever the main/default camera 0 sees" (which, if that's the GM editor's own
free-cam, would explain "editor GUI/view shows through" exactly), then our spawned camera was NEVER
actually bound — `SetWorld` silently succeeded with a meaningless index. **Recommended next step**:
verify whether a camera needs to be registered (e.g. via some `CameraManager` list-add call that ISN'T
`SetCamera`, if one exists — search `mcp__enfusion-mcp__api_search` for `CameraManager`'s FULL method
list again, specifically anything like "RegisterCamera"/"AddCamera" distinct from `SetCamera`/
`SetOverlayCamera`) before `GetCameraIndex()` returns a real per-camera index rather than a shared
default. Alternatively: log `camBase.GetCameraIndex()` at a few different points (immediately after
spawn, after `SetWorldTransform`, after `SwitchToPreviousCamera()`) to see if the index ever changes
from 0 to something else at some point in the lifecycle — if it's ALWAYS 0 no matter what, that's strong
evidence 0 is a "never assigned"/default sentinel, not a real handle to our camera.

## Architecture notes (do not re-derive)

### Item grid architecture
`CategoryItems` MUST be `UniformGridLayoutWidgetClass`, not `GridLayoutWidgetClass`. This was a real,
diagnosed bug this session: `GridLayoutWidget.SetRowFillWeight` divides the grid's TOTAL available height
evenly across every weighted row with no scroll/grow mechanism — measured live at 66 rows in a ~1086px
grid, each row got ~16px vs. the tile's real 180px height, squashing every card into an invisible sliver.
`UniformGridLayoutWidget` has no fill-weight API at all (cells size from content), and `CategoryItems` is
now wrapped in `ScrollLayoutWidgetClass "CategoryItemsScroll"` so it can grow past the pane's fixed
height. Runtime code uses `UniformGridSlot.SetColumn`/`SetRow` (not `GridSlot` — that's the wrong slot
type for this widget class). Do not revert this to `GridLayoutWidgetClass`.

### Static `.layout` files do not accept every runtime slot-class name
`Slot GridSlot { ... }` (a real RUNTIME class, `GridSlot.SetColumn`/`SetRow` used in script) is NOT a
valid STATIC layout-file slot token — Workbench either silently strips it or throws
`GUI (E): Unknown class 'GridSlot'` depending on context. This is the same class of gotcha as the
previously-documented `ScrollLayoutSlot` trap (`docs/ENFUSION_LAYOUT_NOTES.md`). If you need runtime grid
placement, do it via the script-side static setter calls (`UniformGridSlot.SetColumn(widget, col)`) after
`CreateWidgets`, never as a static `Slot` block in the `.layout` file itself.

### `ItemPreviewWidget` (flat icon/character preview) has NO distance-control API
Exhaustively verified this session (multiple agents, multiple direct lookups):
`PreviewRenderAttributes`/`SCR_CharacterInventoryPreviewAttributes` expose exactly 3 methods —
`RotateItemCamera`, `ResetDeltaRotation`, `ZoomCamera` (a RELATIVE FOV increment, clamped, max 120°) — no
distance/pivot/offset control whatsoever. This is why the character preview was rewritten to use a real
spawned camera + `RenderTargetWidget` instead (see "Confirmed WORKING" above). Item-grid CARD thumbnails
still correctly use `ItemPreviewWidget`/`SetPreviewItemFromPrefab` — that auto-framed path works fine for
small item icons (verified live, real 3D models render correctly at card size) and should NOT be changed
to the camera approach; only the CENTER CHARACTER preview needed the rewrite, because
`SetPreviewItemFromPrefab`'s auto-framed path was separately confirmed (in an earlier session) to render
BLANK for a bare character prefab with no inventory yet applied.

### `SCR_Math3D.LookAt` is the correct look-at helper, not `Math3D.DirectionAndUpMatrix`
Verified real: `static void LookAt (vector source, vector destination, vector up, out vector rotMat[4])`
— "Returns a rotation matrix that makes object positioned at source position face the point at
destination." Use this for any future camera-aiming code in this project; the raw `Math3D.DirectionAndUpMatrix`
does not document its output column/sign convention and produced a real "camera pointing straight down"
bug when used naively.

### `SCR_ManualCamera` (`{127C64F4E93A82BC}Prefabs/Editor/Camera/ManualCameraPhoto.et`) is safe to spawn
standalone (confirmed via its full pasted vanilla source this session): its constructor explicitly
tolerates a missing/null `CameraManager` ("camera manager is not required, so the camera can be used for
quick debugging in test worlds"). Its `EOnInit()` DOES call `m_CameraManager.SetCamera(this)` internally
when a CameraManager IS present, which would hijack the whole screen — mitigated in this codebase by
calling `SwitchToPreviousCamera()` (a real method on that class) immediately after spawn. This is already
implemented in `SpawnPreviewCamera()` and confirmed NOT to cause a screen-hijack in the live test — do not
remove that call.

## Files touched this session
- `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c` — extensively edited: item grid widget-type swap
  support, item thumbnail rendering, loadout-panel dedup/removal fixes, `RebuildBrowser` tab-preservation
  fix, full character-preview camera rewrite (`SpawnPreviewCamera`, `PositionCamera`, `BindRenderTarget`,
  `FrameFullBody`, `FrameForCategory`), `SetListenToActions` added then reverted, diagnostic logging in
  `PollTabChange` (still present, useful, do not remove until Bug 1 is fixed).
- `UI/Layouts/GRAD_ArsenalMenu.layout` — `CategoryItems` → `UniformGridLayoutWidgetClass` +
  `ScrollLayoutWidgetClass "CategoryItemsScroll"` wrapper; `PaneCenterPreview` →
  `RenderTargetWidgetClass`; `m_fTabWidth`/`m_fTabWidthTextHidden` added to `SCR_TabViewComponent` block
  (narrows tabs so all 5 fit without clipping — unverified live whether this actually helped, was not the
  focus of the last few test rounds).
- `UI/Layouts/GRAD_ItemCard.layout` — `CardIcon` → `ItemPreviewWidgetClass` (was `ImageWidgetClass`);
  restructured root so a `SizeLayoutWidgetClass` wraps the interactive `ButtonWidgetClass` (was the
  reverse nesting, which caused a `GridLayoutWidget`-row-stretch bug — now moot since the grid uses
  `UniformGridLayoutWidgetClass`, but the corrected nesting is still the right structure, don't revert).

## Suggested priority order for next session
1. **Bug 1** (frozen tab tracking) — blocks everything else in the tab/grid-filter area, including
   properly diagnosing Bug 2. Use live Workbench widget-tree inspection, not more code guessing.
2. **Bug 3** (transparent preview) — investigate the `GetCameraIndex()`-returns-0 lead.
3. **Bug 2** (Q/E double-advance) — revisit only after Bug 1 is fixed, may resolve as a side effect.
