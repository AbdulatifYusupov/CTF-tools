#pragma once
#include <QImage>

#include <opencv2/core.hpp>

namespace app {

// 8-bit gray / BGR / BGRA cv::Mat -> deep-copied QImage (null QImage for anything else).
inline QImage toQImage(const cv::Mat& m) {
    if (m.empty() || m.depth() != CV_8U) return {};
    QImage::Format fmt;
    switch (m.channels()) {
        case 1: fmt = QImage::Format_Grayscale8; break;
        case 3: fmt = QImage::Format_BGR888; break;
        case 4: fmt = QImage::Format_ARGB32; break;  // B,G,R,A byte order on little-endian
        default: return {};
    }
    const QImage view(m.data, m.cols, m.rows, static_cast<qsizetype>(m.step[0]), fmt);
    return view.copy();  // the Mat may go away
}

}  // namespace app
