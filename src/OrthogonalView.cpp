#include "OrthogonalView.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <iostream>

// Geometry of the image as drawn into the widget. The image is fitted into the
// widget while preserving the *physical* aspect ratio (pixelAspect stretches the
// vertical axis relative to the horizontal), then scaled by the user zoom and
// centered with the pan offset. dispW/dispH are the on-screen size in pixels;
// scaleX/scaleY convert image-pixel coords to on-screen coords. Both paint and
// hit-testing go through this so they never diverge.
namespace {
struct DisplayRect {
    double dispW = 0.0;
    double dispH = 0.0;
    int xoff = 0;
    int yoff = 0;
};

DisplayRect computeDisplayRect(int imgW, int imgH, double pixelAspect,
                               const QSize &widget, float userZoom, const QPoint &pan)
{
    DisplayRect r;
    if (imgW <= 0 || imgH <= 0 || widget.width() <= 0 || widget.height() <= 0)
        return r;
    const double pw = static_cast<double>(imgW);
    const double ph = static_cast<double>(imgH) * (pixelAspect > 0.0 ? pixelAspect : 1.0);
    const double fit = std::min(static_cast<double>(widget.width()) / pw,
                                static_cast<double>(widget.height()) / ph);
    r.dispW = pw * fit * static_cast<double>(userZoom);
    r.dispH = ph * fit * static_cast<double>(userZoom);
    r.xoff = static_cast<int>((static_cast<double>(widget.width()) - r.dispW) / 2.0) + pan.x();
    r.yoff = static_cast<int>((static_cast<double>(widget.height()) - r.dispH) / 2.0) + pan.y();
    return r;
}
} // namespace

OrthogonalView::OrthogonalView(QWidget *parent): QWidget(parent) {
    // enable mouse move events even when no button is pressed so callers
    // can show cursor position/intensity while hovering
    setMouseTracking(true);
}

void OrthogonalView::setImage(const QImage &img) {
    m_image = img;
    update();
}

void OrthogonalView::setOverlayDraw(std::function<void(QPainter &p, float scaleX, float scaleY)> func) {
    m_overlay = func;
}

void OrthogonalView::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_image.isNull()) {
        // Fit while keeping the physical aspect ratio, then apply user zoom.
        const DisplayRect r = computeDisplayRect(m_image.width(), m_image.height(),
                                                 m_pixelAspect, size(), m_userZoom, m_pan);
        const int w = std::max(1, static_cast<int>(std::lround(r.dispW)));
        const int h = std::max(1, static_cast<int>(std::lround(r.dispH)));
        QImage scaled = m_image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const float scaleX = float(scaled.width()) / float(m_image.width());
        const float scaleY = float(scaled.height()) / float(m_image.height());
        const int x = r.xoff;
        const int y = r.yoff;
        p.drawImage(QRect(x, y, scaled.width(), scaled.height()), scaled);
        p.translate(x, y);
        if (m_overlay) m_overlay(p, scaleX, scaleY);
        p.translate(-x, -y);
    }
}

// helper: map widget coords to image coords, returns false if outside image
static bool widgetToImage(const QImage &img, const QPoint &widgetPos, const QSize &widgetSize, float userZoom, const QPoint &pan, double pixelAspect, int &outX, int &outY) {
    if (img.isNull()) return false;
    const DisplayRect r = computeDisplayRect(img.width(), img.height(), pixelAspect, widgetSize, userZoom, pan);
    if (r.dispW <= 0.0 || r.dispH <= 0.0) return false;
    const double scaleX = r.dispW / static_cast<double>(img.width());
    const double scaleY = r.dispH / static_cast<double>(img.height());
    int xi = static_cast<int>((widgetPos.x() - r.xoff) / scaleX);
    int yi = static_cast<int>((widgetPos.y() - r.yoff) / scaleY);
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
    if (widgetToImage(m_image, event->pos(), size(), m_userZoom, m_pan, m_pixelAspect, xi, yi)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint globalPos = event->globalPosition().toPoint();
#else
        const QPoint globalPos = event->globalPos();
#endif
        if (event->button() == Qt::RightButton) {
            emit contextMenuRequested(xi, yi, globalPos);
            return;
        }
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
    if (widgetToImage(m_image, event->pos(), size(), m_userZoom, m_pan, m_pixelAspect, xi, yi)) {
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
    if (widgetToImage(m_image, event->pos(), size(), m_userZoom, m_pan, m_pixelAspect, xi, yi)) {
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
