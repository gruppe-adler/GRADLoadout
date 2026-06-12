# Packaging & publishing

## Exclude from the published addon

The following paths are **development-only** and must never be bundled into the Workshop
upload:

- `inspiration/` — clean-room reference material (see [PROVENANCE.md](PROVENANCE.md)). Not read
  during implementation; not part of the addon.
- `docs/` — optional; may be shipped or stripped at preference. Keeping `PROVENANCE.md` and
  `LICENSE.md` in the upload is recommended for transparency.
- `*.tmp`, build artifacts.

When publishing through the Workbench Addon Manager / Workshop uploader, confirm the upload
manifest does not list `inspiration/`. Because `inspiration/` contains no `.gproj`-registered
resources and is referenced by nothing in the project, the resource packer will not pull it in
automatically — but verify the file list before each publish regardless.

## Addon identity

- Project ID: `GRADLoadout`
- GUID: `699932551921B9F3` (from `addon.gproj`)
- Script prefix: `GRAD_`

## Build verification (per phase)

1. Open the project in Arma Reforger Tools (Workbench) with NET API enabled.
2. Script compile must be clean (no errors).
3. Run the phase's manual test checklist (see [ROADMAP.md](ROADMAP.md)).
4. Record a one-line changelog note.
