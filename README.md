# GRAD_Loadout

A virtual arsenal addon for **Arma Reforger** by Gruppe Adler.

Full-screen equipment editor with live character preview, named loadouts saved/loaded as
JSON, and server-authoritative application with permission checks. Usable by players (via an
arsenal box prefab) and by Game Masters (via editor context actions on any unit).

The addon also ships a dependency-free utility script module
(`scripts/Game/GRAD_Loadout/Utils/`) of inventory/slot helpers that other Gruppe Adler mods
may depend on.

## Status

In development. See [docs/ROADMAP.md](docs/ROADMAP.md) for phase tracking and
[docs/DECISIONS.md](docs/DECISIONS.md) for design-decision records.

## Requirements

- Arma Reforger (current stable branch)
- No third-party mod dependencies

## Features (target)

- Live preview character with orbit/zoom camera
- Slot-aware item browser with search and filtering
- Attachment / sub-item management (magazines, optics, etc.)
- Save / load / delete named loadouts (local JSON under the game profile)
- Server-authoritative apply with ownership + Game Master / admin permission gate
- Arsenal box prefab with "Open Arsenal" and "Load previous loadout" actions
- Game Master editor actions: Open Arsenal, Copy / Paste loadout, multi-unit apply
- English and German localization

## Provenance & licensing

This addon is **independently authored from a behavioral specification** (a functional
description of how a virtual arsenal should behave). No code, layout, configuration, asset,
identifier, or text from any third-party mod is included. Vanilla Bohemia Interactive content
is referenced by GUID / prefab inheritance only — nothing is extracted or redistributed.

See [docs/PROVENANCE.md](docs/PROVENANCE.md) for the full clean-room statement and
[LICENSE.md](LICENSE.md) for licensing.

## Credits

- Design & implementation: Gruppe Adler (Nomi)
- Built on Bohemia Interactive's public Enfusion / Arma Reforger modding API.
