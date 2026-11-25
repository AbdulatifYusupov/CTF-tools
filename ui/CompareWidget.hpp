#pragma once
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <cmath>

namespace app {

// Two images of the same size with a draggable vertical curtain: left of it shows `left`, right of
// it shows `right`. Mouse wheel zooms around the cursor, dragging pans, dragging the white line
// moves the curtain, double click re-fits. With only one image it is a plain zoomable viewer.
class CompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompareWidget(QWidget* parent = nullptr);

    // `right` may be null (single image mode).
    void setImages(const QImage& left, const QImage& right, const QString& leftLabel = {},
                   const QString& rightLabel = {});
    void clear();
    void resetView();

    QSize sizeHint() const override { return {640, 480}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    enum class Drag { None, Split, Pan };

    double splitX() const { return width() * split_; }
    bool nearSplit(double x) const { return !right_.isNull() && std::abs(x - splitX()) < 8.0; }
    void fit();
    void zoomAt(const QPointF& pos, double factor);

    QImage left_, right_;
    QString leftLabel_, rightLabel_;
    double scale_ = 1.0;
    QPointF offset_;  // top-left of the image in widget coordinates
    double split_ = 0.5;
    bool userView_ = false;  // true after zoom/pan: resize no longer re-fits
    Drag drag_ = Drag::None;
    QPoint last_;
};

}  // namespace app
