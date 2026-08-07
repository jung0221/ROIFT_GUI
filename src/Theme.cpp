#include "Theme.h"

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSlider>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>

namespace Theme
{

namespace
{

/// The style sheet is written against readable token names ("@panel", "@ink2")
/// and the tokens are substituted in once, so the hex values live in Theme.h
/// only. Editing a colour is a one-line change there, not a search-and-replace.
const char *kStyleSheetTemplate = R"QSS(
/* ---------------------------------------------------------------------------
   Base: the bezel everything floats on.
   --------------------------------------------------------------------------- */
QMainWindow, QDialog {
    background-color: @bezel;
}
QWidget {
    background-color: transparent;
    color: @ink2;
    font-family: 'Inter', 'Segoe UI', 'Ubuntu', sans-serif;
    font-size: @bodypx;
}
QMainWindow > QWidget, QDialog > QWidget {
    background-color: @bezel;
}

/* ---------------------------------------------------------------------------
   Toolbar: a panel strip, divided from the work area by one hairline.
   The hairline divides; it does not lift.
   --------------------------------------------------------------------------- */
QToolBar {
    background-color: @panel;
    border: none;
    border-bottom: 1px solid @hairline;
    spacing: 2px;
    padding: 5px 6px;
}
QToolBar::separator {
    background-color: @hairline;
    width: 1px;
    margin: 5px 7px;
}
QToolBar QToolButton {
    background-color: transparent;
    border: none;
    border-radius: @capr;
    padding: 5px 10px;
    color: @ink2;
    font-size: @bodypx;
}
QToolBar QToolButton:hover {
    background-color: @panelraised;
    color: @ink;
}
QToolBar QToolButton:pressed {
    background-color: @well;
}
QToolBar QToolButton:checked {
    background-color: @accentfill;
    color: @onaccent;
}

/* ---------------------------------------------------------------------------
   Buttons: a well-coloured face on a panel, no border at rest.
   Checked is the one state that earns the accent fill — it is state, not decor.
   --------------------------------------------------------------------------- */
QPushButton {
    background-color: @well;
    border: none;
    border-radius: @fieldr;
    padding: 0px 8px;
    min-height: @controlh;
    max-height: @controlh;
    color: @ink2;
    font-size: @bodypx;
}
QPushButton:hover {
    background-color: @panelraised;
    color: @ink;
}
QPushButton:pressed {
    background-color: @well;
    color: @ink;
}
QPushButton:checked {
    background-color: @accentfill;
    color: @onaccent;
    font-weight: 600;
}
QPushButton:checked:hover {
    background-color: @accentfillhover;
}
QPushButton:focus {
    outline: none;
}
QPushButton:disabled {
    background-color: @panel;
    color: @inkdisabled;
}

/* The one primary action per section. Filled, because it commits work. */
QPushButton#runButton {
    background-color: @accentfill;
    color: @onaccent;
    font-weight: 600;
    min-height: 30px;
    max-height: 30px;
}
QPushButton#runButton:hover  { background-color: @accentfillhover; }
QPushButton#runButton:pressed { background-color: @accent; }
QPushButton#runButton:disabled {
    background-color: @well;
    color: @inkdisabled;
}

/* ---------------------------------------------------------------------------
   Tool sections: one card per section, no inner frames.
   --------------------------------------------------------------------------- */
QScrollArea#toolSidebar, QScrollArea#toolSidebar > QWidget > QWidget {
    background-color: @bezel;
    border: none;
}
QWidget#sectionCard {
    background-color: @panel;
    border-radius: @cardr;
}
QToolButton#sectionHeader {
    background-color: transparent;
    border: none;
    border-radius: @rowr;
    padding: 6px 8px;
    color: @ink;
    font-size: @titlepx;
    font-weight: 500;
    text-align: left;
}
QToolButton#sectionHeader:hover {
    background-color: @panelraised;
}
QWidget#sectionBody {
    background-color: transparent;
}

/* Group label inside a section: metadata ink, never accent. */
QLabel#groupLabel {
    color: @ink3;
    font-size: @labelpx;
    font-weight: 600;
    background: transparent;
}
/* A sentence of explanation, not a control. Quiet, and it wraps. */
QLabel#hintLabel {
    color: @ink3;
    font-size: @labelpx;
    background: transparent;
}

/* ---------------------------------------------------------------------------
   Fields: recessed wells. Focus states the field, it does not decorate it.
   Two focus signals, per interior: the border colours and the surface lifts.
   --------------------------------------------------------------------------- */
QSpinBox, QDoubleSpinBox, QLineEdit, QComboBox {
    background-color: @well;
    border: 1px solid @hairline;
    border-radius: @fieldr;
    padding: 0px 5px;
    min-height: @fieldinnerh;
    max-height: @fieldinnerh;
    color: @ink;
    font-size: @bodypx;
    selection-background-color: @accentfill;
    selection-color: @onaccent;
}
QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover, QComboBox:hover {
    border-color: @panelraised;
}
QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus, QComboBox:focus {
    border-color: @accent;
    background-color: @panelraised;
}
QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled, QComboBox:disabled {
    color: @inkdisabled;
    background-color: @panel;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 15px;
    height: 11px;
    margin: 1px 2px 0px 0px;
    background-color: transparent;
    border: none;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 15px;
    height: 11px;
    margin: 0px 2px 1px 0px;
    background-color: transparent;
    border: none;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: url("@chevronup");
    width: 8px;
    height: 8px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: url("@chevrondown");
    width: 8px;
    height: 8px;
}
QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled,
QDoubleSpinBox::up-arrow:disabled, QDoubleSpinBox::down-arrow:disabled {
    image: none;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    border: none;
    width: 16px;
}
QComboBox::down-arrow {
    image: url("@chevrondown");
    width: 9px;
    height: 9px;
}
QComboBox QAbstractItemView {
    background-color: @panel;
    border: 1px solid @hairline;
    border-radius: @fieldr;
    padding: 3px;
    color: @ink2;
    selection-background-color: @accentfill;
    selection-color: @onaccent;
    outline: none;
}

/* ---------------------------------------------------------------------------
   Lists and trees: wells. Selection is accent because selection is state.
   --------------------------------------------------------------------------- */
QListWidget, QTreeWidget, QTreeView, QListView {
    background-color: @well;
    border: none;
    border-radius: @rowr;
    padding: 3px;
    color: @ink2;
    outline: none;
}
QListWidget::item, QTreeWidget::item {
    padding: 4px 6px;
    border-radius: @capr;
    min-height: 18px;
}
QListWidget::item:hover:!selected, QTreeWidget::item:hover:!selected {
    background-color: @panelraised;
    color: @ink;
}
QListWidget::item:selected, QTreeWidget::item:selected {
    background-color: @accentfill;
    color: @onaccent;
}

/* ---------------------------------------------------------------------------
   Menus and tooltips: panels floating over the work area.
   --------------------------------------------------------------------------- */
QMenu {
    background-color: @panel;
    border: 1px solid @hairline;
    border-radius: @shellr;
    padding: 4px;
    color: @ink2;
}
QMenu::item {
    padding: 6px 20px;
    border-radius: @capr;
}
QMenu::item:selected {
    background-color: @accentfill;
    color: @onaccent;
}
QMenu::separator {
    height: 1px;
    background-color: @hairline;
    margin: 4px 6px;
}
QToolTip {
    background-color: @panelraised;
    color: @ink;
    border: 1px solid @hairline;
    border-radius: @capr;
    padding: 4px 7px;
    font-size: @labelpx;
}

/* ---------------------------------------------------------------------------
   Sliders: a well groove with an accent thumb. The thumb is the only round
   thing in the app, and it is round because it is a physical handle.
   --------------------------------------------------------------------------- */
QSlider::groove:horizontal {
    background-color: @well;
    border: none;
    height: 4px;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background-color: @accentfill;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background-color: @ink;
    border: none;
    width: 12px;
    height: 12px;
    margin: -4px 0;
    border-radius: 6px;
}
QSlider::handle:horizontal:hover {
    background-color: @accent;
}
QSlider::groove:vertical {
    background-color: @well;
    border: none;
    width: 4px;
    border-radius: 2px;
}
QSlider::handle:vertical {
    background-color: @ink;
    border: none;
    width: 12px;
    height: 12px;
    margin: 0 -4px;
    border-radius: 6px;
}

/* ---------------------------------------------------------------------------
   Checkboxes: a well box that fills with accent when it carries state.
   --------------------------------------------------------------------------- */
QCheckBox {
    spacing: 7px;
    color: @ink2;
    font-size: @bodypx;
    background: transparent;
    min-height: 20px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    border-radius: 4px;
    border: 1px solid @hairline;
    background-color: @well;
}
QCheckBox::indicator:hover {
    border-color: @ink3;
}
QCheckBox::indicator:checked {
    background-color: @accentfill;
    border-color: @accentfill;
    image: none;
}
QCheckBox::indicator:disabled {
    border-color: @panel;
    background-color: @panel;
}
QRadioButton {
    spacing: 7px;
    color: @ink2;
    font-size: @bodypx;
    background: transparent;
}
QRadioButton::indicator {
    width: 14px;
    height: 14px;
    border-radius: 7px;
    border: 1px solid @hairline;
    background-color: @well;
}
QRadioButton::indicator:checked {
    background-color: @accentfill;
    border-color: @accentfill;
}

/* ---------------------------------------------------------------------------
   Text, tabs, splitters.
   --------------------------------------------------------------------------- */
QLabel {
    color: @ink2;
    font-size: @bodypx;
    background: transparent;
}
QLabel:disabled {
    color: @inkdisabled;
}

QTabWidget::pane {
    background-color: @panel;
    border: none;
    border-radius: @cardr;
}
QTabBar::tab {
    background-color: transparent;
    border: none;
    border-radius: @fieldr;
    padding: 6px 12px;
    margin-right: 2px;
    color: @ink3;
    font-size: @bodypx;
}
QTabBar::tab:selected {
    background-color: @accentfill;
    color: @onaccent;
    font-weight: 600;
}
QTabBar::tab:hover:!selected {
    background-color: @panelraised;
    color: @ink2;
}

QSplitter::handle {
    background-color: transparent;
}
QSplitter::handle:hover {
    background-color: @accentwash;
}

/* ---------------------------------------------------------------------------
   Scrollbars: reserve the track from first paint, draw nothing until needed.
   --------------------------------------------------------------------------- */
QScrollBar:vertical {
    background: transparent;
    width: 9px;
    margin: 0px;
}
QScrollBar::handle:vertical {
    background-color: @panelraised;
    border-radius: 4px;
    min-height: 28px;
}
QScrollBar::handle:vertical:hover {
    background-color: @ink3;
}
QScrollBar:horizontal {
    background: transparent;
    height: 9px;
    margin: 0px;
}
QScrollBar::handle:horizontal {
    background-color: @panelraised;
    border-radius: 4px;
    min-width: 28px;
}
QScrollBar::handle:horizontal:hover {
    background-color: @ink3;
}
QScrollBar::add-line, QScrollBar::sub-line {
    height: 0px;
    width: 0px;
    border: none;
    background: none;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}

/* ---------------------------------------------------------------------------
   Bottom bar: log well, mono status, moss progress.
   Mono is for metadata and numbers only — which is exactly what these are.
   --------------------------------------------------------------------------- */
QPlainTextEdit#logConsole {
    background-color: @panel;
    border: none;
    border-radius: @rowr;
    padding: 7px 9px;
    font-family: 'JetBrains Mono', 'Consolas', 'DejaVu Sans Mono', monospace;
    font-size: @labelpx;
    color: @ink2;
    selection-background-color: @accentfill;
    selection-color: @onaccent;
}
QLabel#statusLabel {
    background-color: @panel;
    border: none;
    border-radius: @rowr;
    padding: 6px 10px;
    color: @ink3;
    font-family: 'JetBrains Mono', 'Consolas', 'DejaVu Sans Mono', monospace;
    font-size: @labelpx;
}
QProgressBar#progressBar {
    background-color: @well;
    border: none;
    border-radius: @capr;
    color: @ink2;
    font-size: @labelpx;
    padding: 0px;
    min-height: 18px;
    max-height: 18px;
    text-align: center;
}
QProgressBar#progressBar::chunk {
    background-color: @moss;
    border-radius: @capr;
}

/* ---------------------------------------------------------------------------
   Groups: no box, just a label band reserved above the content.

   Setting `border` here is what switches the group over to the style-sheet box
   model, so `margin-top` becomes the label band and the contents start below it.
   SectionGroup paints the label into that band; the ::title rule below only
   matters for a plain QGroupBox that has not been converted yet.
   --------------------------------------------------------------------------- */
QGroupBox {
    background-color: transparent;
    border: none;
    margin-top: @labelband;
    padding: 0px;
    font-size: @labelpx;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 1px;
    top: 1px;
    padding: 0px;
    color: @ink3;
}
/* SectionGroup draws its own label, so hide the native one entirely. */
QGroupBox#sectionGroup::title {
    color: transparent;
}
)QSS";

/// Qt style sheets can only reference an arrow as an image URL, and there is no
/// .qrc in this project to put one in. So the two chevrons the theme needs are
/// drawn once at startup and written beside the app's other cache files; the
/// style sheet then points at those paths. Same two strokes as the section
/// header chevron, so every disclosure in the app is the same mark.
QString chevronPath(bool pointingDown)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(pointingDown ? "chevron-down.png" : "chevron-up.png");

    const int logical = 9;
    const int scale = 3; // drawn oversized so hidpi downsampling stays crisp
    QImage img(logical * scale, logical * scale, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(scale, scale);
    QPen pen{QColor(kInk3)};
    pen.setWidthF(1.3);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    QPainterPath path2;
    if (pointingDown)
    {
        path2.moveTo(2.0, 3.5);
        path2.lineTo(4.5, 6.0);
        path2.lineTo(7.0, 3.5);
    }
    else
    {
        path2.moveTo(2.0, 6.0);
        path2.lineTo(4.5, 3.5);
        path2.lineTo(7.0, 6.0);
    }
    p.drawPath(path2);
    p.end();

    img.save(path, "PNG");
    return path;
}

/// Takes wheel events away from the widget under the cursor and hands them to
/// the scroll area behind it.
///
/// Leaving the event unaccepted is not enough on its own: measured against the
/// real window, QApplication's propagation walk does not carry the event out to
/// the enclosing scroll area, and the scroll simply dies. So the event is
/// forwarded to that scroll area's viewport by hand. With no scroll area above
/// the control — the slice sliders, the right sidebar — there is nothing to
/// scroll and the wheel is dropped, which is the intended "does nothing".
class WheelGuard : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);

        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (!widget)
            return false;

        QAbstractScrollArea *area = nullptr;
        for (QWidget *p = widget->parentWidget(); p; p = p->parentWidget())
        {
            if ((area = qobject_cast<QAbstractScrollArea *>(p)))
                break;
        }
        if (!area)
            return true;

        const QWheelEvent *src = static_cast<QWheelEvent *>(event);
        QWidget *viewport = area->viewport();
        QWheelEvent forwarded(viewport->mapFromGlobal(src->globalPosition().toPoint()),
                              src->globalPosition(),
                              src->pixelDelta(),
                              src->angleDelta(),
                              src->buttons(),
                              src->modifiers(),
                              src->phase(),
                              src->inverted(),
                              src->source());
        QCoreApplication::sendEvent(viewport, &forwarded);
        return true;
    }
};

QHash<QString, QString> tokenTable()
{
    QHash<QString, QString> t;
    t["@bezel"] = kBezel;
    t["@panelraised"] = kPanelRaised; // before @panel: longest key first
    t["@panel"] = kPanel;
    t["@well"] = kWell;
    t["@hairline"] = kHairline;
    t["@inkdisabled"] = kInkDisabled;
    t["@ink3"] = kInk3;
    t["@ink2"] = kInk2;
    t["@ink"] = kInk;
    t["@accentfillhover"] = kAccentFillHover;
    t["@accentfill"] = kAccentFill;
    t["@accentwash"] = kAccentWash;
    t["@accent"] = kAccent;
    t["@onaccent"] = kOnAccent;
    t["@moss"] = kMoss;
    t["@flag"] = kFlag;
    t["@cardr"] = QString("%1px").arg(kRadiusCard);
    t["@shellr"] = QString("%1px").arg(kRadiusShell);
    t["@rowr"] = QString("%1px").arg(kRadiusRow);
    t["@fieldr"] = QString("%1px").arg(kRadiusField);
    t["@capr"] = QString("%1px").arg(kRadiusCap);
    t["@controlh"] = QString("%1px").arg(kControlHeight);
    // Fields carry a 1px border, so their content box is 2px shorter than a
    // borderless button of the same visual height.
    t["@fieldinnerh"] = QString("%1px").arg(kControlHeight - 2);
    t["@chevrondown"] = chevronPath(true);
    t["@chevronup"] = chevronPath(false);
    t["@labelband"] = QString("%1px").arg(kLabelBand);
    t["@labelpx"] = QString("%1px").arg(kLabelFontSize);
    t["@bodypx"] = QString("%1px").arg(kBodyFontSize);
    t["@titlepx"] = QString("%1px").arg(kTitleFontSize);
    return t;
}

} // namespace

QString styleSheet()
{
    QString qss = QString::fromUtf8(kStyleSheetTemplate);
    const QHash<QString, QString> tokens = tokenTable();

    // Replace longest keys first so "@ink3" is not eaten by "@ink".
    QStringList keys = tokens.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b)
              { return a.size() > b.size(); });
    for (const QString &key : keys)
        qss.replace(key, tokens.value(key));

    return qss;
}

void applyLabelStyle(QLabel *label)
{
    if (!label)
        return;
    label->setObjectName("groupLabel");
    label->setText(label->text().toUpper());

    // Qt style sheets have no letter-spacing property, so tracking is the one
    // typographic rule that has to be set on the QFont directly.
    QFont f = label->font();
    f.setPixelSize(kLabelFontSize);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, kLabelTracking);
    label->setFont(f);
}

void applyHintStyle(QLabel *label)
{
    if (!label)
        return;
    label->setObjectName("hintLabel");
    label->setWordWrap(true);
}

void guardWheel(QWidget *root)
{
    if (!root)
        return;

    // One filter object for the whole application: it is stateless, so there is
    // no reason for each widget to own a copy.
    static WheelGuard *guard = new WheelGuard(QCoreApplication::instance());

    const auto guardOne = [](QWidget *w)
    {
        // Wheel focus is the other half of the problem: without this, rolling
        // over a spin box would still steal focus from whatever had it.
        if (w->focusPolicy() == Qt::WheelFocus)
            w->setFocusPolicy(Qt::StrongFocus);
        w->installEventFilter(guard);
    };

    // QSlider rather than QAbstractSlider on purpose: a QScrollBar is also an
    // abstract slider, and a scroll bar is exactly what the wheel is for.
    for (QSlider *w : root->findChildren<QSlider *>())
        guardOne(w);
    for (QAbstractSpinBox *w : root->findChildren<QAbstractSpinBox *>())
        guardOne(w);
    for (QComboBox *w : root->findChildren<QComboBox *>())
        guardOne(w);
}

} // namespace Theme
