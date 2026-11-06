// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QImage>
#include <QRect>
#include <QWidget>

namespace DS {

class Session;

/// Shows the two DS screens of a Session, forwards the keyboard to Azahar's input
/// system and turns mouse/touch on the bottom screen into DS touch input.
class ScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit ScreenWidget(Session& session, QWidget* parent = nullptr);

signals:
    void MouseActivity();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void OnFrameReady();
    void UpdateLayout();
    void HandleTouch(const QPoint& pos, bool pressed);

    Session& session;
    QImage frame;
    QRect top_rect;
    QRect bottom_rect;
    bool touching = false;
};

} // namespace DS
