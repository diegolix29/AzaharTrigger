// Copyright 2026 Azalon3-DS Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

/// Look and feel of the Azalon Dark theme (dark glass + teal/green accent).
/// The stylesheet lives in dist/qt_themes/azalon/style.qss; this adds what QSS can't do:
/// the Fusion style + palette, painted vector icons and small animations.
namespace AzalonUI {

/// Theme id as stored in UISettings::values.theme.
inline const QString ThemeId = QStringLiteral("azalon");

namespace Colors {
inline const QColor Window{0x0b, 0x11, 0x13};
inline const QColor Surface{0x11, 0x19, 0x1c};
inline const QColor Raised{0x17, 0x22, 0x25};
inline const QColor Hover{0x1d, 0x2b, 0x2f};
inline const QColor Border{0x22, 0x31, 0x2f};
inline const QColor Text{0xe6, 0xf1, 0xef};
inline const QColor Muted{0x8a, 0xa1, 0x9c};
inline const QColor Disabled{0x50, 0x60, 0x5d};
inline const QColor Accent{0x14, 0xb8, 0xa6};
inline const QColor Accent2{0x22, 0xc5, 0x5e};
inline const QColor AccentHover{0x2d, 0xd4, 0xbf};
inline const QColor OnAccent{0x04, 0x11, 0x0f};
/// Badge colors per platform.
inline const QColor Badge3DS{0x14, 0xb8, 0xa6};
inline const QColor BadgeDS{0x60, 0xa5, 0xfa};
inline const QColor BadgeDSi{0xc0, 0x84, 0xfc};
} // namespace Colors

/// True when the Azalon theme is the selected UI theme.
bool IsActive();

/// Switches the application style: Fusion + dark palette when `active`, otherwise restores the
/// style the application started with. Call before applying the stylesheet.
void ApplyApplicationStyle(bool active);

enum class Glyph {
    Gear,
    Monitor,
    Speaker,
    Gamepad,
    Chip,
    Grid,
    List,
    Folder,
    Play,
    Globe,
    Search,
    Plus,
    Users,
};

/// Simple line icon drawn with QPainter (crisp at any DPI, follows the accent color).
QIcon MakeIcon(Glyph glyph, const QColor& color = Colors::Text, int size = 20);

/// Fades `widget` in from transparent. Safe to call repeatedly; does nothing for widgets that
/// host native (OpenGL/Vulkan) children.
void FadeIn(QWidget* widget, int duration_ms = 220);

} // namespace AzalonUI
