# Enfusion Layout Study — Reforger Shop System (ekudmada)

Study of a proven, working open-source shop UI to unblock our arsenal grid + tab bar.
Repo: https://github.com/ekudmada/Reforger-Shop-System (branch `main`).

Raw files fetched:
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/UI/Layouts/Menus/ShopMenu/ShopMain.layout
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/UI/Layouts/Menus/ShopMenu/ShopList.layout
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/UI/Layouts/Menus/ShopMenu/ListBoxElement_ShopItem.layout
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/UI/Layouts/Menus/ShopMenu/CategorySpinBox.layout
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/Scripts/Game/UI/Menu/ShopSystem/ADM_ShopUI.c
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/Scripts/Game/UI/Menu/ShopSystem/SCR_SpinBoxComponent.c
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/Scripts/Game/UI/Menu/ShopSystem/ADM_ModdedChimeraMenu.c
- https://raw.githubusercontent.com/ekudmada/Reforger-Shop-System/main/Scripts/Game/UI/Menu/ShopSystem/ADM_CustomPagingButton.c

## HEADLINE FINDING (read this first)

**This mod does NOT build its item grid with `GridLayoutWidget` + `GridSlot.SetColumn/SetRow` in script. It does NOT `CreateWidget` the item cards manually either.** It uses the vanilla widget-library **ListBox** (`SCR_ListBoxComponent`) and just calls `listbox.AddItem(name)` in a loop. The ListBox owns the scroll container, the vertical stacking, the row layout instancing, and the sizing. The script never touches slots, rows, columns, or `.Update()` on the container.

Likewise the "tab bar" (Buy / Sell / Cart) is not hand-built. It's the vanilla **TabView** (`SCR_TabViewComponent`) instanced from `WLib_TabViewCoreMenus.layout`, configured entirely by data in the `.layout` file.

The single biggest lesson for us: **prefer the SCR_ vanilla components (ListBox, TabView, SpinBox) over hand-rolling GridLayoutWidget + GridSlot.** They are proven, handle sizing/scrolling/focus, and cost almost no script.

---

## Q1. FrameWidgetSlot anchoring (top-level layouts)

Both `ShopMain.layout` and `ShopList.layout` have a `FrameWidgetClass` root. Two distinct patterns appear.

### Pattern A — stretched anchor (fill the parent), used in `ShopList.layout`
`ShopList.layout` (root `FrameWidgetClass "rootFrame"`), the sole `OverlayWidgetClass` child:
```
Slot FrameWidgetSlot "{605028C4E12773E2}" {
 Anchor 0 0 1 1        // min corner (0,0) top-left, max corner (1,1) bottom-right => STRETCHED
 PositionX 0
 OffsetLeft 0
 PositionY 0
 OffsetTop 0
 SizeX -1820           // with a stretched anchor, SizeX/SizeY act as inset deltas, not absolute size
 OffsetRight 0
 SizeY -1050
 OffsetBottom 0
}
```
Interpretation: `Anchor 0 0 1 1` means "pin all four edges to the parent's four edges" (stretch). When min and max anchors differ, the widget is sized by the anchor rectangle; `OffsetLeft/Top/Right/Bottom` push each edge inward/outward. The odd `SizeX -1820 / SizeY -1050` values here are Workbench editor bookkeeping (the design canvas was 1920x1080-ish); at runtime the stretch anchor dominates. This is the "fill my parent" case.

### Pattern B — point/edge anchor + explicit SizeX/SizeY (fixed strip), used in `ShopMain.layout`
`ShopMain.layout`, child `ButtonWidgetClass "FakeFocusDontRemove"`:
```
Slot FrameWidgetSlot "{5E5662CF4CB310ED}" {
 Anchor 0 0 0 1        // min (0,0), max (0,1) => x-min == x-max => NOT stretched horizontally, IS stretched vertically
 PositionX 0
 OffsetLeft 0
 PositionY 0
 OffsetTop 0
 SizeX 10              // because horizontal anchors are equal, SizeX is an ABSOLUTE width in px
 OffsetRight -10
 SizeY 0
 OffsetBottom 0
}
```
Interpretation: when the two horizontal anchors are equal (`0 ... 0`), the widget is **not stretched on that axis**, so `SizeX` is an absolute pixel width (10px here). The vertical anchors differ (`0 ... 1`) so it stretches full-height. This is the general rule that also drives the infamous error:

> `TextWidget: Position/Size works only when min and max anchor is the same`

i.e. a fixed `SizeX/SizeY`/`PositionX/Y` is only honored on an axis whose min anchor == max anchor. On a stretched axis you must size via `Offset*` instead.

### Pattern C — empty FrameWidgetSlot (inherit full frame)
`ShopMain.layout`, the `MenuBackground0` overlay and `MenuBase1` overlay:
```
Slot FrameWidgetSlot "{552C9A0A8526D2AD}" {
}
```
An empty `FrameWidgetSlot {}` = default stretched-to-frame. This is how full-screen background + full-screen menu content layers are stacked in the Frame root.

**The rule to remember:**
- Axis where min anchor == max anchor -> use `SizeX/SizeY` + `PositionX/Y` (absolute px).
- Axis where min anchor != max anchor (stretched) -> use `OffsetLeft/Right` / `OffsetTop/Bottom` (edge insets); do NOT set an absolute Size on that axis.

**Apply to our arsenal:** Our root Frame's full-screen panels should use an empty `FrameWidgetSlot {}` or `Anchor 0 0 1 1` with zero offsets. Any fixed-width side strip should use a point anchor on the horizontal axis (`Anchor 0 0 0 1`) + absolute `SizeX`, NOT a stretched anchor with a SizeX.

---

## Q2. The item "grid"/list of dynamically-created cards (our core use case)

**They use a vanilla ListBox, not a GridLayoutWidget.** Three layers:

### (a) Container widget class
`ShopList.layout` — the container is an `OverlayWidgetClass` that INSTANCES the vanilla `ListBox.layout` and attaches `SCR_ListBoxComponent`, whose only config is *which layout to use for each row*:
```
OverlayWidgetClass "{605049D7E7013B81}" : "{94CAAE4A601195D3}UI/layouts/ListBox/ListBox.layout" {
 Name "ListBox0"
 Slot LayoutSlot "{605049D7E7013BAC}" {
  SizeMode Fill
 }
 components {
  SCR_ListBoxComponent "{566C9415BAA54E1B}" {
   m_sElementLayout "{906F7BCCC662C19D}UI/Layouts/Menus/ShopMenu/ListBoxElement_ShopItem.layout"
  }
 }
 {
  TextWidgetClass ... Name "Empty Text" ...        // shown when list is empty
  ScrollLayoutWidgetClass "{56691148E692435A}" {   // ListBox.layout supplies the scroll+vertical stack
   Prefab "{56691148E692435A}"
   "Scrollbar Always Visible" 1
   {
    VerticalLayoutWidgetClass "{56691148E013D981}" { Prefab "{56691148E013D981}" ... }
   }
  }
  ImageWidgetClass ... "Is Visible" 0
 }
}
```
So the scroll container + the vertical stacking layout come from the referenced `UI/layouts/ListBox/ListBox.layout` (a stock game asset). The row template is `m_sElementLayout` = our `ListBoxElement_ShopItem.layout`. The mod author did not build the scroll/stack by hand.

### (b) The row/card layout — `ListBoxElement_ShopItem.layout`
Root is a `ButtonWidgetClass ... style blank` (the whole row is a button). It attaches the click component and a fixed height:
```
ButtonWidgetClass {
 Name "ListBoxElement"
 Slot AlignableSlot "{554ADB1D2D5974C0}" { HorizontalAlign 3 }
 components {
  ADM_ShopUI_Item "{60515D7620599A39}" { ... }                    // custom click handler (see Q4)
  SCR_ListBoxElementComponent "{56CB58E7628E6533}" : "{...}Configs/UI/ListBoxElement_ModularButtonComponent.conf" {
   m_bCanBeToggled 0
   m_bFocusOnMouseEnter 1
   m_sWidgetTextName "Name"        // tells ListBox which child TextWidget to write AddItem() text into
  }
 }
 style blank
 {
  SizeLayoutWidgetClass {
   Name "SizeLayout"
   Slot ButtonWidgetSlot "{5548E6E7E6AEDDC1}" { HorizontalAlign 3 Padding 0 5 0 5 }
   AllowHeightOverride 1
   HeightOverride 96                // <-- fixed row height, see Q3
   {
    OverlayWidgetClass "Overlay" {   // bg image + horizontal content + 2px border
     ImageWidgetClass "Background" { Color 0 0 0 0.7 }             // semi-transparent black bg
     HorizontalLayoutWidgetClass { ... preview / name / price / qty ... }
     PanelWidgetClass "Border" { Color 0.761 0.386 0.08 1  style outline_2px }
    }
   }
  }
 }
}
```
Key structural notes:
- The whole card is a `ButtonWidget style blank` (blank = no default chrome; you paint your own bg/border/text).
- Fixed height via a `SizeLayoutWidget` with `AllowHeightOverride 1 / HeightOverride 96` wrapping the visible overlay. Width is left to `HorizontalAlign 3` (stretch to fill the list width).
- Named children (`ItemPreview0`, `Name`, `Cost`, `CostOverlay`, `Quantity`) are found from script by `FindAnyWidget`.
- `SCR_ListBoxElementComponent.m_sWidgetTextName "Name"` is what makes `listbox.AddItem("some name")` automatically populate the row's `Name` TextWidget.

### (c) Script that instantiates & places each card
`ADM_ShopUI.c`, `PopulateTab()` (lines ~1040-1075) and `CreateListboxItem()` (lines ~953-1039). The entire "placement" is `listbox.AddItem(...)` in a loop — no slots, no rows, no columns, no container `.Update()`:
```cpp
protected void PopulateTab(SCR_TabViewContent wTabView, array<ref ADM_ShopMerchandise> merchandise, bool buyOrSell = true)
{
    Widget wListbox = wTabView.m_wTab.FindAnyWidget("ListBox0");
    if (!wListbox) return;
    SCR_ListBoxComponent listbox = SCR_ListBoxComponent.Cast(wListbox.FindHandler(SCR_ListBoxComponent));
    if (!listbox) return;

    ClearTab(wTabView);                       // listbox.Clear() under the hood
    foreach (ADM_ShopMerchandise merch : merchandise)
    {
        ...
        CreateListboxItem(listbox, merch, buyOrSell);
    }

    TextWidget emptyText = TextWidget.Cast(wTabView.m_wTab.FindAnyWidget("Empty Text"));
    if (merchandise.Count() == 0) { emptyText.SetVisible(true); emptyText.SetText("No items available."); emptyText.Update(); }
    else { emptyText.SetVisible(false); emptyText.Update(); }
}

protected void CreateListboxItem(SCR_ListBoxComponent listbox, ADM_ShopMerchandise merch, bool buyOrSell = true)
{
    string itemName = merch.GetType().GetDisplayName();
    if (!itemName) itemName = "Item";

    int itemIdx = listbox.AddItem(itemName);                       // instances m_sElementLayout, appends row, writes Name text
    SCR_ListBoxElementComponent lbItem = listbox.GetElementComponent(itemIdx);   // handle to that row
    if (!lbItem) return;

    // grab named children of THIS row and fill them:
    ItemPreviewWidget wItemPreview = ItemPreviewWidget.Cast(lbItem.GetRootWidget().FindAnyWidget("ItemPreview0"));
    if (wItemPreview)
        m_ItemPreviewManager.SetPreviewItemFromPrefab(wItemPreview, merch.GetType().GetDisplayEntity(), null, false);

    ADM_ShopUI_Item mainBtn = ADM_ShopUI_Item.Cast(lbItem.GetRootWidget().FindHandler(ADM_ShopUI_Item));
    if (mainBtn)
        mainBtn.m_OnClicked.Insert(AddToCart);                     // wire click (see Q4)

    TextWidget wItemPrice = TextWidget.Cast(lbItem.GetRootWidget().FindAnyWidget("Cost"));
    ... set price text / barter icons ...
}
```
Note: `listbox.AddItem()` returns an int index; `listbox.GetElementComponent(index)` returns the row component; `row.GetRootWidget().FindAnyWidget("...")` reaches its named children. **No `GridSlot`, no `SetRow/SetColumn`, no `container.Update()` call anywhere.** The ListBox handles it.

**Apply to our arsenal:** Replace our manual `GridLayoutWidget` + `GridSlot.SetColumn/SetRow` construction with a `SCR_ListBoxComponent` container referencing our tile layout via `m_sElementLayout`, and populate with `AddItem()` in a loop. This alone should eliminate the overlapping-list bug, because we stop fighting slot math the container should own.

> Caveat for a true multi-column GRID: the vanilla ListBox stacks rows vertically (1 column of full-width rows). This mod's "grid" is really a vertical list of wide rows. If we want an actual N-column icon grid, see the "Recommended changes" section — the honest options are (i) accept a vertical list of rows like this mod, or (ii) use `WrapLayoutWidget`/`GridLayoutWidget` but let the CONTAINER auto-place children (AddChild) rather than us computing row/col, or (iii) keep GridLayoutWidget but verify each tile has a real fixed size so the grid can flow them. NOT FOUND in this repo: a script-built multi-column GridLayoutWidget example — this mod simply doesn't use one.

---

## Q3. Fixed-size tiles

Yes — they pin fixed pixel size with **`SizeLayoutWidgetClass` + `AllowWidthOverride/AllowHeightOverride` + `WidthOverride/HeightOverride`**. Real examples:

Row height, `ListBoxElement_ShopItem.layout`:
```
SizeLayoutWidgetClass {
 Name "SizeLayout"
 AllowHeightOverride 1
 HeightOverride 96
 { OverlayWidgetClass "Overlay" { ... } }
}
```
96x96 item preview cell, same file:
```
SizeLayoutWidgetClass {
 Name "Item Preview Overlay"
 AllowWidthOverride 1
 WidthOverride 96
 AllowHeightOverride 1
 HeightOverride 96
 { ItemPreviewWidgetClass "ItemPreview0" { ... } }
}
```
Fixed width in `CategorySpinBox.layout` (the selection hint bar):
```
SizeLayoutWidgetClass {
 Name "SizeLayout0"
 AllowWidthOverride 1
 WidthOverride 300
 HeightOverride 4
 { ... }
}
```
And a fixed 600px spacer in `ShopMain.layout`: `SizeLayoutWidgetClass { ... WidthOverride 600 }`.

**Gotcha confirmed:** you must set `AllowWidthOverride 1` / `AllowHeightOverride 1` for the corresponding `WidthOverride`/`HeightOverride` to take effect. (`CategorySpinBox`'s outer SizeLayout sets `HeightOverride 38` without an explicit `AllowHeightOverride` because it inherits from `WLib_Base.layout` which already enables it — but for your own SizeLayouts, set the Allow flags.)

**Apply to our arsenal:** Our tile chain `ButtonWidget(blank) -> SizeLayoutWidget(Min/Max overrides) -> OverlayWidget` is basically the same shape they use, EXCEPT: confirm we're using `AllowWidthOverride/AllowHeightOverride 1` + `WidthOverride/HeightOverride` (single override values). If our layout uses "Min/Max" width fields, verify those are the real Enfusion fields — this mod uses `WidthOverride/HeightOverride`, which is the proven property name. Keep the SizeLayout as the DIRECT parent of the visible content, and let the ListBox be the outer container.

---

## Q4. Clickable card built/wired in script

Two-part pattern: a `ButtonWidget style blank` in the layout, plus a `SCR_ModularButtonComponent` subclass that exposes an `m_OnClicked` invoker.

**Layout side** (`ListBoxElement_ShopItem.layout` root):
```
ButtonWidgetClass {
 Name "ListBoxElement"
 components {
  ADM_ShopUI_Item "{60515D7620599A39}" { ... }   // subclass of SCR_ModularButtonComponent
  SCR_ListBoxElementComponent ... { m_bFocusOnMouseEnter 1 m_sWidgetTextName "Name" }
 }
 style blank
 { ... }
}
```
**Script side** — the component class (`ADM_ShopUI.c` line 1) inherits the invoker from the SCR base:
```cpp
class ADM_ShopUI_Item : SCR_ModularButtonComponent
{
   ...
}
```
`SCR_ModularButtonComponent` (vanilla) provides `ScriptInvoker m_OnClicked`. Wiring the click is one line, done per-row right after `AddItem` (`CreateListboxItem`, line ~977):
```cpp
ADM_ShopUI_Item mainBtn = ADM_ShopUI_Item.Cast(lbItem.GetRootWidget().FindHandler(ADM_ShopUI_Item));
if (mainBtn)
    mainBtn.m_OnClicked.Insert(AddToCart);      // AddToCart(component) is called on click
```
The secondary +/- paging buttons use the same idea via `SCR_PagingButtonComponent.m_OnActivated` (item `HandlerAttached`, lines 119-143):
```cpp
m_ButtonLeft = SCR_PagingButtonComponent.Cast(left.FindHandler(SCR_PagingButtonComponent));
if (m_ButtonLeft) m_ButtonLeft.m_OnActivated.Insert(OnQuantityLess);
```
And nav-bar / checkout buttons use `SCR_InputButtonComponent.m_OnActivated` (`ADM_ShopUI.c` ~1245-1268):
```cpp
SCR_InputButtonComponent checkoutButton = SCR_InputButtonComponent.Cast(checkoutWidget.FindHandler(SCR_InputButtonComponent));
if (checkoutButton) checkoutButton.m_OnActivated.Insert(Checkout);
```

**Apply to our arsenal:** Make each tile a `ButtonWidget style blank` with a `SCR_ModularButtonComponent` (or a subclass) in its `components {}`. After creating/adding each tile, `FindHandler(SCR_ModularButtonComponent)` and `m_OnClicked.Insert(OurClickCallback)`. This is far cleaner than a hand-written `ScriptedWidgetEventHandler.OnClick`. (NOT FOUND / not needed here: a raw `ScriptedWidgetEventHandler` OnClick override — the mod relies entirely on the SCR button components' invokers.)

---

## Q5. Tab / category bar

There are TWO distinct mechanisms; ours maps to the first.

### (a) Real tab bar = vanilla TabView (`SCR_TabViewComponent`), configured in layout only
`ShopMain.layout`, `BuySellTabView` (and `CartTabView`) instance `WLib_TabViewCoreMenus.layout` and declare tabs as data:
```
VerticalLayoutWidgetClass "{6050050FFEE19495}" : "{D1CAF877446C66DE}UI/layouts/WidgetLibrary/TabView/WLib_TabViewCoreMenus.layout" {
 Name "BuySellTabView"
 components {
  SCR_TabViewComponent "{546B27D01CA8A38D}" {
   m_aElements {
    SCR_TabViewContent { m_ElementLayout "{...}UI/Layouts/Menus/ShopMenu/ShopList.layout"  m_sTabButtonContent "Buy" }
    SCR_TabViewContent { m_ElementLayout "{...}UI/Layouts/Menus/ShopMenu/ShopList.layout"  m_sTabButtonContent "Sell" }
   }
   m_bKeepHiddenTabs 1
   m_bCreateAllTabsAtStart 1
  }
 }
}
```
Each tab = a `SCR_TabViewContent` with a content layout (`m_ElementLayout`) and a button label (`m_sTabButtonContent`). The horizontal tab-button strip, the active-tab highlight, keyboard/gamepad nav, and show/hide are ALL handled by `SCR_TabViewComponent` + `WLib_TabViewCoreMenus.layout`. The mod writes zero code to draw or highlight tabs.

Script access (`ADM_ShopUI.c` HandlerAttached ~1218-1244):
```cpp
m_BuySellTabComponent = SCR_TabViewComponent.Cast(m_wBuySellTabWidget.FindHandler(SCR_TabViewComponent));
if (m_BuySellTabComponent.GetTabCount() == 2) {
    m_wBuyTabView  = m_BuySellTabComponent.GetEntryContent(0);
    m_wSellTabView = m_BuySellTabComponent.GetEntryContent(1);
}
```
`m_bCreateAllTabsAtStart 1` + `m_bKeepHiddenTabs 1` mean both tab bodies exist immediately, so `wTabView.m_wTab.FindAnyWidget("ListBox0")` works right away. **This is the important reliability trick** — with those flags off, a tab's widgets don't exist until first shown, and `FindAnyWidget` returns null (a classic "my tab is invisible / null widget" bug).

### (b) Category selector = SpinBox (`SCR_SpinBoxComponent`), not a tab strip
`CategorySpinBox.layout` is a `ButtonWidget style blank` with `SCR_SpinBoxComponent` + left/right paging buttons + a center `RichTextWidget "SelectionText"`. Categories are added in script (`ADM_ShopUI.c` ~809-820):
```cpp
protected void ConfigureCategory(SCR_SpinBoxComponent component, array<ADM_ShopCategory> categories)
{
    component.AddItem("All");
    foreach (ADM_ShopCategory category : categories)
        component.AddItem(category.m_DisplayName);
}
```
Active-state coloring for the spinbox is data-driven via `SCR_ButtonEffectColor` entries in the layout (e.g. `m_cFocusGained 1 0.597 0.198 0.3` on the `LineHighlight`/`Background` widgets) — no script sets colors on focus.

**Apply to our arsenal:** Our horizontal category TAB bar should almost certainly be a `SCR_TabViewComponent` (if categories switch whole content panes) or a `SCR_SpinBoxComponent` (if it's a compact "< Category >" selector). Either way, DON'T hand-build the bar and DON'T set the highlight color in script — use the component + `SCR_ButtonEffectColor`. Our "tabs report a size but render invisible" symptom is most likely (1) our tab color equals the background because we're painting it ourselves instead of letting `SCR_ButtonEffectColor` do it, and/or (2) missing `m_bCreateAllTabsAtStart`/`m_bKeepHiddenTabs` so the tab bodies aren't instanced.

---

## Q6. Gotchas surfaced by their code/structure

1. **min-anchor == max-anchor rule (the exact error you hit).** `SizeX/SizeY`/`PositionX/Y` only take effect on an axis whose min and max anchor are equal. On a stretched axis, size via `Offset*`. See Q1 Pattern B. This is precisely the `Position/Size works only when min and max anchor is the same` warning.

2. **`AllowWidthOverride`/`AllowHeightOverride` must be 1** for `WidthOverride`/`HeightOverride` to apply (Q3). Easy to forget; silently ignored otherwise.

3. **Let vanilla components own scroll + stacking.** The scroll container and vertical stack come from stock `ListBox.layout`/`WLib_TabViewCoreMenus.layout`; the mod never manually parents children into a ScrollLayout. ScrollLayout expects exactly one content child (here a single `VerticalLayoutWidget`), and rows go into THAT, not directly into the ScrollLayout.

4. **`m_bCreateAllTabsAtStart 1` + `m_bKeepHiddenTabs 1`** so hidden tab bodies exist and `FindAnyWidget` on them returns real widgets. Without this, off-screen tab widgets are null -> "invisible tab" bugs.

5. **`style blank` on interactive containers.** Cards, spinbox, and search are `ButtonWidget style blank` so no default button chrome interferes; visuals are explicit child Image/Panel/Text widgets. Border via `PanelWidgetClass ... style outline_2px`, background via `ImageWidgetClass Color 0 0 0 0.7`.

6. **Invisible-widget causes seen here:** widgets explicitly disabled with `"Is Visible" 0` / `Opacity 0` (e.g. `FakeFocusDontRemove` is `Opacity 0` but kept for focus). If your widget "has a size but shows nothing," check: Opacity, `Is Visible`, Color alpha (an Image/Panel with alpha 0), Z Order (they set `"Z Order" 1` on `InventoryContent` and `"Z Order" 8` on text to lift it above the background), and Clipping (`Clipping True/False/Inherit` appears throughout; a clipped child that overflows its parent's rect vanishes).

7. **Color/opacity defaults.** New Image/Panel widgets often default to a color/alpha that can render invisible or wrong; the mod sets `Color` explicitly on every visible Image/Panel (e.g. separators `Color 0.133 0.133 0.133 1`, border `Color 0.761 0.386 0.08 1`). Don't rely on defaults for anything meant to be seen.

8. **Text-population coupling:** `SCR_ListBoxElementComponent.m_sWidgetTextName "Name"` ties `listbox.AddItem(text)` to a specific named TextWidget in the row. If that name doesn't match a child TextWidget, `AddItem`'s text goes nowhere.

9. **`.Update()` is called on individual TextWidgets after changing text/visibility** (e.g. `emptyText.Update()`, `quantity.Update()`), NOT on the list container. The container refreshes itself as items are added/removed.

10. **Menu registration:** menus are registered by extending the enum — `modded enum ChimeraMenuPreset { ADM_ViewPaymentMenu, ADM_ShopMenu }` (`ADM_ModdedChimeraMenu.c`) — and the UI class is `ADM_ShopUI : ChimeraMenuBase` with logic in `HandlerAttached(Widget w)` (grab root, find components, wire invokers, populate). Note: init work is in `HandlerAttached`, not a custom `OnMenuOpen` here.

---

## RECOMMENDED CONCRETE CHANGES TO OUR GRID + TAB BAR

Minimal structural approach this mod proves works. Adopt in this order.

### Item pane (replace GridLayoutWidget + GridSlot.SetColumn/SetRow)
1. **Stop hand-placing tiles.** Delete the `GridSlot.SetColumn/SetRow` logic. It's the source of the overlapping list.
2. **Container:** In the arsenal layout, make the item pane an `OverlayWidgetClass : {GUID}UI/layouts/ListBox/ListBox.layout` with a `SCR_ListBoxComponent` whose `m_sElementLayout` points at our tile layout. Give it `Slot ... { SizeMode Fill }`. Let ListBox own scroll + stacking.
3. **Tile layout:** root `ButtonWidgetClass style blank` with components `SCR_ModularButtonComponent` (or subclass) + `SCR_ListBoxElementComponent { m_sWidgetTextName "Name" m_bFocusOnMouseEnter 1 }`. Inside: `SizeLayoutWidget { AllowHeightOverride 1 HeightOverride <px>  (and AllowWidthOverride 1 WidthOverride <px> if fixed width) } -> OverlayWidget { Background Image (Color set), content, optional Panel style outline_2px border }`. Name the icon child (`ItemPreview0` / your image name) and the `Name` text child.
4. **Populate in script:** loop `int idx = listbox.AddItem(displayName); SCR_ListBoxElementComponent row = listbox.GetElementComponent(idx);` then `row.GetRootWidget().FindAnyWidget("<icon>")` to set the image, and `FindHandler(SCR_ModularButtonComponent).m_OnClicked.Insert(OnTileClicked)`. Call `listbox.Clear()` before repopulating. No `.Update()` on the container.

This yields a scrolling **vertical list of full-width tiles** (exactly what this mod ships).

If we specifically need a **multi-column icon grid** (ListBox is single-column):
- Keep the tile layout + fixed `WidthOverride`/`HeightOverride` from step 3, but host tiles in a `WrapLayoutWidget` (or `GridLayoutWidget`) inside a `ScrollLayoutWidget`, and add each tile with `container.AddChild(tileWidget)` — let the wrap/grid FLOW them. Do NOT compute row/column. (This exact grid isn't in the repo — flagged NOT FOUND — but it follows directly from their fixed-size-tile + let-the-container-place-it philosophy, and avoids the SetColumn/SetRow math that's breaking us.)

### Category tab bar (fix the invisible tabs)
1. **Use `SCR_TabViewComponent`** instanced from `WLib_TabViewCoreMenus.layout`, with one `SCR_TabViewContent { m_ElementLayout ... m_sTabButtonContent "<Category>" }` per category. Remove our hand-built bar and our ScrollLayout wrapper around it.
2. **Set `m_bCreateAllTabsAtStart 1` and `m_bKeepHiddenTabs 1`** so tab bodies exist and `FindAnyWidget` works immediately — likely the direct cause of "tabs report a size but nothing shows."
3. **Do not set the active-tab highlight color in script.** Let the TabView's own tab buttons handle highlight (or, for a spinbox-style selector, use `SCR_ButtonEffectColor` entries in the layout like `CategorySpinBox.layout` does: `m_cFocusGained 1 0.597 0.198 0.3`). Our current invisibility is consistent with tab color == background because we're painting it ourselves.
4. If categories are a compact selector rather than full panes, use `SCR_SpinBoxComponent` (`CategorySpinBox.layout` pattern) + `component.AddItem(name)` per category instead of a TabView.

### Net effect
- Grid overlap bug: fixed by handing stacking/scroll to `SCR_ListBoxComponent` (or a wrap/grid container via AddChild) instead of manual `GridSlot` coordinates.
- Invisible tab bar: fixed by using `SCR_TabViewComponent` with `m_bCreateAllTabsAtStart`/`m_bKeepHiddenTabs` and letting the component own highlight color.

### NOT FOUND (not invented)
- A script-built multi-column `GridLayoutWidget` with `GridSlot.SetRow/SetColumn`. This mod does not use one; its "grid" is a single-column ListBox of wide rows.
- A raw `ScriptedWidgetEventHandler.OnClick` for cards. Clicks go through `SCR_ModularButtonComponent.m_OnClicked` / `SCR_*Component.m_OnActivated` invokers.
