# Workbench-bound tasks (GUI artifacts for Nomi)

These few artifacts must be authored in the Workbench editor (the MCP write-tools no-op on
resource registration / stringtable / menu-config). Everything else is script and is done on
disk. Each task lists exactly what the script side expects.

## 1. Menu-preset config — makes the arsenal menu openable

**Goal:** bind `ChimeraMenuPreset.GRAD_ArsenalMenu` → the layout + the `GRAD_ArsenalMenu` class.

- Layout resource: `{88FFBAB9523831E0}UI/layouts/GRAD_ArsenalMenu.layout` (already registered).
- Menu class: `GRAD_ArsenalMenu` (script, compiles).
- Preset enum value: `ChimeraMenuPreset.GRAD_ArsenalMenu` (added via `modded enum`, compiles).

**How:** duplicate the vanilla menu-preset config (the one `ArmaReforgerScripted.GetMenuPreset()`
points at — a big list of `MenuPreset` entries) into the project, and add one entry:
- preset = `GRAD_ArsenalMenu`
- layout = the GUID above
- class = `GRAD_ArsenalMenu`

(Override gives an empty shell — must DUPLICATE the full config to keep all vanilla entries.)

## 2. Localization stringtable — UI text

Create a stringtable (`.conf`) for the project, then add keys (en + de):

| Key | en_us | de |
|---|---|---|
| `GRAD_ARSENAL_TITLE` | Arsenal | Arsenal |
| `GRAD_ARSENAL_OK` | Apply | Übernehmen |
| `GRAD_ARSENAL_CANCEL` | Cancel | Abbrechen |
| `GRAD_ARSENAL_SAVE` | Save Loadout | Ausrüstung speichern |
| `GRAD_ARSENAL_LOAD` | Load Loadout | Ausrüstung laden |
| `GRAD_ARSENAL_LOAD_PREVIOUS` | Load Previous | Vorherige laden |
| `GRAD_ARSENAL_CAT_WEAPONS` | Weapons | Waffen |
| `GRAD_ARSENAL_CAT_UNIFORM` | Uniform | Uniform |
| `GRAD_ARSENAL_CAT_VEST` | Vest | Weste |
| `GRAD_ARSENAL_CAT_HEADGEAR` | Headgear | Kopfbedeckung |
| `GRAD_ARSENAL_CAT_BACKPACK` | Backpack | Rucksack |
| `GRAD_ARSENAL_CAT_ATTACHMENTS` | Attachments | Anbauteile |
| `GRAD_ARSENAL_CAT_LOADOUTS` | Loadouts | Ausrüstungen |
| `GRAD_OPEN_ARSENAL` | Open Arsenal | Arsenal öffnen |
| `GRAD_LOAD_PREVIOUS_LOADOUT` | Load Previous Loadout | Vorherige Ausrüstung laden |

(The layout currently references `#GRAD_ARSENAL_TITLE/_OK/_CANCEL`; the rest are used by P6/P7.)

## 3. Service entity placement

Place one `GRAD_ArsenalService` entity in each world that should host the arsenal (or add it
to a system/gamemode prefab). It self-registers as a singleton and builds the catalog index.

## 4. Arsenal box prefab (P7)

A crate prefab inheriting a vanilla supply box, with `GRAD_ArsenalBoxComponent` (script, P7)
exposing the "Open Arsenal" + "Load previous loadout" user actions.

## 5. Before publish

Run `wb_cleanup` to remove `Scripts/WorkbenchGame/EnfusionMCP/`, and exclude `inspiration/`.
