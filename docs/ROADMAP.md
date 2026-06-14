# Roadmap

Phase tracking for GRAD_Loadout. Each phase ends with: compiles clean, manual test checklist
run, short changelog note.

| Phase | Scope | Status |
|---|---|---|
| **P0** | Scaffold: project, folders, LICENSE/README, provenance, decision log | Done |
| **P1** | Utils module: `GRAD_InventoryLib`, `GRAD_Sorter`, common utils; Workbench test script green on a vanilla character | Compiles clean — pending in-game probe run |
| **P2** | Data model: capture → JSON → apply round-trip on a local character (no UI/network) | Compiles clean — pending in-game round-trip run |
| **P3** | Replication: controller-component RPCs + permission gate; MP peer test | Compiles + component wired onto controller prefab (verified attached at idx 3) — pending MP peer permission test |
| **P4** | Catalog index + singleton service entity; amortized build, faction filtering, slot-allowlist config | Compiles clean — pending in-world index build verification + service entity placement |
| **P5** | Menu MVP: preview character + camera, equipment + item browser panels, OK/Cancel apply | **WORKING LIVE** — menu opens in-game (menu-config override live), full-screen layout renders, catalog builds (492 items/22 cats), category rail + item list both populate, OK/Cancel render. Remaining polish: confirm category-click refilter, preview character (opens targetless in debug), stringtable for labels |
| **P6** | Full UI: attachments panel, save/load/delete, filter/search, gamepad nav, localization | Persistence store (`GRAD_LoadoutStore`: save/load/delete/list under $profile:) authored. Pending: browser population, attachments panel, gamepad, stringtable (Workbench) |
| **P7** | Entry points: arsenal box prefab actions; GM context actions incl. copy/paste + multi-select | **GM right-click WORKING** — "Open Arsenal" appears in the unit context menu (registered in the real override `{2C1D87DE93C77A27}TempEdit.conf`, action classes need `[BaseContainerProps()]`), opens the arsenal on the right-clicked unit with a live preview character. Pending: arsenal box prefab; refine apply for cloth-node/weapon-attachment storages (~12 items skip) |
| **P8** | Hardening: death-while-open, missing prefabs, RPC size limits, dedicated-server smoke, perf pass | Pending |

## Verification posture

All engine API signatures are re-verified against the live script API (`api_search` /
Workbench) at implementation time. The specification is not trusted over the live API.
In-Workbench steps (compile, layout editor, prefab/world authoring, build) require Arma
Reforger Tools running with the NET API + MCP handler.

## Pending live verification (when Workbench connects)

These were authored against the verified script API but not yet compiled/run live:

- **Compile** the whole `scripts/Game/GRAD_Loadout/Utils/` module clean.
- **Run** `GRAD_UtilsTest.RunOn(<rifleman>)` on a vanilla US rifleman; confirm: editable slots
  exclude locked cosmetic nodes; weapon slots enumerate; prefab counts look sane.
- Confirm the generic `GRAD_Sorter<Class T>` template instantiates and sorts (EnforceScript
  generic-class support — verify the `<Class T>` syntax compiles as written).
- Confirm `SpawnEntityPrefabLocal` path produces a usable local entity for preview.

## Changelog

- _P0_ — project scaffold, licensing, provenance, decision log. Done.
- _P1_ — Utils module authored: `GRAD_Log`, `GRAD_CommonUtils`, `GRAD_Sorter`,
  `GRAD_InventoryLib` (+ `GRAD_SlotRef`), and `GRAD_UtilsTest` probe. All engine APIs verified
  via `api_search`. Pending live compile/test.
- _P6/P7 (script side)_ — authored & compiling (PlayMode entered clean): `GRAD_LoadoutStore`
  (save/load/delete/list named loadouts under `$profile:GRAD_Loadout/loadouts/`), arsenal-box
  user actions (`GRAD_OpenArsenalAction`, `GRAD_LoadPreviousAction`), and GM editor context
  actions (`GRAD_GMOpenArsenalAction`, `GRAD_GMCopyLoadoutAction`, `GRAD_GMPasteLoadoutAction`,
  multi-select + clipboard via the service). Remaining P5/P6/P7 work is Workbench-bound artifacts
  (menu-preset config, stringtable, arsenal-box prefab, service placement) tracked in
  docs/WORKBENCH_TASKS.md, plus item-browser population + attachments panel script.
- _P5_ — arsenal menu: `GRAD_ArsenalMenu` (ChimeraMenuBase) + `modded ChimeraMenuPreset` compile;
  `UI/Layouts/GRAD_ArsenalMenu.layout` authored and validated (left rail + ItemPreviewWidget +
  item list + OK/Cancel; GUID {88FFBAB9523831E0}). Preview via ItemPreviewManagerEntity (D1 final).
- _P4_ — Catalog index + service authored & compiling: `GRAD_ArsenalItemRecord` (+ name sorter),
  `GRAD_CatalogIndex` (frame-amortized build from `SCR_EntityCatalogManagerComponent` via
  composition — confirms D2; general + per-faction ITEM catalogs; `OnComplete` invoker),
  `GRAD_ArsenalService` (singleton GenericEntity: owns the index, drives it from `EOnFrame`,
  GM copy/paste clipboard, last-used-loadout session state; `[Attribute]`-tunable entries/frame +
  auto-build). Pending: place the service entity in a world and confirm the index populates.
- _P3_ — Replication authored & compiling: `GRAD_LoadoutManagerComponent` (controller-attached
  RPC host: apply + request/response with request-id + 6s timeout + callback, payload-size warn),
  `GRAD_LoadoutPermissions` (ownership / admin / non-limited GM gate), `GRAD_ReplicationTest`.
  PlayMode launches clean — all four uncertain APIs type-checked (`[RplRpc]`, `RplComponent.GetEntity()`,
  `ScriptComponent.GetOwner()`, `EPlayerRole.ADMINISTRATOR`/`SESSION_ADMINISTRATOR`). Pending:
  attach the component to a player-controller prefab + run the host/client permission matrix.
  NOTE: the editor test world has duplicate game-mode/map entities (vanilla `SCR_MapEntity`
  UpdateViewPort null-ref spam) — a world-setup artifact, not GRAD code.
- _P2_ — Data model authored: `GRAD_LoadoutEntry` (recursive node), `GRAD_LoadoutData`
  (file/string persistence via `SCR_Json*Context`, schema v1 + version gate), `GRAD_LoadoutCapture`
  (full/filtered), `GRAD_LoadoutApply` (replicated + local-preview paths, resilient skips,
  created-entity cleanup), and `GRAD_LoadoutRoundTripTest`. Key insight: equipped weapons live in
  `EquipedWeaponStorageComponent` which IS a `BaseInventoryStorageComponent`, so the single
  recursive storage walk covers clothing, weapons, and attachments uniformly. Pending live
  compile/test of: generic `GRAD_Sorter<Class T>`; JSON member auto-serialization of
  `array<ref GRAD_LoadoutEntry>`; `SpawnEntityPrefabLocal` preview path.
