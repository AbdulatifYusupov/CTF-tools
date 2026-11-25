#include "CompareWidget.hpp"

#include <QColor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace app {

CompareWidget::CompareWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(240, 180);
}

void CompareWidget::setImages(const QImage& left, const QImage& right, const QString& leftLabel,
                              const QString& rightLabel) {
    const bool sameSize = !left_.isNull() && left_.size() == left.size();
    left_       = left;
    right_      = right;
    leftLabel_  = leftLabel;
    rightLabel_ = rightLabel;
    if (!sameSize || !userView_) fit();
    update();
}

void CompareWidget::clear() {
    left_ = right_ = QImage();
    update();
}

void CompareWidget::resetView() {
    userView_ = false;
    fit();
    update();
}

void CompareWidget::fit() {
    if (left_.isNull() || width() <= 0 || height() <= 0) return;
    scale_ = std::min(static_cast<double>(width()) / left_.width(), static_cast<double>(height()) / left_.height());
    offset_ = QPointF((width() - left_.width() * scale_) / 2.0, (height() - left_.height() * scale_) / 2.0);
}

void CompareWidget::resizeEvent(QResizeEvent*) {
    if (!userView_) fit();
}

void CompareWidget::zoomAt(const QPointF& pos, double factor) {
    if (left_.isNull()) return;
    const double fitScale = std::min(static_cast<double>(width()) / left_.width(),
                                     static_cast<double>(height()) / left_.height());
    const double ns = std::clamp(scale_ * factor, fitScale * 0.25, 64.0);
    const double k  = ns / scale_;
    offset_   = pos - (pos - offset_) * k;
    scale_    = ns;
    userView_ = true;
    update();
}

void CompareWidget::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y();
    if (dy == 0) return;
    zoomAt(e->position(), dy > 0 ? 1.25 : 0.8);
    e->accept();
}

void CompareWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    drag_ = nearSplit(e->position().x()) ? Drag::Split : Drag::Pan;
    last_ = e->position().toPoint();
}

void CompareWidget::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ == Drag::Split) {
        split_ = std::clamp(e->position().x() / std::max(1, width()), 0.0, 1.0);
        update();
    } else if (drag_ == Drag::Pan) {
        const QPoint now = e->position().toPoint();
        offset_ += QPointF(now - last_);
        last_     = now;
        userView_ = true;
        update();
    } else {
        setCursor(nearSplit(e->position().x()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
}

void CompareWidget::mouseReleaseEvent(QMouseEvent*) { drag_ = Drag::None; }

void CompareWidget::mouseDoubleClickEvent(QMouseEvent*) { resetView(); }

void CompareWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    if (left_.isNull()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Open a cover image"));
        return;
    }

    const QRectF target(offset_, QSizeF(left_.width() * scale_, left_.height() * scale_));
    p.setRenderHint(QPainter::SmoothPixmapTransform, scale_ < 1.0);  // exact pixels when zoomed in

    if (right_.isNull()) {
        p.drawImage(target, left_);
    } else {
        const double sx = splitX();
        p.save();
        p.setClipRect(QRectF(0, 0, sx, height()));
        p.drawImage(target, left_);
        p.restore();
        p.save();
        p.setClipRect(QRectF(sx, 0, width() - sx, height()));
        p.drawImage(target, right_);
        p.restore();

        p.setPen(QPen(Qt::white, 1.0));
        p.drawLine(QPointF(sx, 0), QPointF(sx, height()));
        p.setBrush(QColor(255, 255, 255, 200));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(sx, height() / 2.0), 7, 7);
    }

    auto label = [&](const QString& text, bool leftSide) {
        if (text.isEmpty()) return;
        const QFontMetrics fm(font());
        const QRect box(0, 0, fm.horizontalAdvance(text) + 12, fm.height() + 6);
        const QRect at = leftSide ? box.translated(6, 6) : box.translated(width() - box.width() - 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(at, 4, 4);
        p.setPen(Qt::white);
        p.drawText(at, Qt::AlignCenter, text);
    };
    label(leftLabel_, true);
    if (!right_.isNull()) label(rightLabel_, false);
}

}  // namespace app
