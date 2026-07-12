# Arsenal Menu — Handover / Status (2026-07-12)

## Where things stand

The arsenal menu (`GRAD_ArsenalMenu`) was rebuilt around vanilla `SCR_TabViewComponent` for the
category tab strip (replacing a hand-built, permanently-invisible tab bar from earlier attempts — see
`docs/ENFUSION_LAYOUT_NOTES.md` and `docs/SHOP_SYSTEM_LAYOUT_STUDY.md` for the full history of why).
The user then substantially and successfully restructured `UI/Layouts/GRAD_ArsenalMenu.layout` into a
proper 3-pane layout. **Tabs, tab highlighting, and Q/E paging are confirmed working** (screenshot +
live test). Two things are not yet confirmed working: the item grid populating with tiles, and the
character preview rendering. Import/Export-to-clipboard was implemented but not yet live-tested.

## Confirmed working (do not re-litigate these)
- **Tab strip**: `SCR_TabViewComponent` hosted by `CategoryTabView` (instances vanilla
  `WLib_TabViewCoreMenus.layout`). 5 tabs (Primary/Secondary/Throwables/Apparel/Container) declared as
  `SCR_TabViewContent` entries in `m_aElements`, each pointing `m_ElementLayout` at
  `GRAD_CategoryPane.layout` (now vestigial filler — items render in the separate shared grid, not in
  these per-tab panes; leave as-is, harmless).
- **Tab highlight**: handled entirely by the vanilla component, no script painting needed.
- **Q/E paging**: `m_sActionLeft`/`m_sActionRight` on `SCR_TabViewComponent` were found (via the user
  pasting the actual vanilla `SCR_TabViewComponent.c` source) to have **swapped defaults** —
  `m_sActionLeft` defaults to `"MenuTabRight"`, `m_sActionRight` defaults to `"MenuTabLeft"`. Fixed by
  setting both explicitly in the layout: `m_sActionLeft "MenuTabLeft"` / `m_sActionRight
  "MenuTabRight"`. **Confirmed working live** (user: "Q + E works").
- **Tab-change reaction without a crash**: `SCR_TabViewComponent.GetOnChanged()`'s invoker type
  `ScriptInvokerTabViewIndex` requires a 3-arg callback (`void
  Method(SCR_TabViewComponent tabView, Widget widget, int index)` — confirmed from the vanilla source).
  Rather than bind that, `GRAD_ArsenalMenu.c` polls `m_TabView.GetShownTab()` once per frame in
  `OnMenuUpdate()` via `PollTabChange()`, calling `SelectCategoryByIndex(shown)` on change. This is
  intentional and should NOT be replaced with a direct `GetOnChanged().Insert(...)` binding unless the
  full 3-arg signature is used.
- **Loadout panel** (Uniform/Backpack/Vest bars + contents + existing [-]/[+] quantity controls):
  unaffected by this work, confirmed rendering correctly in the screenshot.
- **Import/Export wiring** (implemented by a sub-agent this session, reviewed, looks correct, but NOT
  yet live-tested): `ButtonLoad` ("Import" caption) and `ButtonSave` ("Export" caption) in
  `HorizontalLayout0 > ButtonBarLoad` are bound in `BindButtons()`.
  - `OnExportClicked()`: `GRAD_LoadoutCapture.Capture(m_PreviewCharacter, "Export", true)` →
    `data.ToJsonString()` → `System.ExportToClipboard(json)`.
  - `OnImportClicked()`: `System.ImportFromClipboard()` → `GRAD_LoadoutData.FromJsonString(json)` →
    `GRAD_LoadoutApply.Apply(m_PreviewCharacter, data, true, false, created, true)` (clearFirst=true,
    i.e. REPLACES the preview's current loadout) → refreshes `RefreshPreviewRender()` /
    `PopulateItems()` / `RefreshSelectedPanel()` / `RefreshLoadoutPanel()`.
  - **Needs a live test**: Export a loadout, paste clipboard into a text editor to sanity-check it's
    valid JSON, then Import it back and confirm the preview's gear matches.

## NOT yet confirmed working — the open problem

Latest log (after the Q/E fix, same session):
```
SCRIPT (W): ArsenalMenu: preview widget not found in layout
SCRIPT: SelectTab 0 mask=306 itemGridFound=0
```
Screenshot shows: tabs + loadout panel render fine, but the center preview pane is empty (just the
game world showing through) and the left item-tile area is completely empty (no tiles, no error text).

### Diagnosis so far
- `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c`:
  - `WIDGET_PREVIEW = "PaneCenterPreview"` (line ~25)
  - `WIDGET_ITEM_GRID = "CategoryItems"` (line ~27)
  - Both are looked up via plain recursive `root.FindAnyWidget(...)` in `SetupPreview()` and
    `SetupCategoryRail()` respectively — position in the tree does not matter for `FindAnyWidget`.
- `UI/Layouts/GRAD_ArsenalMenu.layout` (re-read fresh this session, confirmed present):
  - `ItemPreviewWidgetClass` named exactly `"PaneCenterPreview"` at line ~206-211, a direct child of
    `Content` (sibling of `PaneLeftCategories` / `PaneRightContainer`).
  - `GridLayoutWidgetClass` named exactly `"CategoryItems"` at line ~156-161, inside
    `PaneLeftCategories`, a sibling of `CategoryTabView` / `SelectedPanel` / `SelButtons`.
  - **Both widget names match the code's constants exactly.** No rename mismatch found on inspection.
- Conclusion: this is very likely the **stale-layout-load problem already documented in
  `docs/ENFUSION_LAYOUT_NOTES.md` section 10.1** — Workbench was continuing to run a previously-loaded
  version of `GRAD_ArsenalMenu.layout` that predates these widget names/positions, because a reimport
  wasn't done, or the layout was open in the Workbench layout editor (which re-saves stale content over
  disk edits when closed). **The user was mid-way through a full Workbench restart when this session's
  context was compacted.**

### What to do next (first thing in the new session)
1. Confirm the Workbench restart completed, then have the user reimport
   `UI/Layouts/GRAD_ArsenalMenu.layout` explicitly in the Resource Browser (right-click → Reimport) if
   a plain restart alone doesn't pick it up — this project has hit this exact caching trap multiple
   times before (see `docs/ENFUSION_LAYOUT_NOTES.md` for the full incident history and the confirmed
   fix pattern: edit → reimport → close-if-open-in-editor → play-mode restart).
2. Re-test: open the arsenal, check the console for the two log lines above. If they're gone / say
   `itemGridFound=1`, both bugs were pure staleness — nothing left to fix, move on to live-testing
   Import/Export (see above) and to giving the tile cards real visual polish (icons currently come from
   `SCR_UIInfo.SetIconTo` per `CreateItemCardWidget()` — confirm icons actually show, not just text).
3. **If the warnings persist after a confirmed-clean reimport + restart**, that means there IS a real
   widget-resolution bug, not staleness — at that point, use the Workbench MCP tools
   (`mcp__enfusion-mcp__wb_entity_inspect` or the layout/resource browser) to inspect the LIVE loaded
   widget tree at runtime and compare it byte-for-byte against what's in
   `UI/Layouts/GRAD_ArsenalMenu.layout` on disk right now — do not re-guess widget names blind again,
   inspect the actual runtime tree.

## Architecture notes for future work (do not re-derive these)
- `GridLayoutWidget` only exposes `Set{Row,Column}FillWeight` for sizing — no auto-flow. Tiles must
  have a real fixed desired size (`SizeLayoutWidget` + `AllowWidthOverride`/`AllowHeightOverride` +
  `WidthOverride`/`HeightOverride`) AND the grid needs `SetColumnFillWeight`/`SetRowFillWeight` called
  per column/row after populating, or cells collapse/overlap. This fix is already applied in
  `PopulateItems()` (search for `SetColumnFillWeight` in `GRAD_ArsenalMenu.c`) — do not remove it.
- A `ScrollLayoutWidget`'s child must use a bare `{ }` — writing an explicit `Slot ScrollLayoutSlot
  { ... }` throws `GUI (E): Unknown class 'ScrollLayoutSlot'` and silently fails the WHOLE layout load
  (this was the root cause of an earlier "nothing renders" incident). Never write that slot type.
- EnforceScript `ScriptInvoker` callbacks require an EXACT parameter-count/type match to the invoker's
  typedef'd method signature, checked at compile time — a mismatch is a compile ERROR (not a silent
  no-op as in some other engines). When an invoker's exact signature isn't independently verifiable,
  prefer polling the underlying state each frame over guessing the callback signature.
- `SelectedPanel` (the old "selected item detail" panel with `SelName`/`SelStats`/`SelIcon`) is
  intentionally hidden (`"Is Visible" 0` / `"Is Enabled" 0`) per the user's explicit design decision —
  they want the grid + bottom ADD-button row as the whole interaction model, no detail panel. Do not
  re-enable it. `RefreshSelectedPanel()`/`SetAddButtonsEnabled()` in the .c file still run harmlessly
  against the hidden widgets and drive the real ADD TO VEST/BACKPACK/EQUIP buttons correctly.
- Two verified-real vanilla input action names for menu tab paging: `"MenuTabLeft"`,
  `"MenuTabRight"` (confirmed from live `SCR_TabViewComponent.c` source, not guessed).

## Files touched this session
- `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c` — TabView integration, single shared item grid
  resolution, PollTabChange, Import/Export methods.
- `UI/Layouts/GRAD_ArsenalMenu.layout` — user's 3-pane restructure (Interface > Content >
  PaneLeftCategories/PaneCenterPreview/PaneRightContainer) + `m_sActionLeft`/`m_sActionRight` fix.
- `docs/ENFUSION_LAYOUT_NOTES.md`, `docs/SHOP_SYSTEM_LAYOUT_STUDY.md` — prior-session research, still
  accurate and referenced above; not modified this session.
