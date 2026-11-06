// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <azalon_ds/azalon_ds.h>

#include "citra_qt/ds/ds_config.h"
#include "citra_qt/ds/ds_screen_widget.h"
#include "citra_qt/ds/ds_session.h"
#include "input_common/keyboard.h"
#include "input_common/main.h"

namespace DS {

namespace {
constexpr int kW = AZDS_SCREEN_WIDTH;
constexpr int kH = AZDS_SCREEN_HEIGHT;
} // namespace

ScreenWidget::ScreenWidget(Session& session_, QWidget* parent)
    : QWidget(parent), session(session_) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(kW, kH);
    frame = session.CurrentFrame();
    connect(&session, &Session::FrameReady, this, &ScreenWidget::OnFrameReady,
            Qt::QueuedConnection);
    UpdateLayout();
}

void ScreenWidget::OnFrameReady() {
    frame = session.CurrentFrame();
    update();
}

void ScreenWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateLayout();
}

void ScreenWidget::UpdateLayout() {
    const Config& cfg = Config::Get();
    const int w = width(), h = height();

    // Size of the arrangement in DS pixels.
    int units_w = kW, units_h = kH * 2;
    switch (cfg.layout) {
    case ScreenLayout::Horizontal:
        units_w = kW * 2;
        units_h = kH;
        break;
    case ScreenLayout::TopOnly:
    case ScreenLayout::BottomOnly:
        units_w = kW;
        units_h = kH;
        break;
    default:
        break;
    }

    double scale = std::min(static_cast<double>(w) / units_w, static_cast<double>(h) / units_h);
    if (cfg.integer_scale && scale >= 1.0)
        scale = std::floor(scale);
    const int sw = static_cast<int>(kW * scale), sh = static_cast<int>(kH * scale);
    const int total_w = static_cast<int>(units_w * scale), total_h = static_cast<int>(units_h * scale);
    const int x0 = (w - total_w) / 2, y0 = (h - total_h) / 2;

    QRect first(x0, y0, sw, sh);
    QRect second = cfg.layout == ScreenLayout::Horizontal ? QRect(x0 + sw, y0, sw, sh)
                                                          : QRect(x0, y0 + sh, sw, sh);
    switch (cfg.layout) {
    case ScreenLayout::TopOnly:
        top_rect = first;
        bottom_rect = QRect();
        break;
    case ScreenLayout::BottomOnly:
        top_rect = QRect();
        bottom_rect = first;
        break;
    default:
        top_rect = cfg.swap_screens ? second : first;
        bottom_rect = cfg.swap_screens ? first : second;
        break;
    }
    update();
}

void ScreenWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, Config::Get().smooth);
    if (!top_rect.isNull())
        painter.drawImage(top_rect, frame, QRect(0, 0, kW, kH));
    if (!bottom_rect.isNull())
        painter.drawImage(bottom_rect, frame, QRect(0, kH, kW, kH));
}

void ScreenWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Tab) {
        session.SetFastForward(true); // hold Tab to fast-forward
        return;
    }
    if (!event->isAutoRepeat())
        InputCommon::GetKeyboard()->PressKey(event->key());
}

void ScreenWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Tab) {
        session.SetFastForward(false);
        return;
    }
    if (!event->isAutoRepeat())
        InputCommon::GetKeyboard()->ReleaseKey(event->key());
}

void ScreenWidget::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    InputCommon::GetKeyboard()->ReleaseAllKeys();
    session.SetFastForward(false);
    if (touching) {
        touching = false;
        session.SetTouch(false, 0, 0);
    }
}

void ScreenWidget::HandleTouch(const QPoint& pos, bool pressed) {
    if (bottom_rect.isNull() || bottom_rect.width() == 0) {
        return;
    }
    if (pressed && !touching && !bottom_rect.contains(pos)) {
        return; // a new touch must start on the bottom screen
    }
    const int x = (pos.x() - bottom_rect.x()) * kW / bottom_rect.width();
    const int y = (pos.y() - bottom_rect.y()) * kH / bottom_rect.height();
    touching = pressed;
    session.SetTouch(pressed, x, y);
}

void ScreenWidget::mousePressEvent(QMouseEvent* event) {
    emit MouseActivity();
    if (event->button() == Qt::LeftButton)
        HandleTouch(event->position().toPoint(), true);
}

void ScreenWidget::mouseMoveEvent(QMouseEvent* event) {
    emit MouseActivity();
    if (touching)
        HandleTouch(event->position().toPoint(), true);
}

void ScreenWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && touching)
        HandleTouch(event->position().toPoint(), false);
}

void ScreenWidget::contextMenuEvent(QContextMenuEvent* event) {
    Config& cfg = Config::Get();
    QMenu menu(this);

    auto* layouts = new QActionGroup(&menu);
    const std::pair<ScreenLayout, QString> options[] = {
        {ScreenLayout::Vertical, tr("Vertical")},
        {ScreenLayout::Horizontal, tr("Side by side")},
        {ScreenLayout::TopOnly, tr("Top screen only")},
        {ScreenLayout::BottomOnly, tr("Bottom screen only")},
    };
    for (const auto& [layout, name] : options) {
        QAction* action = menu.addAction(name);
        action->setCheckable(true);
        action->setChecked(cfg.layout == layout);
        layouts->addAction(action);
        connect(action, &QAction::triggered, this, [this, layout] {
            Config::Get().layout = layout;
            Config::Get().Save();
            UpdateLayout();
        });
    }
    menu.addSeparator();
    QAction* swap = menu.addAction(tr("Swap screens"));
    swap->setCheckable(true);
    swap->setChecked(cfg.swap_screens);
    connect(swap, &QAction::toggled, this, [this](bool on) {
        Config::Get().swap_screens = on;
        Config::Get().Save();
        UpdateLayout();
    });
    QAction* integer = menu.addAction(tr("Integer scaling"));
    integer->setCheckable(true);
    integer->setChecked(cfg.integer_scale);
    connect(integer, &QAction::toggled, this, [this](bool on) {
        Config::Get().integer_scale = on;
        Config::Get().Save();
        UpdateLayout();
    });
    QAction* smooth = menu.addAction(tr("Smooth filtering"));
    smooth->setCheckable(true);
    smooth->setChecked(cfg.smooth);
    connect(smooth, &QAction::toggled, this, [this](bool on) {
        Config::Get().smooth = on;
        Config::Get().Save();
        update();
    });
    menu.exec(event->globalPos());
}

} // namespace DS
