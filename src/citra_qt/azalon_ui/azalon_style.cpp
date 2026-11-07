// Copyright 2026 Azalon3-DS Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <cmath>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>
#include "citra_qt/azalon_ui/azalon_style.h"
#include "citra_qt/uisettings.h"

namespace AzalonUI {

namespace {
QString g_original_style;

/// Lets QMenu popups have rounded corners (the stylesheet's border-radius) by giving them a
/// translucent window. Only touches menus first shown while the theme is active.
class PopupPolisher : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Polish && IsActive()) {
            if (auto* menu = qobject_cast<QMenu*>(watched);
                menu && !menu->property("azalon_rounded").toBool() && !menu->isVisible()) {
                menu->setProperty("azalon_rounded", true);
                menu->setAttribute(Qt::WA_TranslucentBackground);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
PopupPolisher* g_popup_polisher = nullptr;

QPalette DarkPalette() {
    using namespace Colors;
    QPalette p;
    p.setColor(QPalette::Window, Window);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, QColor(0x0f, 0x17, 0x19));
    p.setColor(QPalette::AlternateBase, QColor(0x11, 0x1b, 0x1e));
    p.setColor(QPalette::ToolTipBase, Raised);
    p.setColor(QPalette::ToolTipText, Text);
    p.setColor(QPalette::PlaceholderText, Muted);
    p.setColor(QPalette::Text, Text);
    p.setColor(QPalette::Button, Raised);
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, Hover);
    p.setColor(QPalette::Midlight, Border);
    p.setColor(QPalette::Mid, Raised);
    p.setColor(QPalette::Dark, QColor(0x07, 0x0c, 0x0d));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, Accent);
    p.setColor(QPalette::HighlightedText, OnAccent);
    p.setColor(QPalette::Link, AccentHover);
    p.setColor(QPalette::LinkVisited, Accent2);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, Accent);
#endif

    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, Disabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, Border);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, Muted);
    return p;
}

QPen LinePen(const QColor& color, qreal width) {
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

/// Draws `glyph` into a 24x24 logical box.
void DrawGlyph(QPainter& p, Glyph glyph, const QColor& color) {
    const QPen pen = LinePen(color, 1.8);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (glyph) {
    case Glyph::Gear: {
        p.save();
        p.translate(12, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = 0; i < 8; i++) {
            p.drawRoundedRect(QRectF(-1.6, -10, 3.2, 4.5), 1, 1);
            p.rotate(45);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(LinePen(color, 2.2));
        p.drawEllipse(QPointF(0, 0), 6.2, 6.2);
        p.setPen(LinePen(color, 1.8));
        p.drawEllipse(QPointF(0, 0), 2.4, 2.4);
        p.restore();
        break;
    }
    case Glyph::Monitor:
        p.drawRoundedRect(QRectF(3, 4, 18, 12.5), 2.5, 2.5);
        p.drawLine(QPointF(12, 16.5), QPointF(12, 20));
        p.drawLine(QPointF(8, 20.5), QPointF(16, 20.5));
        break;
    case Glyph::Speaker: {
        QPainterPath body;
        body.moveTo(4, 9.5);
        body.lineTo(8, 9.5);
        body.lineTo(12.5, 5.5);
        body.lineTo(12.5, 18.5);
        body.lineTo(8, 14.5);
        body.lineTo(4, 14.5);
        body.closeSubpath();
        p.drawPath(body);
        p.drawArc(QRectF(11, 8, 7, 8), -60 * 16, 120 * 16);
        p.drawArc(QRectF(11, 4.5, 11, 15), -60 * 16, 120 * 16);
        break;
    }
    case Glyph::Gamepad: {
        QPainterPath pad;
        pad.addRoundedRect(QRectF(2.5, 7, 19, 11), 5.5, 5.5);
        p.drawPath(pad);
        p.drawLine(QPointF(7.5, 10.2), QPointF(7.5, 14.8));
        p.drawLine(QPointF(5.2, 12.5), QPointF(9.8, 12.5));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(15.5, 11.2), 1.3, 1.3);
        p.drawEllipse(QPointF(17.8, 13.6), 1.3, 1.3);
        break;
    }
    case Glyph::Chip:
        p.drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
        p.drawRoundedRect(QRectF(9.5, 9.5, 5, 5), 1, 1);
        for (int i = 0; i < 3; i++) {
            const qreal o = 8.5 + i * 3.5;
            p.drawLine(QPointF(o, 3), QPointF(o, 6));
            p.drawLine(QPointF(o, 18), QPointF(o, 21));
            p.drawLine(QPointF(3, o), QPointF(6, o));
            p.drawLine(QPointF(18, o), QPointF(21, o));
        }
        break;
    case Glyph::Grid:
        p.drawRoundedRect(QRectF(4, 4, 7, 7), 2, 2);
        p.drawRoundedRect(QRectF(13, 4, 7, 7), 2, 2);
        p.drawRoundedRect(QRectF(4, 13, 7, 7), 2, 2);
        p.drawRoundedRect(QRectF(13, 13, 7, 7), 2, 2);
        break;
    case Glyph::List:
        for (int i = 0; i < 3; i++) {
            const qreal y = 6.5 + i * 5.5;
            p.drawLine(QPointF(9, y), QPointF(20, y));
            p.drawPoint(QPointF(4.8, y));
        }
        break;
    case Glyph::Folder: {
        QPainterPath path;
        path.moveTo(3, 7);
        path.quadTo(3, 5, 5, 5);
        path.lineTo(9.5, 5);
        path.lineTo(11.5, 7.5);
        path.lineTo(19, 7.5);
        path.quadTo(21, 7.5, 21, 9.5);
        path.lineTo(21, 17);
        path.quadTo(21, 19, 19, 19);
        path.lineTo(5, 19);
        path.quadTo(3, 19, 3, 17);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Glyph::Play: {
        QPainterPath tri;
        tri.moveTo(8, 5.5);
        tri.lineTo(19, 12);
        tri.lineTo(8, 18.5);
        tri.closeSubpath();
        p.setBrush(color);
        p.drawPath(tri);
        break;
    }
    case Glyph::Globe:
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.drawEllipse(QPointF(12, 12), 3.8, 8.5);
        p.drawLine(QPointF(3.5, 12), QPointF(20.5, 12));
        break;
    case Glyph::Search:
        p.drawEllipse(QPointF(10.5, 10.5), 6, 6);
        p.setPen(LinePen(color, 2.2));
        p.drawLine(QPointF(15, 15), QPointF(20, 20));
        break;
    case Glyph::Plus:
        p.setPen(LinePen(color, 2.2));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Glyph::Users:
        p.drawEllipse(QPointF(9, 8.5), 3.3, 3.3);
        p.drawArc(QRectF(3, 13, 12, 10), 0, 180 * 16);
        p.drawEllipse(QPointF(16.5, 9.5), 2.6, 2.6);
        p.drawArc(QRectF(13.5, 14, 8, 7), 0, 180 * 16);
        break;
    }
}

QPixmap RenderGlyph(Glyph glyph, const QColor& color, int size, qreal dpr) {
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    DrawGlyph(p, glyph, color);
    return pixmap;
}
} // namespace

bool IsActive() {
    return UISettings::values.theme == ThemeId;
}

void ApplyApplicationStyle(bool active) {
    if (g_original_style.isEmpty() && QApplication::style()) {
        g_original_style = QApplication::style()->name();
    }

    if (active) {
        if (!g_popup_polisher) {
            g_popup_polisher = new PopupPolisher(qApp);
            qApp->installEventFilter(g_popup_polisher);
        }
        if (QApplication::style()->name().compare(QStringLiteral("fusion"), Qt::CaseInsensitive)) {
            QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        }
        QApplication::setPalette(DarkPalette());
        return;
    }

    if (!g_original_style.isEmpty() &&
        QApplication::style()->name().compare(g_original_style, Qt::CaseInsensitive)) {
        QApplication::setStyle(QStyleFactory::create(g_original_style));
    }
    QApplication::setPalette(QApplication::style()->standardPalette());
}

QIcon MakeIcon(Glyph glyph, const QColor& color, int size) {
    QIcon icon;
    for (const qreal dpr : {1.0, 1.5, 2.0, 3.0}) {
        icon.addPixmap(RenderGlyph(glyph, color, size, dpr), QIcon::Normal);
        QColor disabled = color;
        disabled.setAlphaF(0.35);
        icon.addPixmap(RenderGlyph(glyph, disabled, size, dpr), QIcon::Disabled);
    }
    return icon;
}

void FadeIn(QWidget* widget, int duration_ms) {
    if (!widget || widget->testAttribute(Qt::WA_NativeWindow)) {
        return;
    }
    if (!IsActive()) {
        return;
    }

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (!effect) {
        if (widget->graphicsEffect()) {
            return; // Someone else's effect, leave it alone.
        }
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }

    auto* animation = new QPropertyAnimation(effect, "opacity", effect);
    animation->setDuration(duration_ms);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QPropertyAnimation::finished, widget, [widget, effect] {
        // Drop the effect afterwards: it forces offscreen rendering of the whole subtree.
        if (widget->graphicsEffect() == effect) {
            widget->setGraphicsEffect(nullptr);
        }
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace AzalonUI
