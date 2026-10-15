#pragma once
// Small window helpers the QML can't do itself.

#include <QObject>
#include <QRegion>
#include <QVariantList>
#include <QWindow>

namespace tala {

class Ui : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Only these rectangles take pointer input; the rest of the window lets
    // clicks through to what's beneath it.
    Q_INVOKABLE void setMask(QWindow* window, const QVariantList& rects) {
        if (!window)
            return;
        QRegion region;
        for (const QVariant& v : rects)
            region += v.toRectF().toAlignedRect();
        window->setMask(region);
    }
};

} // namespace tala
