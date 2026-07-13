# Plan: fix frozen tab tracking (Bug 1/2) + transparent character preview (Bug 3)

**For: Sonnet, executing session.** Written 2026-07-13 by Fable after a verified-source research pass.
Companion doc with full bug history: `docs/ARSENAL_HANDOVER_2026-07-12_B.md`.

## STATUS UPDATE 2026-07-13 (post-implementation, live-tested)

Both fixes below were implemented and live-tested. Results:

- **Tabs: FULLY FIXED.** Log confirmed the exact predicted mechanism: `TabView handler #0
  GetShownTab()=-1` (zombie), `TabView handler #1 GetShownTab()=0` (live). `PollTabChange` now
  fires on every click/Q/E, grid re-filters correctly, tab advance is 1-per-press. `'Tabs'
  container holds 5 tab buttons` — confirms it's a dataless zombie component, NOT a duplicated
  button row, so no follow-up `SetTabVisible` hiding step was needed.
- **Camera: slot binding fixed, but found a SECOND bug — character spawns in the ocean.**
  `world.SetCameraEx`/`SetWorld(world, 7)` worked (log: `preview bound to world camera slot 7`,
  preview is no longer transparent) — but the shot showed open water/horizon, not the character.
  Root cause: `GRAD_InventoryLib.SpawnLocal(prefab)` (`GRAD_InventoryLib.c:492`) defaults
  `position = vector.Zero` (world origin). This was harmless under the OLD `ItemPreviewWidget`
  render path (an isolated preview space, not the real world) but now that the preview camera
  renders the ACTUAL world, world origin lands in open ocean on this map. **Fix applied**
  (`SetupPreview`, `GRAD_ArsenalMenu.c`): spawn the preview character 500m above the primary
  target's `GetOrigin()` instead of at world origin — clears all terrain/water, keeps it away from
  other players/AI, and is directly above solid ground on any map. `PositionCamera`'s math is
  entity-relative so this needed no other changes. **NOT YET RE-TESTED LIVE** — do this first.

## STATUS UPDATE 2 2026-07-13 (double-advance root-caused; 500m-spawn caused a THIRD bug; grid/backpack)

- **Q/E double-advance: root cause was NOT duplicate SCR_TabViewComponent instances.** Live log
  showed only ONE live handler (`#1`, `#0` was the dataless zombie with 0 buttons) — so the
  "mute the second live instance" branch never even fires; there was nothing to mute. The real
  mechanism (confirmed via vanilla `SCR_PagingButtonComponent` source, arexplorer.zeroy.com):
  `SetAction()` registers BOTH an `EActionTrigger.DOWN` action-listener (`OnMenuSelect`) AND the
  button's own `OnClick` — both invoke the identical `m_OnActivated.Invoke`, so one E/Q press can
  drive `SCR_TabViewComponent.OnTabRight()`/`OnTabLeft()` twice, synchronously, before our next
  `OnMenuUpdate` poll ever runs (so `GetShownTab()` already reflects the SECOND jump the first
  time we ever read it — no intermediate value to compare against). This is vanilla script we
  don't own; `SetListenToActions` only gates the component's OWN raw-action listener, a separate,
  unrelated path, so it can't fix this. **Fix applied**: time-based debounce in `PollTabChange`
  (new fields `m_iPendingShownTab`/`m_fPendingShownTabAge`/`PAGING_DEBOUNCE_SECONDS = 0.12`) —
  collapse any run of `GetShownTab()` changes landing within 0.12s into one reaction, using
  whichever value the run settles on. `PollTabChange` now takes `tDelta` (passed from
  `OnMenuUpdate`). **NOT YET RE-TESTED LIVE.**

- **500m-above spawn caused a NEW bug: character free-falls under gravity/ragdoll for ~10s and
  lands wherever, clipped into terrain/a structure** (live screenshot showed the underside of
  wooden beams, extreme close-up). A freshly spawned `ChimeraCharacter` is not physics-frozen and
  there is no verified freeze/kinematic-disable API in the indexed script API. Likely the SAME
  root cause as a second live report: the loadout panel showed Uniform/Backpack/Vest all
  `[Empty]` despite the real target having gear — user confirmed this is a real mirroring bug, not
  an empty test character. Falling/ragdoll state very plausibly disrupts the character's storage
  component resolution before `RefreshLoadoutPanel` reads it. **Fix applied**: stopped spawning
  in the air entirely — spawn at the SAME ground level as the primary target, offset 5m along the
  target's own right axis (`primaryTransform[0]`, confirmed right-column per the matrix-layout doc
  string already cross-checked elsewhere in this file) so the clone doesn't overlap the real
  player. **NOT YET RE-TESTED LIVE** — this should fix both the camera framing AND the empty-gear
  report as one root cause, but that's a hypothesis, not confirmed.

- **Grid wasted horizontal space + items only appearing after scroll — both fixed, NOT YET
  RE-TESTED LIVE.** Two independent causes:
  1. The three top-level panes (`PaneLeftCategories`/`PaneCenterPreview`/`PaneRightContainer`) are
     all bare `SizeMode Fill` in the layout, splitting width evenly (1/3 each); with fixed-220px
     cards and `GRID_COLUMNS=2`, that left roughly a third of the grid pane empty. Fixed at
     runtime in `SetupCategoryRail` via the verified `LayoutSlot.SetFillWeight(widget, weight)`
     static (distinct from the already-documented `GridLayoutWidget` row/column fill-weight trap —
     this is a separate, safe, proportional-share API for `HorizontalLayoutWidget` children):
     categories pane gets weight 1.6 vs. 1.0 for the other two. `GRID_COLUMNS` bumped 2→3 to use
     the reclaimed width.
  2. Populating the grid via `UniformGridSlot.SetColumn/SetRow` never itself triggers a relayout —
     confirmed `Widget.Update()` ("proto external void Update()") is real on the base `Widget`
     interface (also exposed on `ScrollLayoutWidget`) and is the missing call; added at the end of
     `PopulateItems()` on both `m_wItemList` and its `ScrollLayoutWidget` parent.

- **"ADD TO BACKPACK doesn't work"**: NOT actually a button-wiring bug — `SetAddButtonsEnabled`
  correctly disables it when `FindNamedContainer(128)` (BACKPACK) finds nothing, and the reported
  screenshot showed `BACKPACK [Empty]` in the loadout panel. This is downstream of the empty-gear
  mirroring bug above, not a separate defect. Added a `GRAD_Log.Warn` in `OnAddToBackpack`'s
  no-container branch so a recurrence is diagnosable instead of silently doing nothing. **Verify
  after the ground-level-spawn fix**: if gear mirrors correctly now, this should resolve itself
  with no further changes; if the loadout panel is still empty even standing on solid ground, the
  mirroring bug is unrelated to falling and needs fresh investigation (check
  `GRAD_LoadoutCapture.Capture`/`GRAD_LoadoutApply.Apply` directly, not this file).

## Suggested next-session verification order

1. Reimport, open menu. Confirm preview character is standing normally (not in water/underground/
   inside geometry) and the loadout panel shows the real gear (Uniform/Vest/Backpack populated).
2. Q/E a bunch of times fast — confirm exactly one tab step per press now (was double before).
3. Confirm the grid: 3 columns, no big dead-space gap on the right, cards visible immediately on
   tab switch without needing to scroll first.
4. Select a stackable (e.g. a grenade) and click ADD TO BACKPACK — should actually add it if a
   backpack is worn. If the button is still disabled/no-op, check the new warn log line and the
   loadout panel's Backpack section.

## Context

Two open bugs in `GRAD_ArsenalMenu`:

1. **Tabs**: `m_TabView.GetShownTab()` permanently returns `-1` — clicking tabs and Q/E never
   change it, even though the visual highlight moves. Grid filtering is therefore dead. Q/E also
   visually advances TWO tabs per press.
2. **Preview**: the center `RenderTargetWidget` (`PaneCenterPreview`) renders nothing — fully
   transparent, GM background shines through. The bind log showed `camera index 0`.

Both root causes are now understood, backed by **actual vanilla method bodies** read from
https://arexplorer.zeroy.com (full Reforger script source mirror — use it whenever you need a
vanilla method BODY; the local `mcp__enfusion-mcp__api_search` only has signatures, and
`game_read`/`game_browse` currently cannot read the game paks at all).

---

## Bug 1+2 — duplicate `SCR_TabViewComponent` instances (verified mechanics)

### What the vanilla source proves (read from arexplorer, verbatim bodies)

- `GetShownTab()` is literally `return m_iSelectedTab;`.
- `Init()` (called from `HandlerAttached`) resolves `"Tabs"`, `"HorizontalLayout0"`,
  `"ContentOverlay"`, `"PagingLeft"`, `"PagingRight"` **by name via `m_wRoot.FindAnyWidget`**,
  creates one button per `m_aElements` entry into the shared `"Tabs"` container, then does
  `m_iSelectedTab = -1; ShowTab(realSelected, true, false);`.
- `ShowTab(i, ...)` validates `if (i < 0 || i >= m_aElements.Count() || !m_aElements[i].m_bEnabled) return;`
  **before** setting `m_iSelectedTab = i`. → An instance with an **empty `m_aElements`** fails every
  `ShowTab` forever and its `GetShownTab()` is stuck at `-1`. That is exactly our observed symptom.
- Tab clicks are routed per-instance: `CreateTab` does `comp.m_OnClicked.Insert(OnSelection)` on the
  buttons **that instance** created. A zombie instance never learns about clicks on another
  instance's buttons.
- `AddActionListeners()` subscribes `OnTabLeft/OnTabRight` to the **shared** paging buttons'
  `m_OnActivated` invoker (buttons found by name, so *every* instance on the widget subscribes to
  the *same two buttons*). Any instance with a non-empty `m_aElements` advances its own tab state on
  each Q/E press — two live instances ⇒ **double-advance**.

### Why we hold the wrong instance

`UI/Layouts/GRAD_ArsenalMenu.layout:50` imports the vanilla layout
(`VerticalLayoutWidgetClass "{2512F4AB6CC30301}" : "{D1CAF877446C66DE}UI/layouts/WidgetLibrary/TabView/WLib_TabViewCoreMenus.layout"`)
and our hand-written `components { SCR_TabViewComponent "{2512F4AB6CC303A8}" { ... } }` block uses a
**new instance GUID**, which (per the observed behavior) **adds a second component** instead of
overriding the inherited one. `Widget.FindHandler(typename)` is documented as "return **first**
handler of given type" — and it returns the inherited zombie, not ours.
(`GRAD_ArsenalMenu.c:629` in `SetupCategoryRail`.)

We could NOT read the vanilla `WLib_TabViewCoreMenus.layout` to count how many component instances
the base ships with (paks unreadable), so the fix below is written to be correct under every
instance count, and it logs a decisive diagnostic in the same pass.

### Fix (script-side, `SetupCategoryRail`, `GRAD_ArsenalMenu.c:625-644`)

Replace the single `FindHandler` resolution with enumeration over **all** handlers using the
verified engine API `Widget.GetNumHandlers()` / `Widget.GetHandler(int index)` (both confirmed on
the Widget interface docs):

```c
Widget tabViewWidget = root.FindAnyWidget(WIDGET_CATEGORY_TABVIEW);
m_TabView = null;
if (tabViewWidget)
{
    // The WLib_TabViewCoreMenus import carries its own SCR_TabViewComponent; our layout's
    // components block ADDS a second instance instead of overriding it. FindHandler returns the
    // FIRST instance (the inherited zombie: empty m_aElements, GetShownTab() stuck at -1), which
    // froze all tab tracking. Enumerate every instance, keep the first LIVE one (GetShownTab()>=0
    // — only an instance whose ShowTab() ever succeeded, i.e. one with real tabs, can be >= 0),
    // and mute action listening on any OTHER live instance: each live instance advances itself on
    // the shared paging buttons' Q/E activation, which is what doubled the visual tab-advance.
    int numHandlers = tabViewWidget.GetNumHandlers();
    for (int i = 0; i < numHandlers; i++)
    {
        SCR_TabViewComponent tv = SCR_TabViewComponent.Cast(tabViewWidget.GetHandler(i));
        if (!tv)
            continue;

        int shownTab = tv.GetShownTab();
        GRAD_Log.Info(string.Format("ArsenalMenu: TabView handler #%1 GetShownTab()=%2", i, shownTab));

        if (shownTab >= 0)
        {
            if (!m_TabView)
            {
                m_TabView = tv;
            }
            else
            {
                tv.SetListenToActions(false);
                GRAD_Log.Info(string.Format("ArsenalMenu: muted extra live TabView handler #%1 (double-advance culprit)", i));
            }
        }
    }

    // All instances read -1 (would mean even the tab-owning instance never initialized —
    // not expected, but fail towards the old behavior instead of a null handle).
    if (!m_TabView)
        m_TabView = SCR_TabViewComponent.Cast(tabViewWidget.FindHandler(SCR_TabViewComponent));
}

if (!m_TabView)
    GRAD_Log.Warn("ArsenalMenu: CategoryTabView / SCR_TabViewComponent not found");
```

Also add (right after the block above) a one-shot diagnostic counting the buttons in the shared
`"Tabs"` container — this decides between "1 zombie + 1 live" (5 buttons) and "two live instances
each creating a full tab row" (10 buttons, first 5 visible due to clipping — would fully explain
the double-advance):

```c
if (tabViewWidget)
{
    Widget tabsContainer = tabViewWidget.FindAnyWidget("Tabs");
    if (tabsContainer)
    {
        int buttonCount = 0;
        Widget child = tabsContainer.GetChildren();
        while (child)
        {
            buttonCount++;
            child = child.GetSibling();
        }
        GRAD_Log.Info(string.Format("ArsenalMenu: 'Tabs' container holds %1 tab buttons (5 expected)", buttonCount));
    }
}
```

**If the log reports 10 buttons**: the extra live instance's buttons are stacked after the real
ones in the same row. The mute above already fixes Q/E; additionally hide the duplicates by
calling `SetTabVisible(i, false, false)` for `i = 0..4` **on the muted instance** (verified real:
`SetTabVisible(int, bool, bool animate)` is in the indexed API). Do this only if the count is
actually 10 — gate it on the diagnostic, don't apply blind.

Leave `PollTabChange` (`GRAD_ArsenalMenu.c:733`), `RebuildBrowser`'s `ShowTab(tabToShow)`
(`:721`), and the `m_iLastLoggedShownTab` diagnostic exactly as they are — with the right instance
in `m_TabView` they become correct as-is. Replace the long "REVERTED: SetListenToActions..."
comment block (`:634-644`) with a short pointer to the new resolution logic.

### Expected outcome (what the user's next live test should show)

- Log: `TabView handler #0 GetShownTab()=-1` (zombie), `TabView handler #1 GetShownTab()=0` (live)
  — or similar; at least one `>= 0` entry.
- `PollTabChange: raw GetShownTab()=N` lines appearing on every click / Q / E press.
- Grid actually re-filters per tab; Q/E advances exactly one tab **if** the double-advance came
  from a second live instance. If exactly one live instance exists AND double-advance persists,
  report that — next suspect is `SCR_PagingButtonComponent`'s two activation paths (`OnClick` +
  action-listener `OnMenuSelect`, both invoke `m_OnActivated` — verified in its source), which
  would need a different lever.

---

## Bug 3 — RenderTargetWidget needs a BaseWorld camera SLOT index, not a camera entity

### What the vanilla source proves

`SCR_2DPIPSightsComponent` (the shipped in-game user of `RenderTargetWidget`, read on arexplorer):

- The `int camera` argument of `RenderTargetWidget.SetWorld(BaseWorld, int camera)` is a **world
  camera slot index** — a plain integer channel on `BaseWorld`. PIP sights use **index 8** from
  config. Slot **0 is the main game camera** — that's what our
  `CameraBase.GetCameraIndex()` returned for a never-registered spawned camera, i.e. we bound the
  widget to the main camera slot (and got nothing usable).
- The slot is driven **directly on the world**, no entity required:
  `world.SetCameraType(index, CameraType.PERSPECTIVE)`, `world.SetCameraVerticalFOV(index, fov)`,
  `world.SetCameraNearPlane(index, m)`, `world.SetCameraFarPlane(index, m)`, and the transform via
  `BaseWorld.SetCameraEx(int cam, const vector mat[4])` ("Changes camera matrix"). All five are
  verified in the indexed engine API. Vanilla spawns a `ScriptCamera` entity only because a weapon
  sight must track the weapon every frame; a menu preview doesn't need the entity at all (and
  `ScriptCamera` carries free-fly debug input — avoid it).

### Fix (all in `GRAD_ArsenalMenu.c`; layout already correct — `PaneCenterPreview` is
`RenderTargetWidgetClass`)

1. **Delete the camera entity path entirely**: `m_PreviewCamera` field (`:96`),
   `PREVIEW_CAMERA_PREFAB` (`:129`), `SpawnPreviewCamera()` (`:373-427`), `BindRenderTarget()`
   (`:432-443`), the camera cleanup in `OnMenuClose` (`:1702-1705`), and the stale
   "STILL UNVERIFIED: PREVIEW_CAMERA_PREFAB…" comment (`:314`). Keep `m_wPreview`
   (`RenderTargetWidget`, `:83`) and all preview-character code.

2. **Add** near the other preview constants:

```c
// BaseWorld camera slot rendered into the preview RenderTargetWidget. Slot 0 is the main game
// camera; vanilla PIP sights use slot 8 (SCR_2DPIPSightsComponent). 7 avoids both.
protected const int PREVIEW_CAMERA_INDEX = 7;
```

3. **In `SetupPreview`** (replacing the `SpawnPreviewCamera`/`BindRenderTarget` calls at
   `:358-363`): configure the slot, frame it, bind the widget:

```c
BaseWorld world = GetGame().GetWorld();
if (world && m_wPreview)
{
    world.SetCameraType(PREVIEW_CAMERA_INDEX, CameraType.PERSPECTIVE);
    world.SetCameraVerticalFOV(PREVIEW_CAMERA_INDEX, PREVIEW_CAMERA_FOV);
    world.SetCameraNearPlane(PREVIEW_CAMERA_INDEX, 0.05);
    world.SetCameraFarPlane(PREVIEW_CAMERA_INDEX, 500);
    FrameFullBody();
    m_wPreview.SetWorld(world, PREVIEW_CAMERA_INDEX);
    GRAD_Log.Info(string.Format("ArsenalMenu: preview bound to world camera slot %1", PREVIEW_CAMERA_INDEX));
}
```

4. **`PositionCamera(float distance, float eyeHeight)`** (`:527-586`): keep ALL the existing math
   (`SCR_Math3D.LookAt`, eye-height handling — it was never disproven, only untestable). Change
   only the guard and the final line:
   - guard: `if (!m_PreviewCharacter) return;` plus a `BaseWorld world = GetGame().GetWorld(); if (!world) return;`
   - apply: `world.SetCameraEx(PREVIEW_CAMERA_INDEX, camTransform);` instead of
     `m_PreviewCamera.SetWorldTransform(camTransform);`

5. **Re-apply per frame**: vanilla re-applies the slot transform every frame (`ApplyTransform` in
   `EOnPostFrame`); an unmanaged slot may not persist. Store the current framing distance in a new
   field `protected float m_fCameraDistance = PREVIEW_CAMERA_DISTANCE;` — set it in
   `FrameFullBody`/`FrameForCategory` (which then just call `PositionCamera(m_fCameraDistance, PREVIEW_CAMERA_EYE_HEIGHT)`)
   — and call `PositionCamera(m_fCameraDistance, PREVIEW_CAMERA_EYE_HEIGHT)` from `OnMenuUpdate`
   (near the existing `PollTabChange()` call, `:1733`). It's a handful of vector ops per frame;
   also keeps the framing correct if the preview character ever animates/moves.

### Expected outcome

The preview pane shows the world from 2.2 m in front of the preview character at 1.65 m eye
height, 40° vertical FOV. The framing constants (`PREVIEW_CAMERA_DISTANCE/EYE_HEIGHT/FOV`,
`:107-123`) were never visually validated — expect one tuning round with the user. Known
limitation to mention, not fix: the preview renders with live world lighting (dark at night).

---

## Hard constraints (unchanged from previous sessions)

- **EnforceScript has no ternary `?:` and no `??`.** Plain `if/else` only.
- **Never** set `CategoryTabView`'s slot to `SizeMode Fill` (user reverted this personally; it
  stays `Auto`). This plan needs no layout edits at all.
- **Never** call `CameraManager.SetCamera(...)` for the preview — it hijacks the full screen.
- Don't touch the item-card thumbnails (`ItemPreviewWidget` + `SetPreviewItemFromPrefab`) — they
  work.
- Don't read `inspiration/` (forbidden clean-room reference).
- Unverified API guesses have caused native engine crashes before. Every API named in this plan is
  verified (indexed API for signatures, arexplorer for bodies) — if you deviate, verify first.

## Verification

No automated tests exist; verification is a live Workbench run by the user (they drive it — ask
them to test and paste `console.log`). Checklist for the test:

1. Open arsenal menu → log shows the `TabView handler #N GetShownTab()=...` lines and the
   `'Tabs' container holds N tab buttons` line. **Paste these back even if things work.**
2. Click each tab and press Q/E → grid content changes per category; exactly one tab per press.
3. Center pane shows the character (not transparent, not top-down).
4. Open/close the menu twice → no crash, no leftover camera artifacts (slot 7 simply stops being
   read once the widget is gone; nothing to clean up).
5. If tabs still don't filter: the handler-enumeration log now says exactly which instance is
   live and how many buttons exist — include it in the report instead of guessing.
