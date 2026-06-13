# Provenance — clean-room statement

GRAD_Loadout is a virtual arsenal for Arma Reforger built **clean-room from a behavioral
specification**. This document records the process and posture, and is kept in the repository
as evidence of independent authorship.

## What was used

- A written **behavioral specification** describing how a virtual arsenal should *function*
  (slots, preview, save/load, permissions, entry points). The spec transmits function, not
  expression.
- Bohemia Interactive's **public Enfusion / Arma Reforger modding API** and standard modding
  idioms (modding `SCR_PlayerController`, `[RplRpc]` attributes, `JsonApiStruct`,
  `SCR_EntityCatalogManagerComponent`, `MenuBase`, `ScriptInvoker`, the Workbench layout
  editor). These are the platform.

## What was NOT used

- No source code, layout files, configuration files, world/entity files, assets, identifiers,
  comments, or text from any third-party mod were opened, read, extracted, or copied.
- The repository contains a folder named `inspiration/` which holds reference material that is
  **deliberately not read** during implementation and is **excluded from the built addon**.
  It exists only as an out-of-band artifact and crosses no part of the clean-room wall. No
  file inside it has been opened by the implementer.

## Originality measures

- Script prefix `GRAD_` throughout; no third-party prefixes or identifiers, including in log
  strings.
- The loadout JSON schema uses original field names and carries a `version` field; it is not
  field-for-field identical to any existing format.
- All `.layout`, `.conf`, `.et`, `.ent` files are authored fresh in the Workbench; project
  GUIDs are generated per project.
- All UI copy is original and localized (English + German).
- The UI carries at least one deliberate structural difference from existing arsenals (see
  [DECISIONS.md](DECISIONS.md)).

## Clean-room integrity event (2026-06-12)

During P5 UI work, the contents of the reference mod's arsenal layout
(`inspiration/.../BRI_ARS_ArsenalMenu.layout`) were pasted into the working session with a
suggestion to "take inspiration but rename." This was **declined**: authoring our layout against
the reference's expression — even with renamed identifiers/GUIDs — would make it a derivative
work and defeat the clean room. Our `GRAD_ArsenalMenu.layout` and row layouts were instead built
from (a) the behavioral spec, (b) Nomi's independent left-rail design decision (DECISIONS.md D5),
and (c) **vanilla Bohemia** widget-library layouts (`WLib_ButtonText`, `ListBox`,
`WLib_EditBoxSearch`, dialog button prefabs) — BI content, which is fair to reference. No
structure, naming, or expression from the reference layout was used.

## Honest caveat

This is a **one-way clean room**: the specification author was aware of a reference mod's
structure. The implementation, however, is authored solely from the behavioral specification
and the public API. This posture — function transmitted, expression independently created — is
the standard and defensible basis for reimplementations. This document is retained as
provenance.
