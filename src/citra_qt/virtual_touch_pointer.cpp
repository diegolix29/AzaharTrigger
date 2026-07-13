// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QWidget>
#include "citra_qt/bootmanager.h"
#include "citra_qt/virtual_touch_pointer.h"
#include "common/settings.h"
#include "core/frontend/framebuffer_layout.h"

namespace {

/// A small click-through circle drawn over the render window so the user can see where the
/// virtual pointer currently is. Purely cosmetic: it never receives input and has no effect on
/// the emulated touch screen.
class VirtualCursorWidget : public QWidget {
public:
    explicit VirtualCursorWidget(QWidget* parent_) : QWidget(parent_) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground); // Keeps the corners of the square transparent
        setFixedSize(24, 24);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        // Removed Antialiasing and transparency (alpha 255 instead of 150) to prevent fuzziness
        painter.setPen(
            QPen(QColor(170, 170, 170, 255), 2));     // Slightly darker light grey for outline
        painter.setBrush(QColor(211, 211, 211, 255)); // Solid light grey fill

        // Draw a solid circle
        painter.drawEllipse(rect().adjusted(4, 4, -4, -4));
    }
};

} // namespace

VirtualTouchPointer::VirtualTouchPointer(GRenderWindow* primary_window_,
                                         GRenderWindow* secondary_window_, QObject* parent)
    : QObject(parent), primary_window(primary_window_), secondary_window(secondary_window_) {
    cursor_widget =
        new VirtualCursorWidget(nullptr); // No parent - top-level window for free movement
    cursor_widget->hide();                // Hide initially, only show when active

    timer = new QTimer(this);
    timer->setInterval(16); // roughly 60 Hz
    connect(timer, &QTimer::timeout, this, &VirtualTouchPointer::Tick);
    timer->start();
}

VirtualTouchPointer::~VirtualTouchPointer() {
    if (touching) {
        GetTouchScreenWindow()->TouchReleased();
    }
}

void VirtualTouchPointer::ToggleTouchMode() {
    Settings::values.cstick_touch_mode = !Settings::values.cstick_touch_mode;
    if (!Settings::values.cstick_touch_mode) {
        tap_toggled = false;
    }
}

void VirtualTouchPointer::ToggleTap() {
    if (!Settings::values.cstick_touch_mode) {
        return;
    }
    tap_toggled = !tap_toggled;
}

namespace {
// How many ~16ms ticks a quick tap stays pressed for. Long enough for the 3DS UI to reliably
// register it as a tap, short enough to feel instant.
constexpr int kQuickTapFrames = 5;
} // namespace

void VirtualTouchPointer::QuickTap() {
    if (!Settings::values.cstick_touch_mode) {
        return;
    }
    if (tap_toggled) {
        // Already being held down via the drag toggle; a quick tap on top of that would just
        // interrupt the drag, so ignore it.
        return;
    }
    quick_tap_frames_remaining = kQuickTapFrames;
}

void VirtualTouchPointer::EnsureDevice() {
    if (!stick_device) {
        stick_device = Input::CreateDevice<Input::AnalogDevice>(
            Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CStick]);
    }
}

GRenderWindow* VirtualTouchPointer::GetTouchScreenWindow() {
    // In separate windows mode, the bottom screen (touch screen) location depends on swap_screen
    // setting:
    // - If swap_screen is false: bottom screen is in secondary window
    // - If swap_screen is true: bottom screen is in primary window
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        return Settings::values.swap_screen.GetValue() ? primary_window : secondary_window;
    }
    // In non-separated windows mode, both screens are in the primary window
    return primary_window;
}

void VirtualTouchPointer::UpdateCursorWidget(bool visible) {
    if (!cursor_widget) {
        return;
    }

    if (visible) {
        cursor_widget->show();
    } else {
        cursor_widget->hide();
        return;
    }

    GRenderWindow* touch_window = GetTouchScreenWindow();
    const qreal ratio = touch_window->windowPixelRatio();
    if (ratio <= 0.0) {
        return;
    }
    const int wx = static_cast<int>(pointer_x / ratio) - cursor_widget->width() / 2;
    const int wy = static_cast<int>(pointer_y / ratio) - cursor_widget->height() / 2;

    // Calculate position relative to the touch window, then convert to global screen coordinates
    const QPoint window_pos = touch_window->mapToGlobal(QPoint(wx, wy));
    cursor_widget->move(window_pos);
    cursor_widget->raise();
}

void VirtualTouchPointer::Tick() {
    const bool active = Settings::values.cstick_touch_mode;

    if (!active) {
        if (was_active) {
            if (touching) {
                GetTouchScreenWindow()->TouchReleased();
                touching = false;
            }
            UpdateCursorWidget(false);
            initialized_position = false;
            tap_toggled = false;
            quick_tap_frames_remaining = 0;
            was_active = false;
        }
        return;
    }
    was_active = true;

    GRenderWindow* touch_window = GetTouchScreenWindow();
    if (!touch_window->isVisible()) {
        return;
    }

    const auto& layout = touch_window->GetFramebufferLayout();
    // Use the enabled screen (top or bottom) for separated windows mode
    const auto& active_screen =
        layout.bottom_screen_enabled ? layout.bottom_screen : layout.top_screen;
    if (active_screen.GetWidth() == 0 || active_screen.GetHeight() == 0) {
        return;
    }

    if (!initialized_position) {
        pointer_x = (active_screen.left + active_screen.right) / 2.0f;
        pointer_y = (active_screen.top + active_screen.bottom) / 2.0f;
        initialized_position = true;
    }

    EnsureDevice();
    const auto [ax, ay] = stick_device->GetStatus();

    // Apply deadzone to prevent drift
    constexpr float deadzone = 0.15f;
    const float ax_deadzone = std::abs(ax) < deadzone ? 0.0f : ax;
    const float ay_deadzone = std::abs(ay) < deadzone ? 0.0f : ay;

    // Adjusted pacing: Changed from 30.0f to 20.0f for faster movement.
    const float speed_x = static_cast<float>(active_screen.GetWidth()) / 20.0f;
    const float speed_y = static_cast<float>(active_screen.GetHeight()) / 20.0f;

    pointer_x += ax_deadzone * speed_x;
    pointer_y -= ay_deadzone * speed_y; // pushing the stick up moves the pointer up

    pointer_x = std::clamp(pointer_x, static_cast<float>(active_screen.left),
                           static_cast<float>(active_screen.right - 1));
    pointer_y = std::clamp(pointer_y, static_cast<float>(active_screen.top),
                           static_cast<float>(active_screen.bottom - 1));

    UpdateCursorWidget(true);

    const auto fx = static_cast<unsigned>(pointer_x);
    const auto fy = static_cast<unsigned>(pointer_y);

    const bool should_be_pressed = tap_toggled || quick_tap_frames_remaining > 0;

    if (should_be_pressed) {
        if (!touching) {
            touch_window->TouchPressed(fx, fy);
            touching = true;
        } else {
            touch_window->TouchMoved(fx, fy);
        }

        if (quick_tap_frames_remaining > 0) {
            --quick_tap_frames_remaining;
            if (quick_tap_frames_remaining == 0 && !tap_toggled) {
                touch_window->TouchReleased();
                touching = false;
            }
        }
    } else if (touching) {
        touch_window->TouchReleased();
        touching = false;
    }
}