#include "OrthogonalView.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <iostream>

OrthogonalView::OrthogonalView(QWidget *parent): QWidget(parent) {
    // enable mouse move events even when no button is pressed so callers
    // can show cursor position/intensity while hovering
    setMouseTracking(true);
}

void OrthogonalView::setImage(const QImage &img) {
    m_image = img;
    update();
}

void OrthogonalView::setOverlayDraw(std::function<void(QPainter &p, float scale)> func) {
    m_overlay = func;
}

void OrthogonalView::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_image.isNull()) {
        // scale to fit while keeping aspect, then apply user zoom
        QImage fit = m_image.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
        int w = int(fit.width() * m_userZoom);
        int h = int(fit.height() * m_userZoom);
        QImage scaled = m_image.scaled(w, h, Qt::KeepAspectRatio, Qt::FastTransformation);
        float scale = float(scaled.width()) / float(m_image.width());
        int x = (width() - scaled.width()) / 2 + m_pan.x();
        int y = (height() - scaled.height()) / 2 + m_pan.y();
        p.drawImage(QRect(x,y,scaled.width(), scaled.height()), scaled);
        p.translate(x, y);
        if (m_overlay) m_overlay(p, scale);
        p.translate(-x, -y);
    }
}

// helper: map widget coords to image coords, returns false if outside image
static bool widgetToImage(const QImage &img, const QPoint &widgetPos, const QSize &widgetSize, float userZoom, const QPoint &pan, int &outX, int &outY) {
    if (img.isNull()) return false;
    QImage fit = img.scaled(widgetSize, Qt::KeepAspectRatio);
    int w = int(fit.width() * userZoom);
    int h = int(fit.height() * userZoom);
    QImage scaled = img.scaled(w, h, Qt::KeepAspectRatio);
    int xoff = (widgetSize.width() - scaled.width()) / 2 + pan.x();
    int yoff = (widgetSize.height() - scaled.height()) / 2 + pan.y();
    int xi = int((widgetPos.x() - xoff) * float(img.width()) / float(scaled.width()));
    int yi = int((widgetPos.y() - yoff) * float(img.height()) / float(scaled.height()));
    if (xi < 0 || yi < 0 || xi >= img.width() || yi >= img.height()) return false;
    outX = xi; outY = yi; return true;
}

void OrthogonalView::mousePressEvent(QMouseEvent *event) {
    if (m_image.isNull()) return;
    if (event->button() == Qt::MiddleButton) {
        m_middleDown = true;
        m_middleStart = event->pos();
        m_middleZoom = (event->modifiers() & Qt::ControlModifier);
        return;
    }
    int xi, yi;
    if (widgetToImage(m_image, event->pos(), size(), m_userZoom, m_pan, xi, yi)) {
        if (getenv("MANUAL_SEED_DEBUG")) std::cerr << "mousePress mapped: widget("<<event->x()<<","<<event->y()<<") -> img("<<xi<<","<<yi<<")\n";
        emit mousePressed(xi, yi, event->button());
    }
}

void OrthogonalView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_image.isNull()) return;
    if (event->button() == Qt::MiddleButton) {
        m_middleDown = false;
        m_middleZoom = false;
        return;
    }
    int xi, yi;
    if (widgetToImage(m_image, event->pos(), size(), m_userZoom, m_pan, xi, yi)) {
        emit mouseReleased(xi, yi, event->button());
    }
}

void OrthogonalView::mouseMoveEvent(QMouseEvent *event) {
    if (m_image.isNull()) return;
    if (m_middleDown) {
        // if middle dragging with ctrl modifier, perform zoom based on vertical delta
        QPoint delta = event->pos() - m_middleStart;
        if (m_middleZoom) {
            // vertical movement controls zoom
            float factor = 1.0f + float(delta.y()) * 0.005f;
            if (factor <= 0.01f) factor = 0.01f;
            m_userZoom *= factor;
            if (m_userZoom < 0.05f) m_userZoom = 0.05f;
            if (m_userZoom > 20.0f) m_userZoom = 20.0f;
        } else {
            m_pan += delta;
        }
        m_middleStart = event->pos();
        update();
        return;
    }
    int xi, yi;
    if (widgetToImage(m_image, event->pos(), size(), m_userZoom, m_pan, xi, yi)) {
        if (getenv("MANUAL_SEED_DEBUG")) std::cerr << "mouseMove mapped: widget("<<event->x()<<","<<event->y()<<") -> img("<<xi<<","<<yi<<")\n";
        emit mouseMoved(xi, yi, event->buttons());
    } else {
        // out of image bounds send invalid coords (-1)
        emit mouseMoved(-1, -1, event->buttons());
    }
}

void OrthogonalView::wheelEvent(QWheelEvent *event) {
    // zoom in/out with wheel
    const int delta = event->angleDelta().y();
    if (delta==0) return;
    float factor = (delta>0)?1.1f:0.9f;
    m_userZoom *= factor;
    if (m_userZoom < 0.1f) m_userZoom = 0.1f;
    if (m_userZoom > 10.0f) m_userZoom = 10.0f;
    update();
}
