# ROIFT GUI design language

Ported from [interior.dev](https://github.com/ddoemonn/interior), dark mode,
compressed one density step for a three-plane CT viewer.

Everything here is code, not prose: the tokens live in `src/Theme.h` and the
style sheet that consumes them in `src/Theme.cpp`. Read those first — this file
only explains *why* the rules are what they are.

## The three rules

**1. Depth is material, never a border.** Three surfaces stack:

| Layer | Token | What sits on it |
|---|---|---|
| Bezel | `#141312` | the window frame, the 3D canvas |
| Panel | `#1D1D1A` | section cards, the log console, menus |
| Well | `#252522` | fields, lists, buttons, slider grooves |

Qt style sheets have no `box-shadow`, so the background difference *is* the
elevation. A border in this app divides compartments (the toolbar underline);
it never implies lift. This is why the old sidebar — a bordered
`CollapsibleSection` wrapping bordered `QGroupBox`es wrapping bordered controls
— read as noise: three rectangles were competing to say "this is a group".

**2. Accent is state, not decoration.** `#4568FF` fills a control that is
*checked, selected, or running*. `#93B0FF` borders one that is *focused*.
Nothing else is blue. The old theme coloured every `QGroupBox` title
`#0078d4`, so the strongest colour on screen marked headings rather than
whatever the user had just done.

Text is three weights of ink: `#F3F3EF` primary, `#C2C2BA` body, `#93938B`
metadata. Group labels are metadata — uppercase, semibold, 10px, tracked.

**3. Radii are derived, not guessed.** Outer radius = inner radius + the padding
between them:

```
section card (14) = row (9) + 5 card padding
row / well   (9)  = field (6) + 3 row padding
segmented shell (10) = segment (8) + 2 shell padding
```

New container? Derive it the same way instead of inventing a number.

## Density

interior specifies 40px fields for a marketing page. Five tool sections have to
fit beside three orthogonal views plus a render, so controls are 26px and body
copy is 11px. The *relationships* are untouched: label < body < title, field <
row < card.

## Structure

`SectionGroup` replaces `QGroupBox`. It keeps QGroupBox's layout behaviour, so
call sites differ only in the type name, and replaces the frame with one
uppercase label. It paints that label itself rather than letting QGroupBox do
it, because Qt style sheets have no `letter-spacing` — and painting it directly
keeps the tracked `QFont` off the widget, so the fields inside the group do not
inherit it.

`CollapsibleSection` is the one box a section gets: a panel card with a chevron
header. The chevron is drawn in code (two strokes, theme ink) so it can rotate
with the disclosure state.

A first run opens Window/Level and Seeds and folds the rest
(`ManualSeedSelector::expandDefaultSections`). Nothing is removed — every header
is one click from open — and once a session saves its own expansion choices,
those win at the next startup.

There is no separate tool selector. What the left mouse button does is read back
off the two Drawing Mode rows: whichever of Seeds or Mask is not Off owns the
click, and with both Off the slice views are read-only. Choosing a mode in one
row switches the other to Off, so exactly one is ever live
(`setSeedMode` / `setMaskMode` / `syncActiveTool`). A Navigate/Seeds/Mask row on
top of those two said the same thing twice.

## The wheel scrolls; it does not edit

Rolling the wheel over a slider, spin box or combo box scrolls the panel behind
it and leaves the value untouched (`Theme::guardWheel`). A value changes only
when you mean to change it.

The guard forwards the event to the enclosing scroll area's viewport by hand.
The tidier-looking version — ignore the event and let Qt propagate it — does
not work: measured against the real window, the propagation never reaches the
scroll area and the wheel dies instead. With no scroll area above the control
(the slice sliders, the right sidebar) there is nothing to scroll and the wheel
is dropped, which is the intended "does nothing".

Scroll bars, lists, the log console and the slice views keep their wheel; the
views zoom with it. `tests/wheel_guard_test.cpp` pins all of this down.

## Two Qt-specific workarounds

- **Arrows.** Style sheets can only reference an arrow as an image URL, and
  there is no `.qrc` here. `Theme::chevronPath()` draws the two chevrons once at
  startup into the app cache directory and the sheet points at those files. The
  CSS "zero-size element with borders" triangle trick does not work in Qt.
- **Group label band.** Setting `border` on `QGroupBox` is what switches it to
  the style-sheet box model, which makes `margin-top` reserve the label band
  above the contents. `Theme::kLabelBand` and that margin must stay in sync.

## Adding to the UI

- Reach for a token, never a hex literal. If you need a colour that is not a
  token, that is a signal the design is drifting, not that a literal is fine.
- One primary action per section: `setObjectName("runButton")`. Everything else
  is a well-faced button.
- A sentence of explanation is `Theme::applyHintStyle`, not a coloured label.
