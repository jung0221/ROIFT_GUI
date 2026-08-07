#include "RangeSlider.h"

#include "Theme.h"

#include <QPainter>
#include <QMouseEvent>
#include <QtMath>
#include <algorithm>

RangeSlider::RangeSlider(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent), m_orientation(orientation) {
    setMouseTracking(true);
    setMinimumHeight(28);
}

void RangeSlider::setRange(int minimum, int maximum) {
    if (maximum <= minimum) {
        maximum = minimum + 1;
    }
    m_min = minimum;
    m_max = maximum;
    clampValues();
    update();
}

void RangeSlider::setLowerValue(int value) {
    int v = std::clamp(value, m_min, m_upper);
    if (v == m_lower) return;
    m_lower = v;
    emit lowerValueChanged(m_lower);
    emit rangeChanged(m_lower, m_upper);
    update();
}

void RangeSlider::setUpperValue(int value) {
    int v = std::clamp(value, m_lower, m_max);
    if (v == m_upper) return;
    m_upper = v;
    emit upperValueChanged(m_upper);
    emit rangeChanged(m_lower, m_upper);
    update();
}

void RangeSlider::clampValues() {
    if (m_lower < m_min) m_lower = m_min;
    if (m_upper > m_max) m_upper = m_max;
    if (m_lower > m_upper) m_lower = m_upper;
}

int RangeSlider::valueToPos(int value) const {
    double span = static_cast<double>(m_max - m_min);
    if (span <= 0.0) span = 1.0;
    double t = (static_cast<double>(value - m_min)) / span;
    int left = m_handleRadius + 4;
    int right = width() - m_handleRadius - 4;
    return left + static_cast<int>(t * (right - left));
}

int RangeSlider::posToValue(int pos) const {
    int left = m_handleRadius + 4;
    int right = width() - m_handleRadius - 4;
    if (right <= left) return m_min;
    int x = std::clamp(pos, left, right);
    double t = static_cast<double>(x - left) / static_cast<double>(right - left);
    return static_cast<int>(std::round(m_min + t * (m_max - m_min)));
}

void RangeSlider::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int midY = height() / 2;
    const int left = m_handleRadius + 4;
    const int right = width() - m_handleRadius - 4;

    // The groove keeps its black-to-white ramp: this control sets a CT window,
    // so the gradient is data — it shows which intensities the handles bracket.
    // Everything around it takes theme tokens.
    const QRect groove(left, midY - 4, right - left, 8);
    QLinearGradient grad(groove.topLeft(), groove.topRight());
    grad.setColorAt(0.0, QColor(0, 0, 0));
    grad.setColorAt(1.0, QColor(255, 255, 255));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(groove, 4, 4);

    // Everything outside the window is scrimmed back toward the well, so the
    // kept range reads as the lit part rather than as an overlay on top.
    const int lowPos = valueToPos(m_lower);
    const int highPos = valueToPos(m_upper);
    QColor scrim(Theme::kWell);
    scrim.setAlpha(200);
    p.setBrush(scrim);
    p.drawRoundedRect(QRect(groove.left(), groove.top(), lowPos - groove.left(), groove.height()), 4, 4);
    p.drawRoundedRect(QRect(highPos, groove.top(), groove.right() - highPos + 1, groove.height()), 4, 4);

    // Handles are round because they are physical grips, the one place the ban
    // on pills does not apply.
    auto drawHandle = [&p, midY](int x, bool active) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(active ? Theme::kAccent : Theme::kInk));
        p.drawEllipse(QPoint(x, midY), 6, 6);
    };

    drawHandle(lowPos, m_dragLower);
    drawHandle(highPos, m_dragUpper);
}

void RangeSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    int lowPos = valueToPos(m_lower);
    int highPos = valueToPos(m_upper);
    int dxLow = std::abs(event->pos().x() - lowPos);
    int dxHigh = std::abs(event->pos().x() - highPos);
    if (dxLow <= dxHigh) {
        m_dragLower = true;
    } else {
        m_dragUpper = true;
    }
    setCursor(Qt::ClosedHandCursor);
}

void RangeSlider::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragLower && !m_dragUpper) return;
    int value = posToValue(event->pos().x());
    if (m_dragLower) {
        setLowerValue(value);
    } else if (m_dragUpper) {
        setUpperValue(value);
    }
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    if (m_dragLower || m_dragUpper) {
        unsetCursor();
    }
    m_dragLower = false;
    m_dragUpper = false;
}
