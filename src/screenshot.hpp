#pragma once
// A picture of the screen through the Screenshot portal, without asking
// (interactive off): what was on screen when a quick note was taken.

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

namespace tala {

class Screenshot : public QObject {
    Q_OBJECT

public:
    using Done = std::function<void(const QString& path)>;  // "" when there's none

    explicit Screenshot(QObject* parent = nullptr);
    void take(Done done);

private slots:
    void onResponse(uint code, const QVariantMap& results);

private:
    Done done_;
    int tokens_ = 0;
};

} // namespace tala
