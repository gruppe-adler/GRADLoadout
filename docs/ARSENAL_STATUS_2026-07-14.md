# Arsenal Menu — Current Status (2026-07-14)

**See `docs/HANDOFF_2026-07-15.md` first** — it supersedes this doc's "what to do next" framing and
records which of the conclusions below were extrapolations rather than findings.

This is the up-to-date status doc. The other `ARSENAL_REDESIGN_STATUS.md` / `ARSENAL_HANDOVER_2026-07-12*.md` /
`PLAN_TABS_AND_CAMERA_2026-07-13.md` files are historical records of the debugging journey (kept for
reference — several theories in them were disproven later and are corrected here) — do not treat them
as current.

Primary files: `scripts/Game/GRAD_Loadout/Menu/GRAD_ArsenalMenu.c`,
`scripts/Game/GRAD_Loadout/Loadout/GRAD_LoadoutApply.c`, `UI/Layouts/GRAD_ArsenalMenu.layout`.

## Confirmed WORKING (live-tested)

- **Category tabs** (Primary/Secondary/Throwables/Apparel/Container): clicking and Q/E paging both
  work, one step per press, grid re-filters correctly. Root cause was `Widget.FindHandler` returning a
  dataless "zombie" `SCR_TabViewComponent` instance (our layout's `components` block ADDS a second
  instance instead of overriding the vanilla-imported one); fixed by enumerating all handlers via
  `Widget.GetNumHandlers()`/`GetHandler(int)` and keeping the one whose `GetShownTab()` is ever valid.
  The Q/E double-advance was a SEPARATE bug: vanilla `SCR_PagingButtonComponent.SetAction()` wires BOTH
  an `InputManager.AddActionListener` and the button's own `OnClick` to the same callback, firing twice
  per press before we ever get to see an intermediate value. Fixed by muting the vanilla component's own
  listener (`SetListenToActions(false)`) and driving `ShowTab`/`GetNextValidItem` from our OWN single
  `EActionTrigger.DOWN` listener on the same already-valid `MenuTabLeft`/`MenuTabRight` action names.
- **Item grid**: 3 columns, uses the full pane width, cards appear immediately without needing a scroll
  first.
- **Double-click to auto-add**: routes to `GRAD_ArsenalMenu.OnItemRowDoubleClicked` — tries vest →
  backpack → any container with room → equip, in order.
- **Loadout apply/confirm**: the real character now visibly changes after Confirm — live-confirmed
  across several sessions (`RpcAsk_Apply ok=1`, 20-40+ items landing correctly each time). See
  "Inventory placement" below for the fix that made this work.

## Camera architecture — isolated BaseWorld + visual-clone split. Needs live test.

**World/camera plumbing** (mirrors vanilla `SCR_InventoryInspectionUI.CreatePreview()` — verified
verbatim from real source; note that screen previews a single static ITEM, so it is a reference for the
plumbing ONLY, not for entity handling): create a **second, isolated `BaseWorld`** via
`BaseWorld.CreateWorld("InspectionPreview", "InspectionPreview")`, hold its `SharedItemRef` (that ref is
what keeps the world alive), configure camera **slot 0** in it (free — no competing main camera),
`Resource.Load("{4391FE7994EE6FE2}worlds/Sandbox/InventoryPreviewWorld10.et")` + `SpawnEntityPrefab`
for the vanilla ground/sky/lights "studio set" (a fresh BaseWorld is EMPTY — no light at all; the load
is `IsValid()`-guarded exactly as vanilla guards it), then `m_wPreview.SetWorld(m_PreviewWorld, 0)`.
The camera is positioned by writing the slot with `BaseWorld.SetCameraEx(0, mat[4])` — **re-pushed every
frame from `OnMenuUpdate`**, because a one-shot write does NOT stick (live-proven: `CameraPositionDiag`
read back `<0,0,0>` every time via `BaseWorld.GetCamera(0, out mat)`, the verified counterpart). Vanilla's
own `UpdateView` likewise re-writes its slot every frame; that is not incidental to its orbit animation.

**Entity split** (two entities, two jobs — matches a real, working loadout-editor mod):
- `m_PreviewCharacter` — the real, MUTABLE clone, in the **LIVE world**, where its inventory actually
  works. Never rendered directly. Still spawned 5m to the primary target's right at ground level.
- `m_PreviewVisual` — a **visual-only** clone in the isolated world, from
  `InventoryItemComponent.CreatePreviewEntity(world, camera)` called on `m_PreviewCharacter`'s own item
  component. This is what the widget shows; `PositionCamera` frames THIS.
- Rebuilt on every mutation (`RefreshPreviewRender` → `RebuildPreviewVisual`) — the reference mod's own
  design, not a workaround.

**Why the split**: a first pass put the mutable clone directly in the isolated world. That broke the
inventory — a fresh `BaseWorld.CreateWorld` world has **no game systems**, so the clone's inventory
manager silently no-ops (live-proven: `Apply: spawned 8 items` where 30 expected with zero failures
logged; `Capture: 'ArsenalResult' -> 0 nodes` with all five roots `0 occupied`; Confirm refusing with
"empty loadout", `ok=0`). Same silent-no-op failure class as the old SpawnLocal+insert bug, new cause.

**Two corrections to earlier revisions of this doc, worth stating plainly**: (1) the split was justified
here as "what vanilla does" — that was an extrapolation from the single-item inspection screen, not a
finding; the real basis is the working reference mod. (2) The visual clone briefly used
`SCR_CharacterInventoryStorageComponent.CreatePreviewEntity` because that signature exists and sounded
character-shaped — signature-driven guessing; the reference uses the plain `InventoryItemComponent`
overload for a whole dressed character.

**GRAD_Loadout's own twist vs. the reference**: the reference previews the *actual live edited
character*; GRAD_Loadout edits a *clone* so the player can cancel. So our clone is the analogue of its
live character, and `CreatePreviewEntity` is called on the clone. That mapping is our inference.

`GRAD_InventoryLib.SpawnLocal` gained an optional trailing `BaseWorld world = null` during the first
pass; existing callers unaffected, and the preview clone no longer passes it (it wants the live world).

**Separate mechanism, do not conflate**: `ItemPreviewManagerEntity` (via
`ChimeraWorld.GetItemPreviewManager()`) is the engine service for small GRID ICON thumbnails —
`ResolvePreviewEntityForPrefab` spawns a throwaway entity to read name/type, then deletes it. Unrelated
to the 3D pane. Used correctly in `CreateItemCardWidget`; leave it alone.

**The whole camera-slot-in-the-LIVE-world approach was a dead end** — kept here only so nobody re-treads
it. Four attempts, all live-tested: (1) unparented `SCR_PIPCamera` → rendered nothing; (2) parented to
the preview character → camera ~1km away underwater; (3) parented to a boneless `GenericEntity` anchor →
**identical** result to (2), disproving the bone-pivot theory that motivated it; (4) diagnostics finally
proved `SetWorldTransform` **never committed on the camera in any variant** (read-back was always
`<0,0,0>`, the spawn-time identity) — parenting was never the real variable. A slot-0 isolation test had
separately proven `RenderTargetWidget`/`SetWorld` itself works fine, which is what pointed at the camera
entity rather than the widget. The isolated-world approach had the right answer the whole time.

Two separate, already-fixed bugs from that era, still relevant:
- Character spawn position: `vector.Zero` landed in open ocean; 500m up caused a physics free-fall into
  random geometry. Fixed by spawning at the SAME ground level as the primary target, offset 5m along its
  own right axis. (The isolated world may make this moot — the clone no longer needs to be anywhere
  particular in the live world for the preview to work.)
- **"Dead/ragdolled clone" theory — disproven.** `ECharacterLifeState` is `{ ALIVE, INCAPACITATED, DEAD }`
  — **ALIVE = 0**. `GetLifeState()=0` always meant healthy.

## Inventory placement — 3 bugs found + fixed this session

1. **Preview character rendered NAKED / real character didn't change on Confirm** — `SpawnInto`'s LOCAL
   branch used to `SpawnLocal` a bare entity then hand it cold into `TryInsertItemInStorage`/
   `TryInsertItem`/`EquipAny`; those calls returned `true` while doing nothing (worldPos diagnostic
   proved items stayed at `<0,0,0>`). **Fixed**: switched to `TrySpawnPrefabToStorage` (atomic
   spawn+insert), matching the REPLICATED branch's already-working strategy. **Live-confirmed.**
2. **Vest suspenders/scabbard self-collision** — target slot occupied by the SAME prefab (prefab
   baseline gear on the clone, or a `ClearStorages` removal failure on these specific attachment-style
   items). **Fixed**: both branches now skip re-placing if the slot already holds the same prefab.
   `ClearStorages`' removal-failure log bumped Debug→Warn to surface any real removal failures.
   **Live-confirmed** (no longer seen failing in later tests).
3. **Headgear/uniform/backpack fail to equip when something is already worn** — `TrySpawnPrefabToStorage`
   only inserts into a FREE slot, never replaces an occupied one. First fix attempt (`TryEquipReplace`
   v1: search every storage for a free slot, spawn there, then `EquipAny`) was ALSO diagnosed broken —
   but for a genuine, non-buggy reason: most storages on a character (weapon slots, ammo pouches) are
   type-restricted and correctly reject an arbitrary clothing prefab (diagnostic showed "checked 16
   storages, found 17 empty slots, none accepted this prefab" for a plain Boonie Hat — there's no
   generic "holds anything" storage to use as a waypoint). **Current fix**: `EquipAny`'s own real body
   explicitly handles an item with no parent slot AT ALL yet — so `TryEquipReplace` now just spawns the
   prefab as a bare, un-inserted entity (`Game.SpawnEntityPrefab` for replicated, `SpawnEntityPrefabLocal`
   for local preview) and hands it straight to `EquipAny`, skipping the holding-slot step entirely.
   **CAVEAT**: the local path's `SpawnEntityPrefabLocal` is the exact same call that was proven broken
   earlier this session for a DIFFERENT combination of calls (`TryInsertItemInStorage`/`TryInsertItem`);
   it's unconfirmed whether `EquipAny` specifically behaves differently. **Not yet live-tested with this
   rewrite** — check whether swapped items actually appear on the model, not just whether the log says
   "spawned 1 items" (that log line was also true for the earlier, disproven-broken pattern).

## New: unequip a worn garment (uniform/vest/backpack)

There was no UI to remove a WORN GARMENT itself (only its contents, via the `[-]`/`[+]` lines). Added a
"REMOVE" button to each loadout-panel slot header (`ButtonRemoveUniform`/`ButtonRemoveVest`/
`ButtonRemoveBackpack` in `UI/Layouts/GRAD_ArsenalMenu.layout`), wired via a new `GRAD_UnequipHandler`
class routing to `OnUnequipGarment(arsenalTypeBit)` in `GRAD_ArsenalMenu.c`. Uses a new
`FindNamedGarmentOwner` helper to locate the worn entity, then removes it via the EXISTING
`RemoveOneFromPreview` mechanism (reused, not reimplemented) and refreshes every panel + the preview
render immediately. **Needs BOTH the updated layout AND scripts reimported. Not yet live-tested.**

## Open issues (next priorities, in order)

1. **Live-verify ALL of tonight's fixes together** in one fresh test (full reimport of both scripts and
   layout): camera anchor (does the preview actually show the character now, at the right position?),
   headgear/uniform/backpack swap (`TryEquipReplace`), and the new REMOVE buttons. See
   `docs/TODO_FABLE_NEXT.md` for exact things to check.
2. **Why does `TryRemoveItemFromInventory` seem to fail on some attachment-style items specifically**
   (seen on `Vest_ALICE_suspenders_*`/`Scabbard_Bayonet_M9` earlier, under `force=true`, which should
   strip everything)? The self-collision fix papers over the symptom but doesn't explain the underlying
   removal failure. Watch for `ClearStorages: could not remove ... (force=1)` warnings in future logs.
3. **"Mark missing containers on the right pane"** — user-requested UI affordance, not yet implemented:
   when the preview character has no backpack/vest equipped, the loadout panel should visibly mark the
   missing container (currently just shows `[Empty]`). Now that REMOVE exists, `[Empty]` should mean
   something real and reliable — revisit this once the REMOVE buttons are confirmed working.

## Hard constraints (unchanged, still apply)

- EnforceScript has **no ternary `?:`** and **no `??`** — plain `if/else` only.
- Never set `CategoryTabView`'s layout slot to `SizeMode Fill` (user reverted this personally; stays
  `Auto`).
- Never call `CameraManager.SetCamera(...)` on the preview camera — hijacks the full screen.
- Don't touch the item-card thumbnails (`ItemPreviewWidget` + `SetPreviewItemFromPrefab`) — confirmed
  working, unrelated to the center character preview's `RenderTargetWidget` path.
- Don't read `inspiration/` (forbidden clean-room reference — see `docs/PROVENANCE.md`).
- `arexplorer.zeroy.com` mirrors full vanilla script source WITH method bodies (the local
  `mcp__enfusion-mcp__api_search` tool only has signatures) — use it for "what does this vanilla method
  actually do" questions. `InventoryStorageManagerComponent` and friends are native `proto external`
  with NO script source anywhere — root-cause those only via live behavioral testing.
- **Don't assume a vanilla usage pattern transfers safely to a different KIND of owner entity.** The
  camera-parenting saga this session: the same `AddChild(entity, -1, AUTO_TRANSFORM)` call behaves
  differently depending on whether the parent has a skeleton (a character) or not (a weapon, or a
  plain `GenericEntity`) — always check what vanilla actually calls a given method ON, not just that
  the call signature matches.
