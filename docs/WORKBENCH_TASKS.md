# Workbench-bound tasks (GUI artifacts for Nomi)

These few artifacts must be authored in the Workbench editor (the MCP write-tools no-op on
resource registration / stringtable / menu-config). Everything else is script and is done on
disk. Each task lists exactly what the script side expects.

## 1. Menu-preset config — makes the arsenal menu openable  ✅ ENTRY WRITTEN, NEEDS REAL OVERRIDE

**Goal:** bind `ChimeraMenuPreset.GRAD_ArsenalMenu` → the layout + the `GRAD_ArsenalMenu` class.

- Vanilla config: `{C747AFB6B750CE9A}Configs/System/chimeraMenus.conf` (found via
  `ArmaReforger.gproj` → `MenuManagerSettings.MenuConfigs`).
- Layout resource: `{88FFBAB9523831E0}UI/layouts/GRAD_ArsenalMenu.layout`.
- Menu class: `GRAD_ArsenalMenu`. Preset enum: `ChimeraMenuPreset.GRAD_ArsenalMenu` (modded enum).

**Status:** A reference DUPLICATE at `Configs/System/chimeraMenus.conf` already contains the full
vanilla list **plus** the appended entry:

```
MenuPreset GRAD_ArsenalMenu {
 Layout "{88FFBAB9523831E0}UI/layouts/GRAD_ArsenalMenu.layout"
 Class "GRAD_ArsenalMenu"
}
```

**Remaining step (Nomi):** do the REAL override of `chimeraMenus.conf` into the project (Resource
Browser → right-click the vanilla config → Override). Then ensure the overridden file's content
matches the reference duplicate (i.e. full vanilla list + the GRAD_ArsenalMenu entry). The
override REPLACES the vanilla config wholesale, so it must keep every vanilla entry — which the
reference copy already does.

NOTE: overriding shadows any menus BI adds in future game updates; re-merge on engine upgrades.

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

**Category labels** — `SCR_EArsenalItemType` is a BITFLAG enum; the rail shows keys
`GRAD_ARSENAL_CAT_<value>`. Confirmed values seen in the live catalog (22 categories):
2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072,
262144, 524288, 1048576, 2097152, 4194304. Add a stringtable key per value, e.g.
`GRAD_ARSENAL_CAT_2 = "Rifles"`, etc. (exact type→name mapping TBD by inspecting which prefabs
fall in each — or replace the rail labels with the vanilla `SCR_ArsenalItemTypeUIConfig` names
in script later). For now any friendly text beats the raw numbers.

**Stringtable setup:** the project needs its own stringtable registered. The base game uses
`WidgetManagerSettings.StringTables` in `ArmaReforger.gproj` →
`{518AE2E5A9361F76} Language/localization.st` (+ per-language runtime .conf). A mod adds its
own `.st` (e.g. `Language/GRAD_localization.st`) and registers it via the project's
`WidgetManagerSettings.StringTables`. Easiest in Workbench: create a String Table resource,
add the keys above, and reference it. Until then, `#GRAD_*` keys render as raw text (harmless).

## 3. Service entity placement

Place one `GRAD_ArsenalService` entity in each world that should host the arsenal (or add it
to a system/gamemode prefab). It self-registers as a singleton and builds the catalog index.

## 3b. GM right-click "Open Arsenal" context action (P7)

The scripted `GRAD_GMOpenArsenalAction` / `GRAD_GMCopyLoadoutAction` / `GRAD_GMPasteLoadoutAction`
(SCR_BaseContextAction subclasses) need registering into the GM editor's **context-action config**
so they appear in the unit right-click radial menu. Target = the right-clicked/selected unit
(player or AI), passed as the menu context's target. This is the intended primary GM entry point
("right-click unit → Open Arsenal"). Find the editor context-actions config the GM mode loads and
add our actions (override/extend it).

## 4. Arsenal box prefab (P7)

A crate prefab inheriting a vanilla supply box, with `GRAD_ArsenalBoxComponent` (script, P7)
exposing the "Open Arsenal" + "Load previous loadout" user actions.

## 5. Before publish

Run `wb_cleanup` to remove `Scripts/WorkbenchGame/EnfusionMCP/`, and exclude `inspiration/`.
