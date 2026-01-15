#pragma once

#include <QWidget>

class RangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit RangeSlider(Qt::Orientation orientation = Qt::Horizontal, QWidget *parent = nullptr);

    void setRange(int minimum, int maximum);
    void setLowerValue(int value);
    void setUpperValue(int value);
    int lowerValue() const { return m_lower; }
    int upperValue() const { return m_upper; }

signals:
    void rangeChanged(int lower, int upper);
    void lowerValueChanged(int lower);
    void upperValueChanged(int upper);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int valueToPos(int value) const;
    int posToValue(int pos) const;
    void clampValues();

    Qt::Orientation m_orientation;
    int m_min = 0;
    int m_max = 100;
    int m_lower = 25;
    int m_upper = 75;
    int m_handleRadius = 8;
    bool m_dragLower = false;
    bool m_dragUpper = false;
};
