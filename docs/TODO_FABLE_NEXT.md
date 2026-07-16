# TODO — next session

## -1. NEW BUG: freshly-equipped backpack (and likely vest) not usable as a destination until Confirm

**Reported live**: equip a backpack via double-click/EQUIP. The right-hand loadout panel correctly shows
it as worn (fill-bar/percent readout, live-confirmed) — so container DISCOVERY for the loadout panel
itself works immediately. But trying to ADD an item INTO that same freshly-equipped backpack fails right
after equip; it only starts working after Confirm. Likely affects the vest the same way (not yet
confirmed specifically for vest, but it goes through the identical code path).

**Investigated, not yet root-caused**: `OnAddToBackpack`/`RefreshLoadoutSlot` both call the exact same
`FindNamedContainer(128)` → `FindContainerStorage` → `GRAD_InventoryLib.CollectDestinationContainers`
chain — so if the loadout panel's readout is right but the ADD button still fails, they can't actually be
calling different logic; something about the CALL TIMING or the freshly-equipped entity's storage shape
must differ between "read via the panel refresh path" and whatever specific moment ADD's check runs, OR
(the live theory a diagnostic was added for) `CollectDestinationContainersRecursive`'s own
`owner != character` guard structurally only ever records a storage found ONE LEVEL BELOW a top-level
root — if a freshly-equipped backpack's own storage becomes a root ITSELF (returned directly by
`GetTopLevelStorages`) rather than appearing nested inside a character-owned root storage, the recursive
walk would never reach the branch that would register it, since recursion starts one level too deep for
that specific shape. Unconfirmed whether Confirm changes this because Confirm re-applies via a full
capture/clear/re-spawn cycle that might construct the storage graph differently than the incremental
single-item apply path `AddSelectedToContainer`/`ApplyToPreview` uses for a plain equip click.

**Diagnostic added** (`GRAD_InventoryLib.CollectDestinationContainers`, `GRAD_InventoryLib.c`): logs every
top-level storage's type, owner (or `<null>`), and slot count every time it's called. **Not yet
live-tested.** Needs: equip a backpack, immediately try ADD TO BACKPACK (or double-click a stackable
item) and capture the log, then Confirm and repeat the exact same add — compare the two
`CollectDestContainersDiag` dumps. If the backpack's own storage is present as a root in BOTH cases with
the same owner/slot count, the bug is elsewhere (worth checking `FindContainerStorage`'s type-matching
step, `GetArsenalTypeForPrefab` for a freshly-spawned-this-frame entity, or a manager-cache staleness this
diagnostic wouldn't catch). If the backpack's storage is simply ABSENT from the root list right after
equip but present after Confirm, that confirms the manager-index-staleness theory and the fix would be
either forcing a manager refresh after equip or discovering containers via a different, more
immediately-consistent API. **Do not guess at a fix before running this test.**

Everything below is **uncommitted** on `main` as of 2026-07-16. Old handoff docs
(`HANDOFF_2026-07-15.md`, `HANDOFF_2026-07-16.md`, `ARSENAL_STATUS_2026-07-14.md`) still hold the engine
facts (isolated-BaseWorld preview architecture, the `EquipAny`-orphaning garment-reequip bug) — read them
for background if touching the preview/apply code again. This doc tracks only what's actively being
worked on.

## 0. Live search + list-view toggle (NEW, implemented, needs live test)

**The idea** (user request): a text search box above the item grid (below the main tabs) that filters
live as you type, plus a toggleable list-view mode — thumb on the left, title on the right — as an
alternative to the icon-tile grid.

**What already existed to build on**: `GRAD_ItemBrowser.SetSearch(string)`/`m_sSearch` (blank = no
filter, matches against display name) already existed and was already used by `GetFiltered` — just never
wired to any UI, exactly like the faction filter before the previous update.

**Search box** (`GRAD_ArsenalMenu.c`/`GRAD_ArsenalMenu.layout`):
- New `SearchRow` (plain `EditBoxWidgetClass` + the list-view toggle button, side by side) inserted
  between `SubCategoryScroll` and `CategoryItemsScroll`.
- **No confirmed live-text-changed event exists on the base `EditBoxWidget`** — verified via
  `api_search`; only `SCR_EditBoxComponent`/`SCR_ChangeableComponentBase` (vanilla components, not used
  here, same reasoning as the faction-pill rewrite) expose `m_OnChanged`. Rather than guess at an
  unconfirmed callback the way a previous session got burned assuming `SCR_TabViewComponent.GetOnChanged()`
  would just work, this reuses `PollTabChange`'s own already-proven pattern: `PollSearchChange()`
  (called from `OnMenuUpdate`, right after `PollTabChange`) reads `EditBoxWidget.GetText()` every frame
  and only reacts when it differs from `m_sLastPolledSearch`, calling `m_Browser.SetSearch(...)` then
  `PopulateItems()`.
- Did NOT vendor a copy of vanilla `WLib_EditBoxSearch.layout` (referenced as fair-to-use in
  `PROVENANCE.md` but never actually copied into this project) — Workbench wasn't connected this
  session to inspect its real structure, and the faction-pill crash earlier this session was a direct
  lesson in what happens when a vanilla layout's child-slot shape is guessed at instead of verified. A
  plain, self-contained `EditBoxWidgetClass` (a real, verified widget class name) was used instead — less
  visually polished than the vanilla search box (no built-in magnifier icon/placeholder styling) but
  carries no risk of the same "does not accept more children"-class crash. **Worth revisiting once
  Workbench is available to actually read `WLib_EditBoxSearch.layout`'s real structure.**

**List-view toggle** (`GRAD_ArsenalMenu.c`/`GRAD_ArsenalMenu.layout`):
- New `UI/Layouts/GRAD_ItemListRow.layout` (+ `.meta`, GUID `{E1F2A3B4C5D61001}`): a self-contained
  `style blank` button (same pattern as `GRAD_ItemCard.layout`'s own `CardButton` — chosen deliberately
  after the faction-pill lesson about `WLib_ButtonText.layout` only accepting one child) with a thumbnail
  (`ItemPreviewWidgetClass`, same live 3D preview mechanism the grid cards already use) on the left and
  name/count text on the right. Its background image is deliberately named `TileBg` — the SAME name the
  grid card layout uses — so `OnItemRowClicked`'s existing selection-highlight lookup
  (`cardWidget.FindAnyWidget(WIDGET_CARD_BG)`) works unmodified for both view modes with no special-casing.
- New `CategoryListScroll`/`CategoryList` (a `ScrollLayoutWidgetClass` wrapping a plain
  `VerticalLayoutWidgetClass`) added as a sibling of the existing `CategoryItemsScroll`/`CategoryItems` —
  both containers exist in the layout at all times; `ApplyViewMode()` shows exactly one via `SetVisible`
  or the other, and `PopulateItems()` fills whichever is currently visible (`m_bListView` flag) and leaves
  the other alone rather than keeping both in sync for a view the user isn't looking at.
- `PopulateItems`'s grouping/expand-collapse logic (`GetGrouped`, `IsGroupExpanded`, `ConciseVariant`) is
  completely unchanged and shared between both view modes — only the per-item widget-creation step
  differs. `CreateItemCard`/`CreateGroupHeaderCard` now call a new dispatcher, `CreateItemWidget`, which
  routes to the existing `CreateItemCardWidget` (grid) or the new `CreateItemListRowWidget` (list) based
  on `m_bListView`. `CreateItemListRowWidget` has no grid-cell/`UniformGridSlot` addressing to do — a
  plain `VerticalLayoutWidget` just stacks children in append order.
- `OnListViewToggle` (bound to the new `ButtonListView`) flips `m_bListView`, calls `ApplyViewMode()`,
  then `PopulateItems()`.

**Needs a layout reimport** (two layout files touched, one new file) **+ a full Workbench restart**, then
a live check: does the search box actually filter as you type (not just on Enter/focus-lost — the poll
runs every frame, so it should be instant), does toggling list view actually swap containers and
repopulate with the thumb-left/title-right shape, does clicking a list row still select it into the
Selected-Item panel with the same amber highlight the grid cards get, does group-header expand/collapse
still work in list mode, does switching top tabs or sub-tabs while in list mode correctly stay in list
mode (nothing resets `m_bListView` except the user's own toggle click — confirm that's actually true
live, not just true by reading the code).

## 1. Sub-tab clustering within each top-level tab — implemented, needs live test

**The idea:** today the 5 top tabs (`GRAD_ArsenalTabs` in `GRAD_ArsenalMenu.c`) each bucket several
distinct arsenal item types together — e.g. **Apparel** lumps Headgear + Jackets + Vests + Trousers +
Footwear + Gloves into one flat item grid, and **Primary** lumps Rifles + Machine Guns + Sniper Rifles +
Launchers. The user wants a second, finer level of clustering — e.g. clicking Apparel should offer
Headgear / Trousers / Vests / etc. as sub-choices, not one undifferentiated grid of everything.

**Precedent**: `docs/DECISIONS.md` D5 (FINAL) originally specified a left-rail category nav with entries
like Weapons/Uniform/Vest/Headgear/Backpack/Attachments/Loadouts — closer to this granularity than the 5
flat top tabs that shipped instead during the mockup-driven redesign. This sub-tab feature effectively
restores that granularity as a second tier under the current top tabs, without redoing the whole top-level
navigation.

**Design chosen** (avoids re-dragging in `SCR_TabViewComponent`'s fragility — see the multi-session
struggle in `ARSENAL_HANDOVER_2026-07-12*.md` and `PLAN_TABS_AND_CAMERA_2026-07-13.md` just to get the TOP
tab strip working): a plain horizontal row of text buttons (same pattern as `CreateRow`/the loadout
REMOVE buttons — nothing from the vanilla TabView widget library), inserted as a new sibling between
`CategoryTabView` and `CategoryItemsScroll` in `PaneLeftCategories`. Only shown when the active top tab's
distinct sub-types number more than one (e.g. Container = Backpacks + Radio Backpacks still gets a
2-button sub-row; a hypothetical single-type tab would show none). Clicking a sub-tab calls
`GRAD_ItemBrowser.SetCategory(exactType)` (already exists, already does single-type filtering — no
browser-layer changes needed) instead of `SetCategoryMask(tabMask)`. An "All" pill re-applies the mask for
"show everything in this top tab" behavior, preserving today's default view when no sub-tab is picked.

**What already exists to build on**:
- `GRAD_ArsenalCategoryLabels.LabelFor(arsenalType)` (`GRAD_ArsenalMenu.c` ~line 3001) — clean label per
  individual bit (Headgear, Trousers, Machine Guns, Grenades, ...), already used elsewhere for display.
- `GRAD_ItemBrowser.SetCategory(int arsenalType)` (`GRAD_ItemBrowser.c` ~line 73) — exact single-type
  filter, sibling to the mask-mode `SetCategoryMask` the top tabs use. No changes needed here.
- `GRAD_ArsenalTabs.MaskFor(tabIndex)` — the top tab's OR-mask; sub-tabs need the INDIVIDUAL bits that
  make up that mask, decomposed (e.g. Apparel's mask decomposed into its 6 constituent bits).

**Implementation steps**:
1. `UI/Layouts/GRAD_ArsenalMenu.layout`: add a new `HorizontalLayoutWidgetClass` (e.g.
   `Name "SubCategoryRow"`) as a sibling between `CategoryTabView` and `CategoryItemsScroll` inside
   `PaneLeftCategories`. Empty at layout-author time — populated at runtime like the item grid, not
   hand-authored per sub-type (the labels/mask decomposition already come from script). **Needs a layout
   reimport, not just a script restart**, same as the recent Headgear-panel addition.
2. `GRAD_ArsenalMenu.c`: a helper to decompose a tab's mask into its individual set bits (iterate bit
   positions 1..22, test `(mask & (1<<i)) != 0`, collect those present against `1<<i`) — or, simpler and
   less bit-fiddly, add an explicit per-tab array of constituent bits next to each `MaskFor` case in
   `GRAD_ArsenalTabs` (matches how `PRIMARY`/`APPAREL`/etc. masks are already hand-built as an OR of named
   bits, so no new source of truth).
3. On `SelectCategoryByIndex` (top tab change): rebuild the sub-tab row's buttons from the new tab's
   constituent bits (skip rebuild/hide the row entirely if there's only one). Track selected sub-tab
   state (new field, e.g. `m_iSelectedSubCategory`, reset to "All" — i.e. mask mode — on every top-tab
   switch, since a sub-selection from the previous tab means nothing in the new one).
4. Sub-tab click handler (mirrors `BindUnequipButton`'s small-persistent-handler pattern already used in
   this file for REMOVE buttons — needed because each button needs to carry which arsenal-type bit it
   represents): calls `m_Browser.SetCategory(bit)` then `PopulateItems()`. An "All" button (or just
   re-clicking the already-active top tab) calls `m_Browser.SetCategoryMask(currentTabMask)` to restore
   the unfiltered view.
5. Live test: switch top tabs, confirm the sub-row appears/disappears/repopulates correctly, confirm each
   sub-tab actually narrows the grid (compare card count against the label), confirm "All" restores the
   full tab's items, confirm nothing crashes on menu close (clear sub-tab button handlers alongside the
   existing `m_aItemRowHandlers`/`m_aUnequipHandlers` clears already done in `Close()`/equivalent).

**Status: implemented, not yet live-tested.** All 5 steps above are done:
- `UI/Layouts/GRAD_ArsenalMenu.layout`: added `SubCategoryScroll` (a `ScrollLayoutWidgetClass`, `SizeMode
  Auto` so it collapses to zero height when hidden) wrapping `SubCategoryRow` (an empty
  `HorizontalLayoutWidgetClass`, populated at runtime), inserted as a new sibling between
  `CategoryTabView` and `CategoryItemsScroll` inside `PaneLeftCategories`.
- `GRAD_ArsenalMenu.c`: `GRAD_ArsenalTabs.SubTypesFor(tabIndex, outTypes)` decomposes an existing tab mask
  into its individual bits (no separate per-tab list — reads the same hand-built `PRIMARY`/`APPAREL`/etc.
  masks that already exist, so one source of truth). `RebuildSubCategoryRow` (called from
  `SelectCategoryByIndex`) rebuilds the pill row on every top-tab switch, hides it entirely
  (`SetVisible(false)`) when the tab has 1 or fewer constituent types. `CreateSubCategoryPill` reuses the
  existing `CreateRow` helper (same `ROW_LAYOUT`/`GRAD_ListButtonRow.layout` plain text button already
  used by the loadout REMOVE buttons — deliberately NOT another `SCR_TabViewComponent`, to avoid
  re-dragging in that widget's documented fragility). `GRAD_SubCategoryHandler` (new class, mirrors the
  existing `GRAD_UnequipHandler`/`GRAD_LoadoutLineHandler` per-button-context-bridge pattern) routes a
  click to `OnSubCategoryClicked(arsenalType)`, which calls `m_Browser.SetCategory(arsenalType)` for a
  specific sub-type or `SetCategoryMask(tabMask)` for the "All" pill (index 0) — both browser-side methods
  already existed pre-this-change; `GRAD_ItemBrowser` itself was not touched.

**UPDATE (same day, follow-up per user feedback — "the buttons look like shit, can we use some tabs
there too?")**: the pills now use `SCR_ButtonTextComponent`'s own built-in toggle machinery
(`SetToggleable(true)` + `SetToggled(...)`) instead of being plain unstyled text buttons.
`WLib_ButtonText.layout` (the vanilla base every button here already inherits) already defines
`m_BackgroundSelected`/`m_BackgroundSelectedHovered` as the same amber used by the top tab strip's own
active-tab look, so a toggled pill matches the rest of the UI for free — no new color-management code.
`m_aSubCategoryButtons` (parallel to `m_aSubCategoryHandlers`, same index = same pill) holds each pill's
`SCR_ButtonTextComponent` so `OnSubCategoryClicked` can walk all of them and toggle exactly one active
(the component only tracks its own state, not siblings' — mutual exclusion is the menu's job). This still
does not use `SCR_TabViewComponent` itself — same reasoning as before, just a better-looking plain button.

- **Needs a layout reimport (not just a script restart)**, same as the recent Headgear-panel addition,
  then a live pass through the 5-step checklist above plus a visual check of the new toggle styling —
  none of it has been run in Workbench yet.

**UPDATE (same day, live-tested — functionality confirmed working, feedback: "twice as large" and proper
active states for both rows)**: sub-tab pills are now grown at runtime — `SizeLayoutWidget.SetMinDesired
Width/Height` (verified real API) on the button's inherited `"SizeLayout"` child bumps each pill to
160x64 (`SUBCATEGORY_PILL_MIN_WIDTH`/`_HEIGHT` in `GRAD_ArsenalMenu.c`), plus a larger font
(`SCR_ButtonTextComponent.GetTextWidget().SetExactFontSize(24)`), scoped to sub-category pills only —
NOT a `GRAD_ListButtonRow.layout` edit, since `CreateRow` (the layout's only creator) is otherwise
unused elsewhere in this file today, but changing the shared layout file itself would still resize
anything future code hangs off it. The toggle-active look (from the earlier update) was already
confirmed live and did not need further changes.

## 2. Faction filter row (NEW, implemented, needs live test)

**The idea** (user request, same feedback pass as the sub-tab styling above): a row of faction thumbnail
buttons across the very top of the menu — one per faction FactionManager knows about, plus an "ALL" pill
— so the item grid can be scoped to one faction's gear or shown unfiltered across every faction.

**What already existed to build on**: `GRAD_ItemBrowser.SetFactionKey(string)`/`m_sFactionKey` (blank =
all factions) already existed and was already being used — just invisibly. `RebuildBrowser` used to
silently auto-scope the browser to `m_Context.GetPrimaryTarget()`'s own faction on every menu open, with
no UI control over it at all. That auto-scope is now **removed** — the faction row replaces it with an
explicit, visible, user-controlled filter, defaulting to ALL (showing every faction's gear at once, the
literal ask) with the row as the way to narrow.

**Implementation**:
- `UI/Layouts/GRAD_ArsenalMenu.layout`: added `FactionRowScroll` (`SizeMode Auto`) wrapping `FactionRow`
  (an empty `HorizontalLayoutWidgetClass`, populated at runtime) as a new sibling inserted between
  `Title` and `Content`, so it spans the full top of the menu above both the tab strip and the preview
  pane.
- `UI/Layouts/GRAD_FactionPill.layout` (NEW file + matching `.layout.meta`, GUID
  `{D2A3B4C5D6E74001}`): a 64x64 icon button.
- `GRAD_ArsenalMenu.c`: `RebuildFactionRow()` (called once from `SetupCategoryRail`, NOT per-tab like the
  sub-category row — the faction list doesn't change when switching Primary/Apparel/etc.) builds an ALL
  pill (empty key) plus one pill per `FactionManager.GetFactionsList()` entry, each icon sourced from
  `Faction.GetUIInfo().GetIconPath()` (verified real: `proto external UIInfo GetUIInfo()` on `Faction`,
  `proto external ResourceName GetIconPath()` on `UIInfo` — the same accessor vanilla faction-select UI
  uses) via `ImageWidget.LoadImageTexture(0, iconPath)` (verified real:
  `proto bool LoadImageTexture(int num, ResourceName resource, ...)`). `OnFactionClicked(factionKey)`
  calls `m_Browser.SetFactionKey(factionKey)`, re-syncs the active look across all pills, and
  repopulates. `RebuildBrowser` now re-applies `m_sSelectedFactionKey` (the row's current selection)
  instead of re-deriving the target's faction — same "late rebuild from OnCatalogReady must not silently
  override what's visually selected" bug class already documented for the top-tab mask just above it in
  that method.

**UPDATE (same day, live-tested): first version crashed the GUI on open** —
`GUI (E): Cannot add a child, the ButtonWidget FactionPillButton does not accept more children`, one per
pill. Root cause: `GRAD_FactionPill.layout` originally inherited `WLib_ButtonText.layout` and tried to
append an icon as a second child under the inherited `Button` root — but a plain `ButtonWidgetClass`
holds exactly ONE child slot (already filled by the inherited `SizeLayout`), confirmed by every OTHER
`WLib_ButtonText`-inheriting button in this project (`GRAD_ListQtyRow.layout`'s
`ButtonMinus`/`ButtonPlus`, `GRAD_ListButtonRow.layout`) adding zero extra children of their own — none
of them needed to, this was the first one that tried. **Fixed**: `GRAD_FactionPill.layout` rewritten as a
self-contained `style blank` button (mirrors `GRAD_ItemCard.layout`'s own `CardButton` pattern) with a
hand-built `OverlayWidgetClass` stack — `FactionPillBg` (background), `FactionPillBorder` (amber
selection outline, 0 alpha by default), `FactionPillIcon` (the 48x48 icon) — since it no longer inherits
`SCR_ButtonTextComponent`'s automatic toggle machinery, `CreateFactionPill`/`OnFactionClicked` now
manually recolor `FactionPillBorder` on selection (`SetFactionPillActive`, same amber
`0.761 0.386 0.078` as the sub-category pills' native toggle color, alpha 1 active / 0 inactive) — same
amber-highlight-on-select pattern this file already uses for the selected item card
(`m_wSelectedCardBg.SetColor` in `OnItemRowClicked`). `m_aFactionButtons` (the old
`SCR_ButtonTextComponent` array) was replaced with `m_aFactionBorders` (`array<ImageWidget>`).

- **Needs a layout reimport** (two layout files touched, one rewritten after the crash) **+ a full
  Workbench restart**, then a live check: does every faction get a pill, do the icons actually load (a
  faction with no authored icon just shows a blank pill — not a bug, nothing to load), does clicking ALL
  vs. a specific faction actually change which items appear, does the new amber border actually show/hide
  correctly on click (this exact mechanism has not been live-tested yet — only the earlier, crashing
  version was), does the faction row interact sanely with the sub-category row (two independent,
  orthogonal filters — both can be active at once; nothing about that interaction has been tested).

## 3. Garment re-equip-after-strip — fix applied 2026-07-16, needs live retest

See `docs/HANDOFF_2026-07-16.md` for the full investigation. Short version: re-equipping a garment into
an EMPTY slot works; re-equipping BDU Trousers right after a one-piece coverall (which occupies BOTH the
uniform and LEGS slots) vacated that slot silently failed — `EquipAny` reported `true` but the entity was
an orphan (`parent=0`, `worldPos=<0,0,0>`). Verified via arexplorer's real `EquipAny` source that this is
NOT the async drop-callback path (ruled out — our call always takes the synchronous "picking up item from
ground" branch). Fix applied in `TryEquipReplace` (`GRAD_LoadoutApply.c`): don't trust `EquipAny`'s return
value alone — read back `GetParent()`, and treat a still-orphaned result as a failure so the caller's
existing fallback chain gets a real shot instead of silently losing the item. **Not yet live-tested** —
needs a full Workbench restart (script change) and a repeat of the exact repro (coverall → REMOVE trousers
→ re-equip separate BDU Trousers).

## 4. Strong lead, not yet pursued: `SCR_PlayerArsenalLoadout`

`static bool ApplyLoadoutString(IEntity owner, BaseSerializationLoadContext context)` /
`ReadLoadoutString(...)` — vanilla's own full-loadout serialize/apply, almost certainly what the arsenal
terminal uses to swap a whole kit, and its "remove any existing entity whose slot wasn't mentioned"
behavior might sidestep the `EquipAny`-orphaning problem in #3 entirely. Unverified whether the format is
compatible with GRAD_Loadout's own capture model. Architectural decision, not a patch — a dedicated
session, not a detour.

## Constraints (same as always)

- No ternary `?:`, no `??` in EnforceScript.
- Never touch `CategoryTabView`'s `SizeMode` (stays `Auto`).
- Never call `CameraManager.SetCamera(...)`.
- Don't read `inspiration/` (the project's own forbidden clean-room folder).
- Verify every API via `mcp__enfusion-mcp__api_search` or arexplorer.zeroy.com real source before using
  it. arexplorer fetches are AI-summarized per query, not raw text — a second, differently-worded fetch
  can contradict the first; re-fetch verbatim quotes when a detail actually matters before trusting it.
- Full Workbench restart needed for script changes; `.layout` changes need a reimport.
- **Lesson repeated across sessions**: when a change produces IDENTICAL results to the previous attempt
  (not just "still broken" — the SAME symptom), the variable you changed was not the cause. Add a
  diagnostic that reads back actual state instead of trusting a native call's return value.
