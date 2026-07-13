// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QObject>
#include "core/frontend/input.h"

class QTimer;
class QWidget;
class GRenderWindow;

class VirtualTouchPointer : public QObject {
    Q_OBJECT

public:
    explicit VirtualTouchPointer(GRenderWindow* primary_window_, GRenderWindow* secondary_window_,
                                 QObject* parent = nullptr);
    ~VirtualTouchPointer() override;

    void ToggleTouchMode();

    void ToggleTap();

    void QuickTap();

private:
    void Tick();
    void EnsureDevice();
    void UpdateCursorWidget(bool visible);
    GRenderWindow* GetTouchScreenWindow();

    GRenderWindow* primary_window;
    GRenderWindow* secondary_window;
    QTimer* timer;
    QWidget* cursor_widget = nullptr;

    std::unique_ptr<Input::AnalogDevice> stick_device;

    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    bool initialized_position = false;
    bool tap_toggled = false;
    int quick_tap_frames_remaining = 0;
    bool touching = false;
    bool was_active = false;
};
