# Arsenal Redesign — Status / Handoff

_Snapshot for resuming after a compaction. Reflects the actual on-disk state of the files as read on 2026-07-11._

Project root: `c:\Users\nomisum\Documents\my games\ArmaReforgerWorkbench\addons\GRADLoadout`

---

## What this work is

A mock-based redesign of the arsenal menu ("ARSENAL MANAGER") plus preview/preload fixes. The user provided screenshots of a target mock with:

- **Top tabs**: Primary / Secondary / Throwables / Apparel / Container (a horizontal tab bar, not a left rail).
- **Item TILE GRID**: icon cards (icon + name + owned-count) that the player clicks.
- **Selected-Item panel**: shows the clicked item's icon/name/stats + `ADD TO VEST` / `ADD TO BACKPACK` / `EQUIP` buttons.
- **Live character preview** center (drag-rotate, zoom).
- **Right loadout panel**: Uniform / Vest / Backpack, each with a fill-% bar + percent + contents list.

All edits happen on a LOCAL preview clone only; nothing touches the networked target until OK (which serializes the preview loadout and applies it via RPC).

---

## Files changed (current on-disk state)

### `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c` (1340 lines)
The main menu. Key structure:

- `modded enum ChimeraMenuPreset { GRAD_ArsenalMenu }` (lines 4-7).
- `class GRAD_ArsenalMenu : ChimeraMenuBase` (line 22):
  - Widget-name constants (lines 25-58): preview, tab bar `CategoryList`, grid container `ItemList`, card children (`CardIcon/CardName/CardCount`), selected panel (`SelIcon/SelName/SelStats/ButtonAddVest/ButtonAddBackpack/ButtonAddEquip`), loadout panel slots (Uniform/Vest/Backpack Bar/Percent/Contents), `BAR_FULL_WIDTH=220`.
  - Preview fields (66-82): `m_PreviewCharacter`, `m_wPreview`, `m_PreviewManager`, `m_PreviewCameraHelper`, `m_PreviewAttribs` (**typed base `PreviewRenderAttributes`** so it matches `Update(inout ...)`), `m_sPreviewPrefab`, `m_aPreviewCreated`.
  - Browser/grid fields (85-122): `m_Browser`, `m_wCategoryList`, `m_wItemList`, `ROW_LAYOUT {4BE35AEBB44455F0}`, **`CARD_LAYOUT = "{A704EDAAAADC6ADB}UI/Layouts/GRAD_ItemCard.layout"`** (line 99), row-handler arrays, `m_mExpandedGroups`, `m_SelectedRecord`, `m_mPreviewCounts`, **`GRID_COLUMNS = 2`** (121) + `m_iGridCell` (122).
  - `OnMenuOpen` (145): builds context, then `BindButtons` → `SetupPreview` → `SetupCategoryRail`.
  - `BindButtons` (177): binds OK/Cancel + the 3 ADD buttons; starts ADD buttons disabled.
  - `SetupPreview` (211): **instantiates `new SCR_CharacterInventoryPreviewAttributes()`** (line 233, the fix — was `PreviewRenderAttributes`), spawns a local clone via `SpawnLocal`, `SetPreviewItem`, copies identity, mirrors current loadout, re-renders, pins alive, then registers `SCR_InventoryCharacterWidgetHelper` on the widget for mouse orbit/zoom.
  - `CopyIdentity` (274), `RefreshPreviewRender` (296), `PinPreviewAlive` (307, `Deactivate()`), `ApplyToPreview` (317, additive, routes to `preferredStorage`).
  - `SetupCategoryRail` (339): finds `CategoryList`+`ItemList`, ensures service exists, kicks/subscribes to catalog build, `RebuildBrowser`.
  - `OnCatalogReady` (373), `RebuildBrowser` (385): builds `GRAD_ItemBrowser`, sets faction, `PopulateCategories` → `SelectCategoryByIndex(0)` → `RefreshLoadoutPanel`.
  - `PopulateCategories` (408): builds the 5 tabs from `GRAD_ArsenalTabs` as row buttons.
  - `SelectCategoryByIndex` (430): sets browser mask via `GRAD_ArsenalTabs.MaskFor`, **logs `SelectTab %1 mask=%2 catListFound=%3` (diag, line 436-437)**, `PopulateItems`.
  - `PopulateItems` (445): clears item-row handlers + children, **resets `m_iGridCell=0` (454)**, `RebuildPreviewCounts`, `GetGrouped`, then for each group makes a single card / collapsible header + variant cards.
  - `ConciseVariant` (496), `IsGroupExpanded` (547), `CreateGroupHeaderCard` (556), `OnGroupHeaderClicked` (576).
  - `CreateItemCard` (585): builds a card, sets handler, **`m_bBoundOk = SCR_InputButtonComponent.FindComponent(card) != null` diag (604)**.
  - **`CreateItemCardWidget` (610)**: `workspace.CreateWidgets(CARD_LAYOUT, m_wItemList)`, then **`GridSlot.SetColumn(card, cell % GRID_COLUMNS)` / `GridSlot.SetRow(card, cell / GRID_COLUMNS)` + `m_iGridCell++` (621-623)**; fills name/count text and icon (`uiInfo.SetIconTo` or hide if no icon).
  - `OnItemRowClicked` (648): **logs `OnItemRowClicked: <name>` (diag, 653)**, sets `m_SelectedRecord`, `RefreshSelectedPanel`.
  - `RefreshSelectedPanel` (663): fills icon/name/stats; enables ADD buttons — vest if `FindNamedContainer(4096)`, backpack if `FindNamedContainer(128)`, equip if not stackable.
  - `SetAddButtonsEnabled` (712) / `FindNamedContainer` (726) / `FindContainerStorage` (735, uses `CollectDestinationContainers`).
  - `OnAddToVest` (767, 4096) / `OnAddToBackpack` (776, 128) / `OnEquipSelected` (785, null=auto) / `AddSelectedToContainer` (793): applies single-item loadout to preview, then `PopulateItems` + `RefreshSelectedPanel` + `RefreshLoadoutPanel`.
  - `RebuildPreviewCounts` (811), `CountOnPreview` (824), `RemoveOneFromPreview` (844).
  - `CreateRow` (882), `SetButtonText` (901), `SetButtonEnabled` (914, dims via opacity 0.35/1.0).
  - `RefreshLoadoutPanel` (932): calls `RefreshLoadoutSlot` for TORSO 2048 (Uniform), VEST 4096, BACKPACK 128.
  - `RefreshLoadoutSlot` (941): `GetStorageFillFraction` → bar opacity `0.25+0.75*frac` + percent text + `FillSlotContents`.
  - `FillSlotContents` (980) / `CreateContentsLine` (1008) / `ClearChildren` (1026, `RemoveFromHierarchy`).
  - `OnConfirm` (1039): capture preview loadout, apply via `SCR_PlayerController.GradGetLocal().GradApplyLoadout(rplId, result)` to each target. `OnCancel` (1084). `OnMenuClose` (1090): tears down helper, unsubscribes, cleanup created, unbinds preview, deletes clone. `OnMenuUpdate` (1122): feeds helper `Update(tDelta, m_PreviewAttribs)`, re-pushes on change.
- `GRAD_ArsenalMenuContext` (1140): target list wrapper.
- `GRAD_ArsenalRowHandler` (1176): `m_iCategoryIndex`, `m_Record`, `m_bIsCategory`, `m_bIsGroupHeader`, `m_sGroupKey`, **`m_bBoundOk` diag (1185)**; ctor binds the widget's single `SCR_InputButtonComponent`; `OnActivated` (1202) routes to group-header / category / item-click.
- `GRAD_ArsenalCategoryLabels` (1224): `LabelFor(int)` decimal-bit → name; `IsStackable(int)` (grenades/heal/smoke/equipment/explosives).
- `GRAD_ArsenalTabs` (1288): PRIMARY/SECONDARY/THROWABLES/APPAREL/CONTAINER masks, `Count()=5`, `LabelFor`, `MaskFor`, `IsStackableTab`.

### `scripts/Game/GRAD_Loadout/Menu/GRAD_ItemBrowser.c` (238 lines)
- `SetCategoryMask(int mask)` (81): sets `m_iCategoryMask`, clears single-type mode.
- `GetFiltered` (116): **mask branch at 125-130** — `if (m_iCategoryMask != 0) { if ((rec.m_iArsenalType & m_iCategoryMask) == 0) continue; }`; else single-type; then faction + search filters; sorts.
- `GetGrouped` (156): buckets filtered records by `m_sBaseName` into `GRAD_ItemGroup`s, sorts groups + items.
- `GRAD_ItemGroup` (227): `m_sLabel` + `m_aItems`.

### `scripts/Game/GRAD_Loadout/Utils/GRAD_InventoryLib.c` (594 lines)
- `GetStorageFillFraction` (298): occupied direct slots / total (occupancy metric, not weight).
- `StorageHasFreeSlot` (319).
- `CollectDestinationContainers` (233) + recursive worker (252): worn cargo garments only, filtered by `GRAD_ContainerTypes.MASK` via `index.GetArsenalTypeForPrefab`.
- `CountPrefabInstances` (378).
- `GRAD_ContainerTypes` (566): `MASK = BACKPACK(1<<7) | TORSO(1<<11) | VEST_AND_WAIST(1<<12) | LEGS(1<<13) | RADIO_BACKPACK(1<<15)`.
- `GRAD_ContainerRef` (580): storage + label + owner.
- Also: `SpawnLocal` (492), `CollectAllItems` (339), `GetPrefabResourceName` (95), `ClearStorages` (430).

### `scripts/Game/GRAD_Loadout/Service/GRAD_ArsenalService.c` (215 lines)
- Constructor (55): **seeds `m_iEntriesPerFrame=64` + `m_bAutoBuildIndex=true`** because class-spawn leaves `[Attribute]` defvalues at type defaults (0/false).
- `EOnInit` (70): registers singleton, builds `GRAD_CatalogIndex`, **`GetGame().GetCallqueue().CallLater(GradBuildDriver, BUILD_TICK_MS=33, true)`** (entity FRAME event was unreliable for a script-spawned entity).
- `GradBuildDriver` (95): logs first-tick catalog-mgr presence (diag), kicks `BeginBuild` when catalogs ready, `Tick`s the amortized build, updates the preload bar, `Remove`s itself on complete.
- `UpdatePreloadIndicator` (127): creates/updates/destroys `GRAD_PreloadIndicator`.
- `IsCatalogReady` (161): `m_CatalogIndex && m_CatalogIndex.IsComplete()`.
- Clipboard + last-used-name accessors; destructor removes the callqueue driver.

### `scripts/Game/GRAD_Loadout/Service/GRAD_PreloadIndicator.c` (122 lines)
Script-built bottom-left indicator (Frame + bg + label + bar track + bar fill). `Update(progress)` sizes the fill and updates the "Preloading arsenal… N%" label; hides at ≥1.0. `Destroy()` removes from hierarchy. No layout resource needed.

### `scripts/Game/GRAD_Loadout/Entry/GRAD_GMArsenalActions.c` (231 lines)
- `GRAD_GMOpenArsenalAction` (50): `CanBeShown` (77) **gated on `service.IsCatalogReady()`** (returns false until preload done), then requires a selected/hovered character. `Perform` (98) builds context from selected chars (fallback hovered) and `GRAD_ArsenalMenu.Open`.
- `GRAD_GMCopyLoadoutAction` (123) + `GRAD_GMPasteLoadoutAction` (176): clipboard copy/paste via service + RPC.

### `UI/Layouts/GRAD_ArsenalMenu.layout`
Root `FrameWidgetClass "GRAD_ArsenalMenuRoot"`:
- `Background` image, `Title` "ARSENAL MANAGER".
- `CategoryList` = **HorizontalLayoutWidgetClass** (the tab bar), anchored top-left, SizeX 560 / SizeY 56.
- `PreviewCharacter` = ItemPreviewWidgetClass, anchor 0.35 0 0.72 1, **`Enabled true` (line 49, the fix)**.
- `ItemListScroll` = ScrollLayoutWidgetClass (anchor 0 0 0.34 1) containing **`ItemList` = GridLayoutWidgetClass "{00F6B11784CDB106}"** (line 62 — was VerticalLayout).
- `SelectedPanel` = OverlayWidgetClass (bottom-left) with `SelBg/SelIcon/SelName/SelStats` + `SelButtons` VerticalLayout holding `ButtonAddVest/ButtonAddBackpack/ButtonAddEquip` (each inherits `WLib_ButtonText.layout` + an `SCR_InputButtonComponent` action `MenuSelect`).
- `LoadoutPanel` = VerticalLayoutWidgetClass (anchor 0.74 0 1 1) with Uniform/Vest/Backpack blocks: Header text + Bar image + Percent text + Contents vertical layout.
- `ButtonBar` (bottom-right) with `ButtonCancel` (DialogButtonCancel) + `ButtonOK` (DialogButtonConfirm).

### `UI/Layouts/GRAD_ItemCard.layout` (+ `.meta`)
Root **`OverlayWidgetClass "ItemCard"`** (rewritten):
- `SizeLayoutWidgetClass "TileSize"` with `WidthOverride 150` / `HeightOverride 116`, containing:
  - `TileBg` image (dark),
  - `TileContent` VerticalLayout > `CardIcon` (image) / `CardName` (text, centered) / `CardCount` (text, centered).
- **`CardButton` = a bare transparent `ButtonWidgetClass "{A0000000000000B0}"` sibling** (full-size overlay) with an `SCR_InputButtonComponent` action `MenuSelect` — the click target (a `WLib_ButtonText` button can't take extra children, so click is a separate bare button).
- `.meta` GUID: **`{A704EDAAAADC6ADB}`** (matches `CARD_LAYOUT` in the menu). The `.meta` is currently **untracked** in git (`?? GRAD_ItemCard.layout.meta`).

---

## What WORKS (verified live via screenshots/logs)

- Catalog preload builds (~464 records) via the callqueue driver; preload bar shows; GM "Open Arsenal" gated on ready.
- Menu opens; tab filter logic works (log showed `SelectTab 0 mask=306 catListFound=1`).
- Right loadout panel fully works: Uniform/Vest/Backpack fill bars + percent + contents lists.
- Item cards render as clickable buttons; catalog has 19 categories.
- Container-destination filtering (cargo garments only), minus-removes fix, perf precompute of counts.

---

## What is IN PROGRESS / BROKEN (current focus — NOT yet tested live)

1. **TILE GRID**: `ItemList` switched VerticalLayout → `GridLayoutWidget`; cards placed via `GridSlot.SetColumn/SetRow(card, cell%GRID_COLUMNS / cell/GRID_COLUMNS)`, `GRID_COLUMNS=2`. Card layout is now OverlayWidget root (`SizeLayout 150x116` > `TileBg` + `TileContent` VerticalLayout > icon/name/count) + a transparent full-size `CardButton` last. **Needs Workbench cold restart to test.**
2. **PREVIEW CAMERA**: was extreme-zoom (inside mesh) + no drag-rotate. Root cause: used base `new PreviewRenderAttributes()` (no character framing). Fix applied: `new SCR_CharacterInventoryPreviewAttributes()` (field still typed base to match `Update(inout ...)`). Also added `Enabled true` to `PreviewCharacter` in the layout. **Not yet tested.**
3. **Item click → Selected panel → ADD buttons**: unconfirmed whether clicks fire (log never showed `OnItemRowClicked` — user may not have clicked, or wiring issue). Diagnostics present (`OnItemRowClicked` log, `m_bBoundOk` field).

---

## Known engine/API facts (VERIFIED — do not re-research)

- `WrapLayoutWidget` does NOT exist. Use `GridLayoutWidget` + manual `GridSlot.SetColumn/SetRow` (no auto-wrap).
- `SetWidthOverride/SetHeightOverride` exist ONLY on `SizeLayoutWidget`, NOT base `Widget`.
- A `ButtonWidget` accepts exactly ONE child. `WLib_ButtonText` already has a child → you CANNOT add children to a button inheriting it ("does not accept more children"). Use OverlayWidget root + a transparent bare `ButtonWidget` sibling for the click target.
- `OverlayWidgetSlot` is the correct slot class under `OverlayWidget` (NOT "OverlaySlot").
- `SCR_InputButtonComponent.FindComponent(rootWidget)` searches the hierarchy (finds a nested button component). `m_OnActivated` invoker for clicks.
- `SCR_UIInfo.HasIcon()` + `SetIconTo(ImageWidget)` for icons; give the icon a fixed size or it may render 0-size.
- Preview camera: **NO bone/target/pivot/focus API exists.** `PreviewRenderAttributes` only has `RotateItemCamera(deltaRot, limitMin, limitMax)`, `ResetDeltaRotation()`, `ZoomCamera(inc, minFOV, maxFOV)`. Per-item body focus (weapon=full / headgear=head / etc.) can ONLY be approximated via per-slot Zoom+Rotate presets — a true focus API does NOT exist. This is an engine limitation to tell the user.
- Class chain: `SCR_CharacterInventoryPreviewAttributes` → `PreviewRenderAttributes` → `BaseItemAttributeData`; `SCR_InventoryCharacterWidgetHelper` → `ScriptedWidgetEventHandler`.
- Layout resource GUID lives in the `.layout.meta`; the code `ResourceName` must match that GUID (e.g. `CARD_LAYOUT {A704EDAAAADC6ADB}` matches `GRAD_ItemCard.layout.meta`; `ROW_LAYOUT {4BE35AEBB44455F0}` matches `GRAD_ListButtonRow.layout.meta`).

---

## CRITICAL WORKFLOW GOTCHA

Workbench's `wb_reload` / `wb_play` run STALE compiled scripts — code edits do NOT take effect until a FULL Workbench PROCESS restart (kill + reopen project). `wb_launch` no-ops when Workbench is already running. **Every test cycle requires the user to fully kill and relaunch Workbench.**

Also: editing a `.layout` while it is OPEN in the Workbench layout editor can cause Workbench to overwrite the disk file (reverting your changes) — the card layout got reverted this way once.

---

## Git state

- Last commit on `main`: **`c498891` "Arsenal: mock-based UI redesign + preview/preload/perf fixes"**.
- Uncommitted since (from `git status`):
  - Modified: `UI/Layouts/GRAD_ArsenalMenu.layout`, `UI/Layouts/GRAD_ItemCard.layout`, `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c`, plus incidental `resourceDatabase.rdb`, `GRAD_ListButtonRow.layout`, `GRAD_ListQtyRow.layout`, `WLib_ButtonText.layout`, `Menus/Dialogs/GroupLeaderReplacementRequest.layout`.
  - Untracked: **`UI/Layouts/GRAD_ItemCard.layout.meta`** (must be `git add`ed when committing).
- Commit only when the user asks.

---

## NEXT STEPS (immediate)

1. User cold-restarts Workbench, opens the arsenal.
2. Verify: tile grid renders as a 2-col grid of icon cards; clicking a card logs `OnItemRowClicked` and fills the Selected panel; ADD TO VEST/BACKPACK work; preview shows the full character (not zoomed) and drag-rotates.
3. Then: per-slot camera zoom/rotate presets for body focus (engine limitation: no true focus API); remove diagnostics; polish tile sizing/columns; commit (remember to add the `.meta`).

---

## Remaining polish / TODO

- Diagnostics to remove: `OnItemRowClicked` log (menu ~653), `m_bBoundOk` field (menu 604 + handler 1185), `SelectTab` log (menu 436-437), `BuildDriver first tick` log (service 98-100).
- Tile column count (`GRID_COLUMNS=2`) and tile size (150x116) may need tuning for the panel width (`ItemListScroll` anchored 0..0.34).
- Item icons: many catalog items may lack icons (`HasIcon` false) → icon hidden, name-only card.
- Preview: body-region focus via presets is a nice-to-have, not blocking.
