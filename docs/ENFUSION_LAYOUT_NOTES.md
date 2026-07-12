# Enfusion / Arma Reforger UI Layout Notes

Authoritative reference for positioning widgets in `.layout` files without trial-and-error.
Derived from the Enfusion script API (via `api_search`), the BI wiki (via `wiki_search`), and
real vanilla / GRAD loose `.layout` files on disk. Every non-obvious claim is tagged with a
source. Anything not directly confirmable is flagged **UNVERIFIED**.

Sources referenced repeatedly:
- **[API:FrameSlot]** `api_search FrameSlot` (Enfusion Engine API, class `FrameSlot`)
- **[API:LayoutSlot]** / **[API:AlignableSlot]** / **[API:GridSlot]** etc. (`api_search`)
- **[VL:GLRR]** vanilla-derived layout on disk: `UI/Layouts/Menus/Dialogs/GroupLeaderReplacementRequest.layout`
- **[VL:WLibBtn]** vanilla widget-library button: `UI/Layouts/WidgetLibrary/Buttons/WLib_ButtonText.layout`
- **[GRAD:Arsenal]** `UI/Layouts/GRAD_ArsenalMenu.layout` (the file being fixed; used for the two working point-anchored siblings)

---

## 1. FrameWidgetSlot: the anchor / offset / size model

A child of a `FrameWidgetClass` is positioned with a `Slot FrameWidgetSlot { ... }` block.
The relevant fields and their API backing (`FrameSlot` static accessors) **[API:FrameSlot]**:

| Layout field(s)                | API accessor                    | Meaning |
|--------------------------------|---------------------------------|---------|
| `Anchor  minX minY maxX maxY`  | `GetAnchorMin` / `GetAnchorMax` | Two anchor points as fractions 0..1 of the PARENT rect. min = top-left anchor, max = bottom-right anchor. |
| `OffsetLeft OffsetTop OffsetRight OffsetBottom` | `GetOffsets(l,t,r,b)` | Pixel offsets applied at each edge (see per-axis rules below). |
| `SizeX SizeY`                  | `GetSize` / `GetSizeX/Y`        | Fixed pixel size, used **only in the point-anchor case** (min==max on that axis). |
| (implicit)                     | `IsSizeToContent` / `SetSizeToContent` | Whether the widget sizes to its own content instead of to the slot. |
| `Alignment`                    | `GetAlignment` / `SetAlignment` | Pivot within the resulting rect (rarely set in text form). |

Each axis (X and Y) is resolved **independently**. There are two modes per axis:

### 1a. STRETCHED axis (minAnchor != maxAnchor)

Both edges are pinned to parent-relative anchor lines and inset by the offsets. `Size*` is
**ignored** on that axis.

```
parentLen      = parent rect length on this axis (px)
minLine        = minAnchor * parentLen
maxLine        = maxAnchor * parentLen
topEdge/left   = minLine + OffsetTop   (or + OffsetLeft)
botEdge/right  = maxLine + OffsetBottom (or + OffsetRight)   // note: offset is ADDED, so use NEGATIVE to inset
length         = botEdge - topEdge
```

Positive `OffsetTop`/`OffsetLeft` push the near edge inward; **negative** `OffsetBottom`/`OffsetRight`
pull the far edge inward. This is why the full-stretch siblings work:

- `Background`  `Anchor 0 0 1 1  Offsets 0 0 0 0` -> exactly fills parent. **[GRAD:Arsenal]**
- `ItemListScroll` `Anchor 0 0 0.34 1  OffsetLeft 40 OffsetTop 140 OffsetRight -8 OffsetBottom -220`
  -> X stretched from 0..0.34 of parent, inset 40px left / 8px right; Y stretched 0..1, inset 140 top / 220 bottom. **[GRAD:Arsenal]**

### 1b. POINT axis (minAnchor == maxAnchor) -- the one that bites you

When min==max on an axis there is only a single anchor line, so the engine needs an explicit
extent. On that axis the size comes from **`SizeX`/`SizeY`** and the near edge from the near
offset:

```
anchorLine     = anchor * parentLen         // single line, since min==max
nearEdge       = anchorLine + OffsetTop/OffsetLeft
length         = SizeY / SizeX               // OffsetBottom/OffsetRight are NOT the far edge here
farEdge        = nearEdge + length
```

The recurring engine error

> `TextWidget : Position/Size works only when min and max anchor is the same in given direction`

is emitted when a `Set*` script call (e.g. `FrameSlot.SetSize`, `SetPos`) assumes a point
anchor but the slot is actually stretched on that axis (min != max), or vice-versa. **The fix is
to make the anchor consistent with how you are sizing**: if you want to drive size with `SizeY`,
the Y anchors must be equal (a point); if you want to drive size with offsets, the Y anchors must
differ (stretched). **[API:FrameSlot]** (SetSize/SetPos are the point-anchor accessors)

### 1c. Worked example: the Title (why `SizeY 50` + `OffsetBottom -50` gives a 50px bar)

```
Slot FrameWidgetSlot { Anchor 0 0 1 0  OffsetLeft 40 OffsetTop 0  SizeX -80 OffsetRight -100  SizeY 50 OffsetBottom -50 }
```
- **X axis:** minX=0, maxX=1 -> STRETCHED. Left = 0*W + 40 = 40; Right = 1*W + (-100) = W-100.
  (`SizeX -80` is ignored on a stretched axis.) Width = W-140.
- **Y axis:** minY=0, maxY=0 -> POINT at the top. Top = 0 + OffsetTop(0) = 0; height = **SizeY = 50**.
  `OffsetBottom -50` is **stored but not used as the far edge** in point mode.

Cross-check with the other working point-anchored sibling, `ButtonBar`:
```
Slot FrameWidgetSlot { Anchor 1 1 1 1  OffsetLeft -360 OffsetTop -64 SizeX 320 OffsetRight -40  SizeY 48 OffsetBottom -16 }
```
Both axes are point anchors (at fraction 1). Top = H-64, height = SizeY = 48 -> bottom lands at
H-16, which equals `OffsetBottom`. So in these working files the author kept the invariant
`OffsetBottom == OffsetTop + SizeY` (and `OffsetRight == OffsetLeft + SizeX`). The Title breaks
that invariant (0 + 50 = 50, not -50) yet still renders correctly -- **confirming that in
point-anchor mode SizeY wins and OffsetBottom is inert.** **[GRAD:Arsenal]**

> Practical rule of thumb: the Layout Editor writes BOTH `SizeY` and `OffsetBottom` when you drag
> a point-anchored widget, and keeps them in sync (`OffsetBottom = OffsetTop + SizeY`). Hand-edited
> files only need to get `Anchor` + `OffsetTop` + `SizeY` right; keep `OffsetBottom` consistent to
> avoid confusing the editor, but the renderer uses `SizeY`.

### 1d. Why CategoryList computes a NEGATIVE height

```
CategoryList: Anchor 0 0 0.34 0   OffsetTop 72   SizeY 56   OffsetBottom -56
```
- Y axis: minY=0, maxY=0 -> POINT anchor at the top.
- The renderer should use SizeY=56 -> a 56px bar at y=72. So far so good in isolation.

The negative height `(781, -166)` comes from a **mismatch between mode and driver**, most likely
one of:
1. The container is a `HorizontalLayoutWidget`, whose height on a point-Y-anchor slot is being
   driven by its children's desired size (size-to-content), and the children currently contribute
   0 desired height (see section 2) -- so `SizeY 56` is being overridden by content measurement
   that resolves negative once `OffsetBottom -56` is subtracted. i.e. the engine is treating the
   slot as stretched (`top=72`, `bottom = 0*H + (-56) = -56`) giving height `-56 - 72 = -128`.
2. Some script path calls a stretched-style `SetOffsets` on it after load.

Either way the cure is to make the slot unambiguous. See the fix in section 6.

---

## 2. HorizontalLayoutWidget / VerticalLayoutWidget child sizing

**[API:HorizontalLayoutWidget]** (`Update`, `SetFillOrigin`, `GetFillOrigin`),
**[API:LayoutSlot]** (`SetFillWeight`, `SetSizeMode`, `SetHorizontalAlign`, `SetVerticalAlign`, `SetPadding`).

Behavior:
- A Horizontal/Vertical layout arranges children along its **main axis** (H = left-to-right,
  V = top-to-bottom) in order.
- **Main-axis size of each child** = the child's **desired size**, unless the child's `LayoutSlot`
  `SizeMode`/`FillWeight` says to stretch/fill. With a fill weight the child grows to share leftover
  main-axis space; with default (fit) size mode the child takes exactly its desired main-axis size.
  **[API:LayoutSlot]** `SetFillWeight` / `SetSizeMode`.
- **Cross-axis size of each child** = stretched to the container's cross-axis extent by default,
  OR the child's desired cross size when the slot align is not "stretch". `HorizontalAlign 3` /
  `VerticalAlign 3` (value 3 = stretch) is what vanilla buttons use. **[VL:WLibBtn]**
- **The container's own size:** on any axis where its OWN slot is a point/size-to-content, the
  container reports the **sum (main axis) or max (cross axis) of children's desired sizes**. So a
  Horizontal layout with a point-Y-anchor slot derives its height from the tallest child's desired
  height. If children have 0 desired height, the container measures 0 (or, combined with a negative
  offset, negative) -- exactly the CategoryList symptom.

**Children therefore need an intrinsic desired size to occupy space in a fit-mode layout.** A bare
`ButtonWidget` does not itself declare a desired size; it derives one from its child. That leads to
question 2 below.

### Does a bare ButtonWidget propagate its child's desired size?

**Answer: YES, a ButtonWidget forwards the desired size of its single child**, provided the child
actually declares one. A `ButtonWidget`'s child uses `Slot ButtonWidgetSlot` and the button
measures to that child. Vanilla proves this: `WLib_ButtonText.layout` is a bare `ButtonWidgetClass`
whose sole child is a `SizeLayoutWidgetClass`, and the button sizes to it. **[VL:WLibBtn]**

The trap is NOT that the button "collapses" -- it is that the child must supply a concrete desired
size. If the button's child is a `SizeLayoutWidget` with **only** `HeightOverride` set but
`AllowHeightOverride` left at 0, or without `MinDesired*`, the measured desired size can be 0 and
the button (and the parent Horizontal layout) collapses. The reliable pattern (section 3) sets the
Allow* flags AND the Min/Max desired values so the desired size is forced non-zero and propagates
button -> horizontal-layout -> point-anchored container.

So: **five `ButtonWidget` tiles, each wrapping a correctly-configured `SizeLayoutWidget`
(AllowWidthOverride + AllowHeightOverride + WidthOverride/HeightOverride + Min/MaxDesired), WILL
each get a 110x52 box in a HorizontalLayout.** If they collapse, the cause is the SizeLayout config,
not the button. The existing `GRAD_TabButton.layout` is configured correctly for this (see below).

---

## 3. SizeLayoutWidget semantics

**[API:SizeLayoutWidget]** exposes paired Enable/Set methods:
`EnableWidthOverride(bool)` + `SetWidthOverride(float)`, `EnableHeightOverride` + `SetHeightOverride`,
`EnableMinDesiredWidth/Height`, `EnableMaxDesiredWidth/Height`, `SetMinDesiredWidth/Height`,
`SetMaxDesiredWidth/Height`, plus aspect-ratio overrides.

In the text `.layout` format these map to:

| Layout field           | Meaning |
|------------------------|---------|
| `AllowWidthOverride 1` | Enables the width override (= `EnableWidthOverride(true)`). **Required** or `WidthOverride` is ignored. |
| `AllowHeightOverride 1`| Enables the height override. |
| `WidthOverride 110`    | Forces the widget's width to 110px when the Allow flag is on. |
| `HeightOverride 52`    | Forces the height to 52px when the Allow flag is on. |
| `MinDesiredWidth/Height` | Lower clamp on the desired size reported to the parent layout. |
| `MaxDesiredWidth/Height` | Upper clamp on the desired size reported to the parent layout. |

**Which actually force a fixed size that PROPAGATES to a parent H/V/Grid layout?**
- `WidthOverride`/`HeightOverride` (with their Allow flags) set the widget's own rect.
- **`MinDesiredWidth/Height` (and Max) are what the parent layout MEASURES.** To get a fixed tile
  that reserves space in a Horizontal/Vertical/Grid layout, set Min == Max == the target size so the
  reported desired size is pinned. Setting only `*Override` without `MinDesired*` can leave the
  reported desired size at 0 and the tile collapses in a fit-mode parent (section 2).

**Vanilla confirmation [VL:GLRR]** -- a fixed 56x56 icon box:
```
SizeLayoutWidgetClass {
  AllowWidthOverride 1
  WidthOverride 56
  HeightOverride 56
  MinDesiredHeight 56
  MaxDesiredWidth 56
  { ImageWidgetClass { ... Size 32 32 } }
}
```
Note vanilla sets both the override AND the Min/Max desired to make the box reserve 56px in its
parent layout.

`GRAD_TabButton.layout` already follows the stronger form (all four Allow/Override + all four
Min/Max Desired at 110x52), which is correct for a fixed tile. **[GRAD:Arsenal / GRAD_TabButton]**

---

## 4. Slot type must match the parent container

Confirmed by the `AlignableSlot` inheritance tree **[API:AlignableSlot]**: `AlignableSlot` is the
base of `ButtonSlot, GridSlot, LayoutSlot, OverlaySlot, ScrollLayoutSlot, SizeLayoutSlot`
(and `LayoutSlot`'s subclass `HorizontalLayoutSlot`/`VerticalLayoutSlot`). A child's `Slot <Type>`
must be the slot type the parent container hands out.

| Parent widget class         | Child's `Slot` type        | Source |
|-----------------------------|----------------------------|--------|
| `FrameWidgetClass`          | `FrameWidgetSlot`          | [API:FrameSlot], [GRAD:Arsenal] |
| `OverlayWidgetClass`        | `OverlayWidgetSlot`        | [API:OverlaySlot], [VL:WLibBtn] (`Slot OverlayWidgetSlot`) |
| `HorizontalLayoutWidgetClass` | `LayoutSlot`             | [API:LayoutSlot], [VL:GLRR] (`Slot LayoutSlot` under HorizontalLayout), [GRAD:Arsenal] |
| `VerticalLayoutWidgetClass` | `LayoutSlot`               | [API:LayoutSlot] (VerticalLayoutSlot inherits LayoutSlot), [GRAD:Arsenal] (`Slot LayoutSlot` under VerticalLayout) |
| `SizeLayoutWidgetClass`     | `SizeLayoutSlot`           | [API:SizeLayoutSlot]. In practice the SizeLayout has a single child and vanilla often omits the Slot line entirely (it defaults correctly) -- [VL:GLRR] SizeLayout children have no explicit Slot. |
| `ScrollLayoutWidgetClass`   | (OMIT the Slot line)       | **CONFIRMED: writing `Slot ScrollLayoutSlot { ... }` explicitly gives `GUI (E): Unknown class 'ScrollLayoutSlot'` and FAILS the whole layout load.** The child must use a BARE `{ }` with no Slot line (it defaults correctly). Verified against the working `ItemListScroll -> ItemList`. Do NOT write the slot type for a ScrollLayout child. |
| `GridLayoutWidgetClass`     | `GridSlot`                 | [API:GridSlot] (`SetRow`/`SetColumn` are `GridSlot` statics) |
| `ButtonWidgetClass`         | `ButtonWidgetSlot`         | [API:ButtonSlot], [VL:WLibBtn] & [GRAD:TabButton] (`Slot ButtonWidgetSlot`) |

Notes:
- The concrete text token is `LayoutSlot` for both Horizontal and Vertical layouts (they share the
  `LayoutSlot` accessor family). You may also see `HorizontalLayoutSlot`; `LayoutSlot` is what
  vanilla writes. **[VL:GLRR]**
- `ButtonWidgetSlot` vs `ButtonSlot`: the API accessor class is `ButtonSlot`, but the token in the
  `.layout` text is `ButtonWidgetSlot` (as written by the editor). **[VL:WLibBtn]**
- Using the WRONG slot type is silently tolerated by some containers but breaks alignment/sizing;
  match them.

---

## 5. GridLayoutWidget

**[API:GridLayoutWidget]** (`Update`, `SetRowFillWeight`, `SetColumnFillWeight`),
**[API:GridSlot]** (`SetRow`, `SetColumn`, `SetRowSpan`, `SetColumnSpan`, `SetPadding`, align).

- Children are placed by their **grid cell** via `GridSlot.SetColumn(widget, col)` and
  `GridSlot.SetRow(widget, row)` (or `Column`/`Row` fields in the `Slot GridSlot { }` block).
  Two children with the same row+column **overlap** -- this is the classic overlap bug: cells not
  assigned, so everything lands in (0,0). **[API:GridSlot]**
- Row/column extents auto-size to the largest desired size of any child in that row/column, unless
  a fill weight is set (`SetRowFillWeight`/`SetColumnFillWeight`), which distributes leftover space.
  So fixed-size, non-overlapping tiles require: (a) each child has a real desired size (SizeLayout
  as in section 3), and (b) each child has a distinct `Row`/`Column`. **[API:GridLayoutWidget]**
- **`Update()` must be called after adding/removing children or changing row/col at runtime** for
  the grid to re-measure and re-place. The `Update()` method exists on the grid for exactly this;
  the same applies to `HorizontalLayoutWidget.Update()`. **[API:GridLayoutWidget]/[API:HorizontalLayoutWidget]**
- For a fixed N-column grid of fixed tiles: give every tile the SizeLayout recipe from section 3,
  assign `Column = index % N`, `Row = index / N`, and call `grid.Update()` after populating.

**UNVERIFIED:** the exact tie-break when a cell's children have differing desired sizes vs. a set
fill weight (measurement precedence). Empirically, set weight OR rely on desired size, not both.

---

## 6. Recommended concrete fixes

### 6a. CategoryList slot (target: x=40, y=72, width to ~0.34 of parent minus 8px, height=56)

The bug is the ambiguous Y axis (point anchor + a stale `OffsetBottom -56`). Two valid, clean forms:

**Form (a) -- point Y anchor + SizeY (matches the Title/ButtonBar working pattern):**
```
Slot FrameWidgetSlot {
  Anchor 0 0 0.34 0
  OffsetLeft 40
  OffsetTop 72
  OffsetRight -8
  SizeY 56
  OffsetBottom 128     // = OffsetTop + SizeY, keeps the editor happy; renderer uses SizeY
}
```
(X is stretched 0..0.34 inset 40/8; Y is a 56px point-anchored bar at y=72. The critical change is
`OffsetBottom` must be `OffsetTop + SizeY = 128`, NOT `-56`, so it never resolves negative.)

**Form (b) -- stretched Y anchor + offsets (no SizeY):**
```
Slot FrameWidgetSlot {
  Anchor 0 0 0.34 0.0691   // 0.0691 * 1080 = ~74.6; pick maxY so (maxY*H) - 72 = 56
  OffsetLeft 40
  OffsetTop 72
  OffsetRight -8
  OffsetBottom 0
}
```
Form (b) is fragile because it depends on parent height (1080 base). **Prefer Form (a).**

The single recommended change: keep `Anchor 0 0 0.34 0`, `OffsetTop 72`, `SizeY 56`, and set
`OffsetBottom 128` (remove the `-56`). That removes the negative-height computation.

### 6b. Tab tiles in the HorizontalLayout

`GRAD_TabButton.layout` is already correct: a bare `ButtonWidgetClass` -> `SizeLayoutWidget` with
`AllowWidthOverride 1 / WidthOverride 110 / AllowHeightOverride 1 / HeightOverride 52` AND
`MinDesiredWidth/Height 110/52` + `MaxDesiredWidth/Height 110/52`. That forces a 110x52 desired size
which propagates through the button to the HorizontalLayout, so each tile reserves 110x52. **No
change needed on the tile itself.**

The reason tiles were invisible is upstream: the CategoryList container had negative height, so its
children had nothing to render into. Once 6a gives CategoryList a real 56px height, the 52px-tall
tabs fit. (Tab height 52 <= container 56, good.) Ensure each tab is added with `Slot LayoutSlot`
(child of a HorizontalLayout) and, if built at runtime, call `CategoryList.Update()` after adding
all tabs so the layout measures them.

---

## Quick-reference summary

- FrameWidgetSlot: per-axis. **min==max anchor -> size from `SizeX/SizeY`, near edge from
  `OffsetLeft/Top`; `OffsetRight/Bottom` inert.** min!=max anchor -> both edges from
  `anchor*parentLen + offset`, `Size*` ignored.
- The "min and max anchor is the same" engine error = a size/pos call assuming the wrong mode; make
  the anchor consistent with how you drive size.
- H/V layouts fit children to their **desired size** on the main axis; a fixed tile needs a
  `SizeLayoutWidget` with Allow*Override + *Override + Min/MaxDesired to pin that desired size.
- A bare `ButtonWidget` **does** propagate its child's desired size; it only "collapses" when the
  child's desired size is 0.
- Child `Slot` type must match the parent (table in section 4).
- Grid tiles need distinct `Row`/`Column` + a call to `grid.Update()`; overlap = unassigned cells.

---

## 9. RUNTIME FINDINGS that CONTRADICT the theory above (empirical, from live logs)

The theory in the sections above did NOT hold up in the actual engine for the CategoryList case.
Measured live via `Widget.GetScreenSize()` (fired ~200ms after menu open, i.e. after layout settled):

- **CategoryList = a `HorizontalLayoutWidget` in a `FrameWidgetSlot` reported `listSize=(781.913, -166.296)` — a NEGATIVE height.**
- The `-166.296` height was **IDENTICAL** across `OffsetBottom -56`, `OffsetBottom 128`, and other
  values, across full Workbench restarts + layout reimport. **CONCLUSION: the `HorizontalLayoutWidget`'s
  cross-axis (height) is driven by its CHILDREN's measured size, NOT by the slot's `SizeY`/`OffsetBottom`.**
  Changing the parent slot's vertical offsets did nothing. So for a layout-widget container, the slot
  height is not authoritative — the children are.
- Therefore the doc's "point-anchor -> size from SizeY" rule is **at best incomplete for layout-widget
  containers**: a `HorizontalLayoutWidget`/`VerticalLayoutWidget` sizes ITSELF to its children on the
  cross axis regardless of the slot. A negative container height means the **children measured negative**.
- The children (tab tiles) were a bare `ButtonWidget` -> `SizeLayoutWidget(110x52, Allow*Override,
  Min/MaxDesired)` -> `OverlayWidget` -> [`ImageWidget` bg + `VerticalLayout`[icon + `TextWidget`]].
  A recurring engine error **`TextWidget : Position/Size works only when min and max anchor is the same
  in given direction`** fires during this menu's setup — suspected to originate INSIDE the tab tile
  (the label), and to be what produces the bad/negative measurement that propagates up to the
  container. **UNCONFIRMED which widget emits it**, but it correlates with the tab tile.
- **Open action:** simplified the tab tile to `ButtonWidget -> OverlayWidget -> [ImageWidget bg +
  TextWidget label]` (removed the `SizeLayoutWidget` and the `VerticalLayout`) to test whether the
  `SizeLayoutWidget` inside a button inside a HorizontalLayout is the source of the negative measure.
  Result pending a restart.

### Revised working rules (superseding section 8 where they conflict)
- For a `HorizontalLayoutWidget`/`VerticalLayoutWidget` container, **do not rely on the parent
  `FrameWidgetSlot` to set the cross-axis size** — the container fits its children. Size the CHILDREN.
- If a layout-widget container reports a negative or zero size at runtime, **the bug is in the
  children's measurement**, not the container's slot. Instrument child `GetScreenSize` to find which
  child measures wrong.
- Treat `SizeLayoutWidget` inside a `ButtonWidget` inside a `HorizontalLayoutWidget` as **suspect**
  until proven — it may not propagate a clean desired size (contradicts section 8's claim; under test).
- The `TextWidget: Position/Size works only when min and max anchor is the same` error must be chased
  down and eliminated — a mis-anchored `TextWidget` in the subtree appears able to poison the whole
  container's measurement.

### Update 2 (narrowed the tab measurement bug)
After simplifying the tab tile to `ButtonWidget -> OverlayWidget -> [ImageWidget bg + TextWidget label]`,
the diagnostic reported:
```
tab0 size=(83.398, -166.296)   // WIDTH is now positive & real; HEIGHT is negative (== container height)
```
So: **width measures fine; only HEIGHT is negative, and it equals the container's height** — i.e. the
HorizontalLayout stretches each child's height to the (already-negative) container height. The negative
originates at the container's cross-axis, and the only thing that fits is the `TextWidget` anchor error.
- **Hypothesis under test:** the `TabLabel`'s OverlayWidgetSlot used `HorizontalAlign 1 / VerticalAlign 1`
  (center-align on the SLOT). On an `OverlayWidgetSlot`, alignment `1` (center) for a `TextWidget` is
  what emits `Position/Size works only when min and max anchor is the same`. **Fix applied:** set the
  slot's `HorizontalAlign 3 / VerticalAlign 3` (FILL) and center the TEXT via the widget's own
  `"Horizontal Alignment" Center` / `"Vertical Alignment" Center` properties (text-render props, NOT
  slot anchors). Result pending.
- **Rule (tentative):** for a `TextWidget` in an `OverlayWidgetSlot`, use slot align `3` (fill) + the
  widget's own `"Horizontal/Vertical Alignment"` properties to place the text; do NOT use slot align
  `1` (center) on a TextWidget — it triggers the anchor error and can produce a negative measured size.

---

## 10. CONFIRMED FINDINGS (this is the section to trust — verified live)

Two things were finally nailed down with live diagnostics + a deliberate cache-buster test:

### 10.1 Layout reload DOES work — there was no caching bug
Suspicion: `GRAD_ArsenalMenu.layout` edits weren't taking effect (multiple `OffsetBottom` changes gave
an identical runtime size). **Test:** changed the menu Title text to "ARSENAL MANAGER **v2**" and
changed the CategoryList slot at the same time.
- **Result: the title showed "v2" AND `listSize` width jumped from `781` to `2427`.** So the layout
  file IS reloaded on a play-mode restart (after reimport). **There is no stale-layout caching issue.**
- The reason earlier `OffsetBottom` edits "did nothing": they genuinely have no effect on a
  `HorizontalLayoutWidget`'s cross-axis size — see 10.2. The edits WERE loaded; they were just inert.
- **Workflow that works:** edit `.layout` on disk -> reimport it in Workbench Resource Browser ->
  play-mode restart. That reliably loads the new layout. (If a layout is open in the Workbench layout
  EDITOR tab, close it first — an open editor can re-save stale content over your disk edit.)

### 10.2 A layout-widget container's CROSS-AXIS size = its children's measured size (NOT its slot)
Confirmed: `CategoryList` is a `HorizontalLayoutWidget`. Its `FrameWidgetSlot` `SizeY`/`OffsetBottom`
had **zero** effect on the reported height; only the CHILDREN's measured height mattered.
- With bad children the container measured `-166`. Fixing the container slot to full-width (copying the
  working Title slot: `Anchor 0 0 1 0`, `SizeX -80`, `OffsetRight -100`, `SizeY 56`, `OffsetBottom -56`)
  moved the height to `-13` — still negative because the CHILDREN (tabs) still measured slightly
  negative. **Takeaway: to size a H/V-layout container's cross axis, you MUST give the children a real
  fixed size; the parent slot cross-axis is inert.**

### 10.3 The fixed-size-tile recipe that we're standardizing on
A tile that must be a fixed WxH inside a Horizontal/Vertical/Grid layout:
```
ButtonWidgetClass { style blank  components { SCR_InputButtonComponent {...} }
 {
  SizeLayoutWidgetClass {
   AllowWidthOverride 1  WidthOverride W   AllowHeightOverride 1  HeightOverride H
   MinDesiredWidth W  MinDesiredHeight H  MaxDesiredWidth W  MaxDesiredHeight H   // Min==Max==target
   { OverlayWidgetClass {           // NB: SizeLayout child Slot is SizeLayoutSlot, or OMIT the Slot line (defaults correctly)
      ImageWidgetClass  { Slot OverlayWidgetSlot { HorizontalAlign 3 VerticalAlign 3 } ... }   // bg fills
      TextWidgetClass   { Slot OverlayWidgetSlot { HorizontalAlign 3 VerticalAlign 3 }         // label fills slot
                          "Horizontal Alignment" Center "Vertical Alignment" Center }          // text centered via WIDGET props
   } }
  }
 }
}
```
Key rules baked in:
- Both `AllowWidthOverride`+`AllowHeightOverride` AND all four `Min/MaxDesired*` set to the target — a
  SizeLayout with only `HeightOverride` (no Min/Max, or width unset) measures oddly.
- Tile child of the SizeLayout: use `Slot SizeLayoutSlot` OR omit the Slot line entirely (vanilla omits it).
- Center a TextWidget with slot align `3` (fill) + the widget's own `"Horizontal/Vertical Alignment"
  Center` props. NEVER slot-align `1` on a TextWidget (emits the anchor error, poisons measurement).
- This same recipe fixes both the tabs and the item-card grid overlap (same class of bug).

### 10.5 A bare HorizontalLayoutWidget in a FrameSlot won't hold a slot-set height; wrap it in a ScrollLayoutWidget
The cleanest fix for "my HorizontalLayout container collapses / measures negative" is to **NOT put the
HorizontalLayout directly in the FrameWidgetSlot.** Instead wrap it in a `ScrollLayoutWidget` that takes
the FrameWidgetSlot: a `ScrollLayoutWidget` HAS a definite size from its slot (proven by the working
`ItemListScroll` -> `ItemList` GridLayout), and the inner Horizontal/Vertical layout scrolls within it.
- Working reference in THIS file: `ItemListScroll` (`ScrollLayoutWidgetClass`, `Slot FrameWidgetSlot
  { Anchor 0 0 0.34 1 ... OffsetBottom -220 }`, **no scrollbar-mode props**) whose sole child is a
  layout widget with a **bare `{}`** (no explicit `ScrollLayoutSlot` line — it defaults correctly).
- Applied to tabs: `CategoryScroll` (ScrollLayoutWidget, `Anchor 0 0 1 0 / OffsetTop 72 / SizeY 56`)
  -> `CategoryList` (HorizontalLayout, bare). This also gives horizontal scrolling for overflow tabs
  for free.
- Bonus: a `ScrollLayoutWidget` tolerates the point-Y-anchor + `SizeY` form (unlike a bare layout
  widget), because the scroll view itself is a sized container, not one that fits to children.

### 10.6 TABS FINALLY WORKING — the full recipe that succeeded
After wrapping the tab bar in a `ScrollLayoutWidget` (10.5) and giving tabs the fixed-tile recipe
(10.3), the diagnostic confirmed:
```
DiagTabSizes: listPos=(85.79, 81.6)  listSize=(680, 54.4)   // POSITIVE height, LEFT side
DiagTabSizes: tab0 pos=(85.79, 81.6)  size=(136, 54.4)  visible=1
```
The winning combination:
- Tab bar = `ScrollLayoutWidget` (`CategoryScroll`) in the FrameWidgetSlot, confined to the left pane
  width: `Anchor 0 0 0.34 0`, `OffsetLeft 40`, `OffsetRight -8`, `SizeY 56`. (Matches the item list's
  0->0.34 width so it sits ABOVE the item list, not into the center preview.)
- Its single child = the `HorizontalLayoutWidget` (`CategoryList`) with a **bare `{ }`** (NO Slot line).
- Each tab tile = the 10.3 fixed-tile recipe (bare ButtonWidget + SizeLayout with all Min/Max set).
- The earlier "centered at x=968" symptom was a full-width scroll centering its narrow content; fixed
  by confining the scroll to 0->0.34.

### 10.4 Still-open at time of writing
- Tab tiles rewritten to 10.3's recipe (120x48); pending a restart to confirm `tab0 size=(120,48)`,
  positive container height, and 5 visible boxes.
- The `TextWidget: Position/Size works only when min and max anchor is the same` error STILL fires once
  early (before tab build) — so at least one instance is NOT the tab label; source still UNCONFIRMED,
  but it did not block the container once children had real sizes.
