#pragma once

#include <QWidget>
#include <QImage>
#include <QWheelEvent>
#include <vector>
#include <functional>

class OrthogonalView : public QWidget {
    Q_OBJECT
public:
    explicit OrthogonalView(QWidget *parent = nullptr);

    void setImage(const QImage &img);
    void setOverlayDraw(std::function<void(QPainter &p, float scale)> func);
    void setUserZoom(float z) { m_userZoom = z; update(); }
    float userZoom() const { return m_userZoom; }
    void resetView() { m_userZoom = 1.0f; m_pan = QPoint(0,0); update(); }

signals:
    void mouseClicked(int x, int y, Qt::MouseButton button);
    void mousePressed(int x, int y, Qt::MouseButton button);
    void mouseReleased(int x, int y, Qt::MouseButton button);
    void mouseMoved(int x, int y, Qt::MouseButtons buttons);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QImage m_image;
    std::function<void(QPainter &p, float scale)> m_overlay;
    float m_userZoom = 1.0f;
    QPoint m_pan{0,0};
    bool m_middleDown = false;
    QPoint m_middleStart{0,0};
    bool m_middleZoom = false;
};
