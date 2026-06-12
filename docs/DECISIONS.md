# Design decisions

Records the open decisions the implementer must make and document (spec §7). Each entry is
updated as the relevant phase is reached. Entries marked **OPEN** are not yet resolved.

---

## D1 — Preview character approach

**Decision (FINAL):** Use the engine's **`ItemPreviewManagerEntity` + `ItemPreviewWidget`**, not a
dedicated preview world / RTT. The preview character is a local-only entity
(`GRAD_InventoryLib.SpawnLocal`) shown via `ItemPreviewManagerEntity.SetPreviewItem(widget,
character)`, with `SCR_InventoryCharacterWidgetHelper` providing mouse orbit + wheel zoom.

**Why:** My provisional concern (that the preview manager only does item thumbnails) was wrong
once verified against the live API: `SetPreviewItem` accepts any `IEntity` including a full
character, and the system auto-refreshes the render when the character's inventory hierarchy
changes — exactly the live-edit behaviour an arsenal needs. Vanilla precedents confirm the path:
`SCR_LoadoutPreviewComponent` (previews a character loadout into the widget) and
`SCR_InventoryCharacterWidgetHelper` (orbit/zoom camera, used by the inventory screen). This
removes an entire custom preview world + render-to-texture pipeline — less to author, fewer
moving parts, and reuses the same preview the base game's inventory uses.

**Status:** FINAL (verified against live API, P5).

---

## D2 — `modded class` vs composition for catalog access

**Decision (provisional, to finalize in P4):** Prefer **composition** — a `GRAD_` helper that
queries `SCR_EntityCatalogManagerComponent` through its public API — over `modded class`,
wherever the public API is sufficient. Fall back to `modded class` only for hooks the public
API does not expose.

**Why:** Composition avoids `modded class` collisions with other mods touching the same engine
classes, improving mod compatibility. To be confirmed once the exact catalog query surface is
verified in P4.

**Status:** PROVISIONAL.

---

## D3 — JSON loadout schema + versioning

**Decision:** Recursive entity tree. Original field names; a `version` int from day one.
Draft schema (subject to refinement in P2):

```
GRAD_LoadoutEntry {
  int      schemaVersion   // bumped on breaking schema changes; v1 to start
  string   prefab          // ResourceName of the entity prefab
  int      slotIndex       // index within the parent storage / slot array (-1 = any)
  string   storageId       // stable identifier of the parent storage (class-derived)
  int      quantity        // stack count for stackables (1 default)
  array<ref GRAD_LoadoutEntry> children  // recursive sub-items
}
GRAD_LoadoutFile {
  int      schemaVersion
  string   name
  int      createdUnix
  ref GRAD_LoadoutEntry root
}
```

**Why:** A recursive tree mirrors the inventory hierarchy directly and round-trips cleanly. A
`schemaVersion` field both enables forward migration and differentiates the format. Field names
are chosen independently. Finalize concrete field set in P2 after verifying slot-addressing API.

**Status:** DRAFT — finalize in P2.

---

## D6 — RPC host: component on player controller vs. `modded class SCR_PlayerController`

**Decision:** Put the server-authoritative RPCs on a dedicated **`GRAD_LoadoutManagerComponent`
attached to the SCR_PlayerController prefab**, rather than `modded class SCR_PlayerController`.

**Why:** `Rpc()` and `[RplRpc]` live on `GenericComponent`/`GenericEntity`, so a component hosts
RPCs natively. This mirrors the engine's own pattern (`SCR_PlayerControllerGroupComponent`,
`SCR_PlayerControllerCommandingComponent`, both with a static `GetPlayerControllerComponent(id)`
accessor). It avoids a `modded class` collision with any other mod that mods the controller —
consistent with the composition-over-modding stance in D2 — while fully satisfying the spec's
intent of a server-authoritative flow keyed to the player controller. Wired by overriding
`Prefabs/Characters/Core/DefaultPlayerControllerMP_Factions.et` into GRADLoadout and adding the
component (verified attached, idx 3). The component is reached
via `PlayerManager.GetPlayerController(id).FindComponent(GRAD_LoadoutManagerComponent)`.

**Status:** DECIDED.

---

## D4 — RPC payload limits

**Status:** OPEN — to be measured experimentally in P3. Hypothesis: serialized loadout strings
may exceed the engine's per-RPC string cap for heavy loadouts; if so, chunk the string across
multiple ordered RPCs and reassemble server-side, or compress. Findings recorded here in P3.

---

## D5 — UI structural differentiator

**Decision (FINAL):** **Left-rail category navigation.** A vertical category rail (Weapons /
Uniform / Vest / Headgear / Backpack / Attachments / Loadouts) drives a single content panel on
the right; the preview character sits center-left. One category in focus at a time, instead of
the classic multi-panel arsenal grid that shows every slot and list simultaneously.

**Why:** This is the deliberate structural difference from existing arsenals (which favour a
dense multi-panel grid). A single-focus content area is calmer, scales better to gamepad
navigation (rail = vertical list, content = grid), and gives room for a large legible item
browser. Chosen by Nomi (design lead). The item browser within each category still carries
search + filter (P6), so power users aren't slowed down.

**Status:** FINAL.
