#pragma once

/**
 * Theme.h — the ROIFT GUI design language, in one place.
 *
 * Ported from the interior.dev design language (github.com/ddoemonn/interior),
 * dark mode, adapted to a dense medical viewer. The three ideas that matter:
 *
 *   1. Depth comes from *material*, never from borders. Three surfaces stack:
 *      bezel (the frame) < panel (lifted cards) < well (recessed slots).
 *      A border in this app divides compartments; it never implies elevation.
 *   2. Accent marks interaction and state only — active tool, focus, selection.
 *      It is never used to colour a heading or decorate a frame.
 *   3. Radii are derived, not guessed: outer = inner + the padding between them.
 *
 * Everything below is a token. Widgets read tokens; they do not hard-code hex.
 */

#include <QString>

class QLabel;
class QWidget;

namespace Theme
{

// ---------------------------------------------------------------------------
// Material — three surfaces, in stacking order
// ---------------------------------------------------------------------------

/// The frame the whole app floats on. Nothing sits behind it.
inline constexpr const char *kBezel = "#141312";
/// Lifted cards: tool sections, view panels, sidebar blocks, menus.
inline constexpr const char *kPanel = "#1D1D1A";
/// Recessed slots: fields, lists, the log console, slider grooves.
inline constexpr const char *kWell = "#252522";
/// A panel raised above another panel (hovered rows, pressed buttons).
inline constexpr const char *kPanelRaised = "#2A2A26";

/// Compartment division only — toolbar underline, splitter handles.
/// Never put this on a card to make it look lifted; that is what kPanel is for.
inline constexpr const char *kHairline = "#302F2B";

// ---------------------------------------------------------------------------
// Ink — three weights of text, darkest surface to lightest reader
// ---------------------------------------------------------------------------

/// Primary: values, active labels, card titles.
inline constexpr const char *kInk = "#F3F3EF";
/// Body: field text, list rows, ordinary controls.
inline constexpr const char *kInk2 = "#C2C2BA";
/// Metadata: group labels, units, hints, disabled affordances.
inline constexpr const char *kInk3 = "#93938B";
/// Disabled: present but inert.
inline constexpr const char *kInkDisabled = "#5C5C56";

// ---------------------------------------------------------------------------
// Accent and status
// ---------------------------------------------------------------------------

/// Dark-mode accent. Focus rings, checked borders, selection text.
inline constexpr const char *kAccent = "#93B0FF";
/// The brand blue. Filled interactive surfaces (checked buttons, selected rows).
inline constexpr const char *kAccentFill = "#4568FF";
/// Hover state of a filled accent surface.
inline constexpr const char *kAccentFillHover = "#5A79FF";
/// Text drawn on top of a filled accent surface.
inline constexpr const char *kOnAccent = "#0E1020";
/// A 6% accent wash — inset focus on rows inside a container.
inline constexpr const char *kAccentWash = "rgba(147, 176, 255, 0.10)";

/// Moss: success, completion, running-and-healthy.
inline constexpr const char *kMoss = "#57C88A";
/// Flag: rejection, failure, destructive confirmation.
inline constexpr const char *kFlag = "#F0796B";

// ---------------------------------------------------------------------------
// Radius nesting
//
// Derived from the containers that actually exist in this window:
//
//   section card (14) = row (9) + 5 card padding
//   row / well  (9)   = field (6) + 3 row padding
//   segmented shell (10) = segment (8) + 2 shell padding
//
// If a new container appears, derive its radius the same way instead of
// inventing one.
// ---------------------------------------------------------------------------

inline constexpr int kRadiusCard = 14;
inline constexpr int kRadiusShell = 10;
inline constexpr int kRadiusRow = 9;
inline constexpr int kRadiusField = 8;
inline constexpr int kRadiusCap = 6;

// ---------------------------------------------------------------------------
// Density
//
// interior specifies h-10 (40px) fields for a marketing-density web page. A
// three-plane CT viewer has to fit five tool sections beside the images, so the
// scale is compressed one step: 26px controls, 11px body. The *relationships*
// (label < body < title, field < row < card) are preserved exactly.
// ---------------------------------------------------------------------------

/// Height of every interactive control: buttons, fields, combos.
inline constexpr int kControlHeight = 26;
/// Padding inside a section card.
inline constexpr int kCardPadding = 5;
/// Gap between labelled groups inside one section.
inline constexpr int kGroupGap = 12;
/// Gap between rows inside one group.
inline constexpr int kRowGap = 6;

/// Section labels ("BRUSH", "FILE") — uppercase, tracked, metadata ink.
inline constexpr int kLabelFontSize = 10;
/// Tracking for uppercase labels, in px at kLabelFontSize (interior: 0.08em).
inline constexpr qreal kLabelTracking = 0.8;
/// Height of the band a group label occupies above its content. Kept in sync
/// with the QGroupBox top margin in styleSheet(); SectionGroup paints into it.
inline constexpr int kLabelBand = 15;
/// Body copy: field text, list rows, buttons.
inline constexpr int kBodyFontSize = 11;
/// Card and row titles.
inline constexpr int kTitleFontSize = 12;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// The application-wide style sheet, built from the tokens above.
/// Install once on the QMainWindow; every widget inherits from it.
QString styleSheet();

/// Turn @p label into an interior section label: uppercase, semibold, tracked,
/// metadata ink. Tracking is applied through QFont because Qt style sheets have
/// no letter-spacing property. Use for standalone labels; a labelled group of
/// controls should be a SectionGroup, which paints the same label itself.
void applyLabelStyle(QLabel *label);

/// Turn @p label into a quiet hint line: metadata ink, wrapped, no emphasis.
void applyHintStyle(QLabel *label);

/// Stop the mouse wheel from changing values anywhere under @p root.
///
/// Rolling the wheel over a slider, spin box or combo box scrolls the panel
/// underneath instead of editing the control — a value only changes when you
/// mean to change it. Scroll bars, lists and the slice views keep their wheel,
/// so scrolling still does the one thing it should.
///
/// Call once per top-level window after its widgets exist. Controls created
/// later need their own call.
void guardWheel(QWidget *root);

} // namespace Theme
