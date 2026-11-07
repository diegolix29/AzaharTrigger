// Copyright 2026 Azalon3-DS Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOption>
#include <QTextLayout>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <fmt/format.h>
#include "citra_qt/azalon_ui/azalon_style.h"
#include "citra_qt/azalon_ui/game_grid.h"
#include "citra_qt/game_list.h"
#include "citra_qt/game_list_p.h"
#include "citra_qt/uisettings.h"

namespace AzalonUI {

namespace {
constexpr int CellWidth = 184;
constexpr int CellHeight = 240;
constexpr qreal CardWidth = 164;
constexpr qreal CardHeight = 222;
constexpr qreal CardRadius = 16;
constexpr qreal IconSize = 96;
constexpr int HoverMs = 160;
constexpr int AppearMs = 320;
constexpr int AppearStaggerMs = 22;
constexpr int AppearStaggerMax = 40;

constexpr u64 DSProgramIdTag = 0x4E4453ull;
constexpr qreal Pi = 3.14159265358979323846;

qreal EaseOutCubic(qreal t) {
    const qreal f = 1.0 - t;
    return 1.0 - f * f * f;
}

QColor Mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t),
                            static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QColor WithAlpha(QColor color, qreal alpha) {
    color.setAlphaF(static_cast<float>(alpha));
    return color;
}

QColor BadgeColor(Platform platform) {
    switch (platform) {
    case Platform::N3DS:
        return Colors::Badge3DS;
    case Platform::DS:
        return Colors::BadgeDS;
    case Platform::DSi:
        return Colors::BadgeDSi;
    }
    return Colors::Badge3DS;
}

/// Up to two lines, the second one elided.
QStringList WrapTitle(const QString& title, const QFont& font, qreal width) {
    QStringList lines;
    const QFontMetricsF metrics(font);
    QTextLayout layout(title, font);
    layout.beginLayout();
    for (int i = 0; i < 2; i++) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(width);
        if (i == 1) {
            lines << metrics.elidedText(title.mid(line.textStart()).simplified(), Qt::ElideRight,
                                        width);
        } else {
            lines << title.mid(line.textStart(), line.textLength()).trimmed();
        }
    }
    layout.endLayout();
    return lines;
}
} // namespace

Platform PlatformOf(u64 program_id, const QString& file_type) {
    if ((program_id >> 40) != DSProgramIdTag) {
        return Platform::N3DS;
    }
    return file_type.startsWith(QStringLiteral("DSi")) ? Platform::DSi : Platform::DS;
}

bool MatchesPlatform(PlatformFilter filter, Platform platform) {
    switch (filter) {
    case PlatformFilter::All:
        return true;
    case PlatformFilter::N3DS:
        return platform == Platform::N3DS;
    case PlatformFilter::DS:
        return platform == Platform::DS || platform == Platform::DSi;
    }
    return true;
}

QString PlatformName(Platform platform) {
    switch (platform) {
    case Platform::N3DS:
        return QStringLiteral("3DS");
    case Platform::DS:
        return QStringLiteral("DS");
    case Platform::DSi:
        return QStringLiteral("DSi");
    }
    return {};
}

// ---------------------------------------------------------------------------- GameGridModel

GameGridModel::GameGridModel(QStandardItemModel* source_, QObject* parent)
    : QAbstractListModel(parent), source{source_} {
    rebuild_timer.setSingleShot(true);
    rebuild_timer.setInterval(60);
    connect(&rebuild_timer, &QTimer::timeout, this, &GameGridModel::Rebuild);

    connect(source, &QAbstractItemModel::rowsInserted, this, &GameGridModel::ScheduleRebuild);
    connect(source, &QAbstractItemModel::rowsRemoved, this, &GameGridModel::ScheduleRebuild);
    connect(source, &QAbstractItemModel::modelReset, this, &GameGridModel::ScheduleRebuild);
    connect(source, &QAbstractItemModel::layoutChanged, this, &GameGridModel::ScheduleRebuild);
    connect(source, &QAbstractItemModel::dataChanged, this, &GameGridModel::ScheduleRebuild);
}

int GameGridModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries.size());
}

QModelIndex GameGridModel::SourceIndex(const QModelIndex& index) const {
    if (!index.isValid() || index.row() >= static_cast<int>(entries.size())) {
        return {};
    }
    return entries[static_cast<std::size_t>(index.row())].index;
}

QVariant GameGridModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(entries.size())) {
        return {};
    }
    const Entry& entry = entries[static_cast<std::size_t>(index.row())];
    const QModelIndex src = entry.index;
    if (!src.isValid()) {
        return {};
    }

    switch (role) {
    case PlatformRole:
        return static_cast<int>(entry.platform);
    case PathRole:
        return entry.path;
    case TitleTextRole:
    case Qt::DisplayRole:
    case Qt::ToolTipRole: {
        QString title = src.data(GameListItemPath::LongTitleRole).toString();
        if (title.isEmpty()) {
            title = src.data(GameListItemPath::TitleRole).toString();
        }
        if (title.isEmpty()) {
            title = QFileInfo(entry.path).completeBaseName();
        }
        return title;
    }
    case SubtitleRole: {
        const QString play_time =
            src.sibling(src.row(), GameList::COLUMN_PLAY_TIME).data(Qt::DisplayRole).toString();
        if (!play_time.isEmpty()) {
            return play_time;
        }
        return src.sibling(src.row(), GameList::COLUMN_REGION)
            .data(Qt::DisplayRole)
            .toString()
            .section(QLatin1Char('\n'), 0, 0);
    }
    case FavoriteRole:
        return UISettings::values.favorited_ids.contains(
            src.data(GameListItemPath::ProgramIdRole).toULongLong());
    case SearchTextRole: {
        const QString file_path = entry.path.toLower();
        return QString(file_path.mid(file_path.lastIndexOf(QLatin1Char{'/'}) + 1) +
                       QLatin1Char{' '} +
                       src.data(GameListItemPath::LongTitleRole).toString().toLower());
    }
    case ProgramIdTextRole:
        return QString::fromStdString(
                   fmt::format("{:016X}", src.data(GameListItemPath::ProgramIdRole).toULongLong()))
            .toLower();
    case IconRole: {
        const QVariant large = src.data(GameListItemPath::LargeIconRole);
        if (large.isValid() && !large.value<QPixmap>().isNull()) {
            return large;
        }
        return src.data(Qt::DecorationRole);
    }
    default:
        return {};
    }
}

void GameGridModel::ScheduleRebuild() {
    // Throttle, don't debounce: covers keep appearing while a folder is being scanned.
    if (!rebuild_timer.isActive()) {
        rebuild_timer.start();
    }
}

void GameGridModel::RefreshData() {
    if (!entries.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(entries.size()) - 1));
    }
}

void GameGridModel::Rebuild() {
    // Collect the games currently in the tree (folders -> games, favorites excluded).
    std::vector<Entry> fresh;
    QHash<QString, std::size_t> fresh_by_path;
    const int top_count = source->rowCount();
    for (int i = 0; i < top_count; i++) {
        const QStandardItem* folder = source->item(i, 0);
        if (!folder) {
            continue;
        }
        const auto folder_type = static_cast<GameListItemType>(folder->type());
        if (folder_type != GameListItemType::CustomDir &&
            folder_type != GameListItemType::InstalledDir &&
            folder_type != GameListItemType::SystemDir) {
            continue;
        }
        for (int j = 0; j < folder->rowCount(); j++) {
            const QStandardItem* game = folder->child(j, 0);
            if (!game || game->type() != static_cast<int>(GameListItemType::Game)) {
                continue;
            }
            const QString path = game->data(GameListItemPath::FullPathRole).toString();
            if (path.isEmpty() || fresh_by_path.contains(path)) {
                continue;
            }
            const QStandardItem* type_item = folder->child(j, GameList::COLUMN_FILE_TYPE);
            const QString file_type = type_item ? type_item->text() : QString{};
            fresh_by_path.insert(path, fresh.size());
            fresh.push_back({QPersistentModelIndex(game->index()),
                             PlatformOf(game->data(GameListItemPath::ProgramIdRole).toULongLong(),
                                        file_type),
                             path});
        }
    }

    // Many removals (folder refresh): just reset.
    std::size_t removed = 0;
    for (const Entry& entry : entries) {
        if (!fresh_by_path.contains(entry.path)) {
            removed++;
        }
    }
    if (removed > 64u) {
        beginResetModel();
        entries = std::move(fresh);
        endResetModel();
        return;
    }

    // Otherwise remove / append in place so the view keeps its scroll position.
    for (int i = static_cast<int>(entries.size()) - 1; i >= 0; i--) {
        const auto it = fresh_by_path.constFind(entries[static_cast<std::size_t>(i)].path);
        if (it == fresh_by_path.constEnd()) {
            beginRemoveRows({}, i, i);
            entries.erase(entries.begin() + i);
            endRemoveRows();
        } else {
            entries[static_cast<std::size_t>(i)] = fresh[it.value()];
        }
    }

    QSet<QString> known;
    for (const Entry& entry : entries) {
        known.insert(entry.path);
    }
    std::vector<Entry> added;
    for (Entry& entry : fresh) {
        if (!known.contains(entry.path)) {
            added.push_back(std::move(entry));
        }
    }
    if (!added.empty()) {
        const int first = static_cast<int>(entries.size());
        beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
        for (Entry& entry : added) {
            entries.push_back(std::move(entry));
        }
        endInsertRows();
    }

    RefreshData();
}

// ---------------------------------------------------------------------------- GameGridFilter

void GameGridFilter::SetFilter(PlatformFilter platform_, const QString& text_) {
    if (platform == platform_ && text == text_) {
        return;
    }
    platform = platform_;
    text = text_;
    invalidateFilter();
}

bool GameGridFilter::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
    const QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
    const auto row_platform =
        static_cast<Platform>(idx.data(GameGridModel::PlatformRole).toInt());
    if (!MatchesPlatform(platform, row_platform)) {
        return false;
    }
    if (text.isEmpty()) {
        return true;
    }

    const QString haystack = idx.data(GameGridModel::SearchTextRole).toString();
    const QStringList words = text.split(QLatin1Char{' '}, Qt::SkipEmptyParts);
    const bool all_words = std::all_of(words.begin(), words.end(), [&haystack](const QString& w) {
        return haystack.contains(w);
    });
    if (all_words) {
        return true;
    }
    const QString program_id = idx.data(GameGridModel::ProgramIdTextRole).toString();
    return program_id.length() == 16 && text.contains(program_id);
}

bool GameGridFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const bool left_fav = left.data(GameGridModel::FavoriteRole).toBool();
    const bool right_fav = right.data(GameGridModel::FavoriteRole).toBool();
    if (left_fav != right_fav) {
        return left_fav;
    }
    return QString::localeAwareCompare(left.data(GameGridModel::TitleTextRole).toString(),
                                       right.data(GameGridModel::TitleTextRole).toString()) < 0;
}

// ---------------------------------------------------------------------------- delegate

GameGridDelegate::GameGridDelegate(GameGridView* view_) : QStyledItemDelegate(view_), view{view_} {}

QSize GameGridDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    const QSize grid = view->gridSize();
    return grid.isValid() ? grid : QSize(CellWidth, CellHeight);
}

QRectF GameGridDelegate::CardRect(const QRect& cell) {
    QRectF card(0, 0, CardWidth, CardHeight);
    card.moveCenter(QRectF(cell).center());
    return card;
}

QRectF GameGridDelegate::PlayButtonRect(const QRect& cell) {
    const QRectF card = CardRect(cell);
    const QRectF art(card.left(), card.top(), card.width(), card.width() * 0.86);
    QRectF play(0, 0, 46, 46);
    play.moveCenter(art.center() + QPointF(0, 4));
    return play;
}

void GameGridDelegate::ClearCache() {
    backdrop_cache.clear();
}

QPixmap GameGridDelegate::Backdrop(const QString& key, const QPixmap& icon,
                                   const QSize& size) const {
    const auto it = backdrop_cache.constFind(key);
    if (it != backdrop_cache.constEnd() && it->size() == size) {
        return *it;
    }

    QPixmap result(size);
    result.fill(Colors::Raised);
    if (!icon.isNull()) {
        // Squash the icon to a few pixels and blow it back up: a cheap, smooth color wash.
        const QImage tiny = icon.toImage().scaled(5, 5, Qt::IgnoreAspectRatio,
                                                  Qt::SmoothTransformation);
        const QImage wash =
            tiny.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPainter p(&result);
        p.drawImage(0, 0, wash);
        p.fillRect(result.rect(), WithAlpha(Colors::Window, 0.55));
    }
    if (backdrop_cache.size() > 2048) {
        backdrop_cache.clear();
    }
    backdrop_cache.insert(key, result);
    return result;
}

void GameGridDelegate::paint(QPainter* p, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    const qreal hover = EaseOutCubic(view->HoverProgress(index));
    const qreal appear = EaseOutCubic(view->AppearProgress(index));
    if (appear <= 0.0) {
        return;
    }
    const bool selected = option.state & QStyle::State_Selected;
    const bool focused = selected && view->hasFocus();
    const auto platform = static_cast<Platform>(index.data(GameGridModel::PlatformRole).toInt());
    const QString path = index.data(GameGridModel::PathRole).toString();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->setOpacity(appear);

    QRectF card = CardRect(option.rect);
    card.translate(0, (1.0 - appear) * 16.0 - hover * 4.0);

    const qreal scale = 1.0 + 0.04 * hover;
    p->translate(card.center());
    p->scale(scale, scale);
    p->translate(-card.center());

    // Glow / shadow.
    const qreal glow = std::max(hover, focused ? 0.7 : (selected ? 0.45 : 0.0));
    if (glow > 0.0) {
        for (int i = 4; i >= 1; i--) {
            QPainterPath halo;
            const qreal spread = i * 3.0;
            halo.addRoundedRect(card.adjusted(-spread, -spread + 2, spread, spread + 4),
                                CardRadius + spread, CardRadius + spread);
            p->fillPath(halo, WithAlpha(Colors::Accent, 0.045 * glow * (5 - i)));
        }
    } else {
        QPainterPath shadow;
        shadow.addRoundedRect(card.translated(0, 4), CardRadius, CardRadius);
        p->fillPath(shadow, WithAlpha(Qt::black, 0.35));
    }

    QPainterPath card_path;
    card_path.addRoundedRect(card, CardRadius, CardRadius);
    p->fillPath(card_path, Mix(Colors::Surface, Colors::Raised, hover));

    // Artwork area: blurred backdrop + icon.
    const QPixmap icon = index.data(GameGridModel::IconRole).value<QPixmap>();
    const QRectF art(card.left(), card.top(), card.width(), card.width() * 0.86);
    {
        p->save();
        p->setClipPath(card_path);
        const QSize art_size = art.size().toSize();
        p->drawPixmap(art.topLeft(), Backdrop(path, icon, art_size));

        QLinearGradient fade(art.topLeft(), art.bottomLeft());
        fade.setColorAt(0.0, WithAlpha(Colors::Window, 0.0));
        fade.setColorAt(0.65, WithAlpha(Colors::Window, 0.05));
        fade.setColorAt(1.0, Mix(Colors::Surface, Colors::Raised, hover));
        p->fillRect(art, fade);

        // Light sheen across the top, the "glass" edge.
        QLinearGradient sheen(art.topLeft(), art.topRight());
        sheen.setColorAt(0.0, WithAlpha(Qt::white, 0.07 + 0.05 * hover));
        sheen.setColorAt(0.5, WithAlpha(Qt::white, 0.0));
        p->fillRect(QRectF(art.left(), art.top(), art.width(), art.height() * 0.5), sheen);
        p->restore();
    }

    QRectF icon_rect(0, 0, IconSize, IconSize);
    icon_rect.moveCenter(art.center() + QPointF(0, 4));
    {
        QPainterPath icon_shadow;
        icon_shadow.addRoundedRect(icon_rect.translated(0, 5), 16, 16);
        p->fillPath(icon_shadow, WithAlpha(Qt::black, 0.45));

        QPainterPath icon_path;
        icon_path.addRoundedRect(icon_rect, 16, 16);
        p->save();
        p->setClipPath(icon_path);
        if (icon.isNull()) {
            p->fillPath(icon_path, Colors::Hover);
            const QIcon glyph = MakeIcon(Glyph::Gamepad, Colors::Muted, 48);
            glyph.paint(p, icon_rect.adjusted(24, 24, -24, -24).toRect());
        } else {
            // DS icons are 32x32 pixel art: keep them sharp.
            const bool pixel_art = platform != Platform::N3DS;
            p->setRenderHint(QPainter::SmoothPixmapTransform, !pixel_art);
            p->drawPixmap(icon_rect, icon, QRectF(icon.rect()));
        }
        p->restore();
        p->setPen(QPen(WithAlpha(Qt::white, 0.10), 1));
        p->drawPath(icon_path);
    }

    // Play button that pops in on hover.
    if (hover > 0.01) {
        QRectF play = PlayButtonRect(option.rect);
        play.translate(0, card.top() - CardRect(option.rect).top());
        const qreal s = 0.6 + 0.4 * hover;
        const QPointF c = play.center();
        play = QRectF(c.x() - play.width() * s / 2, c.y() - play.height() * s / 2,
                      play.width() * s, play.height() * s);
        p->save();
        p->setOpacity(appear * hover);
        QLinearGradient grad(play.topLeft(), play.bottomRight());
        grad.setColorAt(0.0, Colors::Accent);
        grad.setColorAt(1.0, Colors::Accent2);
        p->setPen(Qt::NoPen);
        p->setBrush(WithAlpha(Qt::black, 0.35));
        p->drawEllipse(play.translated(0, 3));
        p->setBrush(grad);
        p->drawEllipse(play);
        QPainterPath tri;
        const qreal r = play.width() * 0.22;
        tri.moveTo(c.x() - r * 0.7, c.y() - r);
        tri.lineTo(c.x() + r * 1.05, c.y());
        tri.lineTo(c.x() - r * 0.7, c.y() + r);
        tri.closeSubpath();
        p->fillPath(tri, Colors::OnAccent);
        p->restore();
    }

    // Platform badge.
    {
        QFont badge_font = option.font;
        badge_font.setPixelSize(10);
        badge_font.setWeight(QFont::Bold);
        badge_font.setLetterSpacing(QFont::PercentageSpacing, 108);
        const QString badge_text = PlatformName(platform);
        const QFontMetricsF fm(badge_font);
        const QRectF badge(card.left() + 10, card.top() + 10,
                           fm.horizontalAdvance(badge_text) + 14, 18);
        QPainterPath badge_path;
        badge_path.addRoundedRect(badge, 9, 9);
        const QColor badge_color = BadgeColor(platform);
        p->fillPath(badge_path, WithAlpha(Colors::Window, 0.72));
        p->setPen(QPen(WithAlpha(badge_color, 0.85), 1));
        p->drawPath(badge_path);
        p->setFont(badge_font);
        p->setPen(badge_color.lighter(125));
        p->drawText(badge, Qt::AlignCenter, badge_text);
    }

    // Favorite star.
    if (index.data(GameGridModel::FavoriteRole).toBool()) {
        const QPointF c(card.right() - 19, card.top() + 19);
        QPainterPath star;
        for (int i = 0; i < 10; i++) {
            const qreal radius = (i % 2 == 0) ? 7.5 : 3.3;
            const qreal angle = -Pi / 2 + i * Pi / 5;
            const QPointF pt(c.x() + radius * std::cos(angle), c.y() + radius * std::sin(angle));
            if (i == 0) {
                star.moveTo(pt);
            } else {
                star.lineTo(pt);
            }
        }
        star.closeSubpath();
        p->fillPath(star, QColor(0xfa, 0xcc, 0x15));
    }

    // Title + subtitle.
    {
        const QRectF text_rect(card.left() + 12, art.bottom() + 2, card.width() - 24,
                               card.bottom() - art.bottom() - 10);
        QFont title_font = option.font;
        title_font.setPixelSize(13);
        title_font.setWeight(QFont::DemiBold);
        const QStringList lines = WrapTitle(
            index.data(GameGridModel::TitleTextRole).toString(), title_font, text_rect.width());
        const QFontMetricsF tfm(title_font);
        p->setFont(title_font);
        p->setPen(Colors::Text);
        qreal y = text_rect.top();
        for (const QString& line : lines) {
            p->drawText(QRectF(text_rect.left(), y, text_rect.width(), tfm.height()),
                        Qt::AlignLeft | Qt::AlignVCenter, line);
            y += tfm.height();
        }

        const QString subtitle = index.data(GameGridModel::SubtitleRole).toString();
        if (!subtitle.isEmpty()) {
            QFont sub_font = option.font;
            sub_font.setPixelSize(11);
            const QFontMetricsF sfm(sub_font);
            p->setFont(sub_font);
            p->setPen(Colors::Muted);
            p->drawText(QRectF(text_rect.left(), text_rect.bottom() - sfm.height(),
                               text_rect.width(), sfm.height()),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        sfm.elidedText(subtitle, Qt::ElideRight, text_rect.width()));
        }
    }

    // Border.
    QColor border = Mix(WithAlpha(Qt::white, 0.07), WithAlpha(Colors::Accent, 0.85), hover);
    qreal border_width = 1.0;
    if (selected) {
        border = focused ? Colors::Accent : WithAlpha(Colors::Accent, 0.6);
        border_width = 2.0;
    }
    p->setPen(QPen(border, border_width));
    p->setBrush(Qt::NoBrush);
    p->drawPath(card_path);

    p->restore();
}

// ---------------------------------------------------------------------------- view

GameGridView::GameGridView(QWidget* parent) : QListView(parent) {
    setObjectName(QStringLiteral("AzalonGameGrid"));
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(false); // sizeHint follows the stretched grid cell
    setSpacing(0);
    setGridSize(QSize(CellWidth, CellHeight));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    verticalScrollBar()->setSingleStep(24);
    setItemDelegate(new GameGridDelegate(this));

    clock.start();
    anim_timer.setInterval(16);
    connect(&anim_timer, &QTimer::timeout, this, &GameGridView::Tick);

    scroll_animation = new QVariantAnimation(this);
    scroll_animation->setDuration(240);
    scroll_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(scroll_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { verticalScrollBar()->setValue(value.toInt()); });
}

qreal GameGridView::HoverProgress(const QModelIndex& index) const {
    return hover_progress.value(index.data(GameGridModel::PathRole).toString(), 0.0);
}

qreal GameGridView::AppearProgress(const QModelIndex& index) const {
    const auto it = appear_start.constFind(index.data(GameGridModel::PathRole).toString());
    if (it == appear_start.constEnd()) {
        return 1.0;
    }
    const qint64 elapsed = clock.elapsed() - it.value();
    return std::clamp(static_cast<qreal>(elapsed) / AppearMs, 0.0, 1.0);
}

void GameGridView::SetHovered(const QModelIndex& index) {
    const QString path = index.isValid() ? index.data(GameGridModel::PathRole).toString() : QString{};
    if (path == hovered_path) {
        return;
    }
    hovered_path = path;
    if (!path.isEmpty() && !hover_progress.contains(path)) {
        hover_progress.insert(path, 0.0);
    }
    viewport()->setCursor(path.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    StartAnimating();
}

void GameGridView::mouseMoveEvent(QMouseEvent* event) {
    SetHovered(indexAt(event->position().toPoint()));
    QListView::mouseMoveEvent(event);
}

void GameGridView::mousePressEvent(QMouseEvent* event) {
    pressed_play = QPersistentModelIndex();
    const QPoint pos = event->position().toPoint();
    const QModelIndex idx = indexAt(pos);
    if (event->button() == Qt::LeftButton && idx.isValid() &&
        GameGridDelegate::PlayButtonRect(visualRect(idx)).contains(pos)) {
        pressed_play = idx;
    }
    QListView::mousePressEvent(event);
}

void GameGridView::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    const QModelIndex idx = indexAt(pos);
    const bool launch = event->button() == Qt::LeftButton && pressed_play.isValid() &&
                        idx == QModelIndex(pressed_play) &&
                        GameGridDelegate::PlayButtonRect(visualRect(idx)).contains(pos);
    pressed_play = QPersistentModelIndex();
    QListView::mouseReleaseEvent(event);
    if (launch) {
        emit PlayRequested(idx);
    }
}

void GameGridView::mouseDoubleClickEvent(QMouseEvent* event) {
    // The first click on the play button already launched the game.
    const QPoint pos = event->position().toPoint();
    const QModelIndex idx = indexAt(pos);
    if (idx.isValid() && GameGridDelegate::PlayButtonRect(visualRect(idx)).contains(pos)) {
        event->accept();
        return;
    }
    QListView::mouseDoubleClickEvent(event);
}

void GameGridView::leaveEvent(QEvent* event) {
    SetHovered({});
    QListView::leaveEvent(event);
}

void GameGridView::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        currentIndex().isValid()) {
        emit PlayRequested(currentIndex());
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void GameGridView::wheelEvent(QWheelEvent* event) {
    // Touchpads already scroll smoothly; animate classic mouse wheel notches.
    if (!event->pixelDelta().isNull() || event->angleDelta().y() == 0 ||
        (event->modifiers() & Qt::ControlModifier)) {
        QListView::wheelEvent(event);
        return;
    }
    QScrollBar* bar = verticalScrollBar();
    const int base = scroll_animation->state() == QAbstractAnimation::Running ? scroll_target
                                                                              : bar->value();
    scroll_target = std::clamp(base - event->angleDelta().y() * 3 / 2, bar->minimum(),
                               bar->maximum());
    scroll_animation->stop();
    scroll_animation->setStartValue(bar->value());
    scroll_animation->setEndValue(scroll_target);
    scroll_animation->start();
    event->accept();
}

void GameGridView::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    UpdateGrid();
}

void GameGridView::UpdateGrid() {
    // Stretch cells so the columns fill the width evenly.
    const int width = viewport()->width() - 2;
    const int columns = std::max(1, width / CellWidth);
    const int cell = std::max(CellWidth, width / columns);
    if (gridSize().width() != cell) {
        setGridSize(QSize(cell, CellHeight));
    }
}

void GameGridView::MarkAppearing(int start, int end) {
    if (!model()) {
        return;
    }
    const qint64 now = clock.elapsed();
    int k = 0;
    for (int row = start; row <= end; row++) {
        const QString path = model()->index(row, 0).data(GameGridModel::PathRole).toString();
        if (path.isEmpty() || appear_start.contains(path)) {
            continue;
        }
        appear_start.insert(path, now + std::min(k, AppearStaggerMax) * AppearStaggerMs);
        k++;
    }
    if (k > 0) {
        StartAnimating();
    }
}

void GameGridView::rowsInserted(const QModelIndex& parent, int start, int end) {
    QListView::rowsInserted(parent, start, end);
    if (!parent.isValid()) {
        MarkAppearing(start, end);
    }
}

void GameGridView::reset() {
    QListView::reset();
    hover_progress.clear();
    hovered_path.clear();
    if (model()) {
        MarkAppearing(0, model()->rowCount() - 1);
    }
}

void GameGridView::StartAnimating() {
    if (!anim_timer.isActive()) {
        last_tick = clock.elapsed();
        anim_timer.start();
    }
}

void GameGridView::Tick() {
    const qint64 now = clock.elapsed();
    const qreal step = static_cast<qreal>(now - last_tick) / HoverMs;
    last_tick = now;

    bool animating = false;
    for (auto it = hover_progress.begin(); it != hover_progress.end();) {
        const qreal target = it.key() == hovered_path ? 1.0 : 0.0;
        qreal value = it.value();
        value = target > value ? std::min(target, value + step) : std::max(target, value - step);
        if (value <= 0.0 && target <= 0.0) {
            it = hover_progress.erase(it);
            continue;
        }
        it.value() = value;
        if (value != target) {
            animating = true;
        }
        ++it;
    }

    for (auto it = appear_start.begin(); it != appear_start.end();) {
        if (now - it.value() >= AppearMs) {
            it = appear_start.erase(it);
        } else {
            animating = true;
            ++it;
        }
    }

    viewport()->update();
    if (!animating) {
        anim_timer.stop();
    }
}

// ---------------------------------------------------------------------------- PillBar

PillBar::PillBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("AzalonPills"));
    setAttribute(Qt::WA_StyledBackground);
    layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    animation = new QVariantAnimation(this);
    animation->setDuration(260);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        indicator = value.toRectF();
        update();
    });
}

void PillBar::AddPill(const QString& text) {
    auto* button = new QPushButton(text, this);
    button->setProperty("pill", true);
    button->setCheckable(true);
    button->setAutoExclusive(false);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    const int index = static_cast<int>(pills.size());
    connect(button, &QPushButton::clicked, this, [this, index] {
        SetCurrent(index);
        emit CurrentChanged(index);
    });
    pills.push_back(button);
    layout->addWidget(button);
    if (index == current) {
        button->setChecked(true);
    }
}

void PillBar::SetPillText(int index, const QString& text) {
    if (index >= 0 && index < static_cast<int>(pills.size())) {
        pills[static_cast<std::size_t>(index)]->setText(text);
    }
}

QRectF PillBar::TargetRect() const {
    if (current < 0 || current >= static_cast<int>(pills.size())) {
        return {};
    }
    return QRectF(pills[static_cast<std::size_t>(current)]->geometry());
}

void PillBar::SetCurrent(int index, bool animate) {
    if (index < 0 || index >= static_cast<int>(pills.size())) {
        return;
    }
    current = index;
    for (std::size_t i = 0; i < pills.size(); i++) {
        pills[i]->setChecked(static_cast<int>(i) == index);
    }
    const QRectF target = TargetRect();
    if (!animate || indicator.isNull() || !isVisible()) {
        animation->stop();
        indicator = target;
        update();
        return;
    }
    animation->stop();
    animation->setStartValue(indicator);
    animation->setEndValue(target);
    animation->start();
}

void PillBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (animation->state() != QAbstractAnimation::Running) {
        indicator = TargetRect();
    }
}

void PillBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    if (indicator.isNull()) {
        indicator = TargetRect();
    }
    if (indicator.isNull()) {
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient grad(indicator.topLeft(), indicator.bottomRight());
    grad.setColorAt(0.0, Colors::Accent);
    grad.setColorAt(1.0, Colors::Accent2);
    QPainterPath path;
    path.addRoundedRect(indicator, indicator.height() / 2, indicator.height() / 2);
    p.fillPath(path, grad);
}

// ---------------------------------------------------------------------------- LibraryHeader

namespace {
QToolButton* MakeHeaderButton(QWidget* parent, Glyph glyph) {
    auto* button = new QToolButton(parent);
    button->setProperty("headerAction", true);
    button->setIcon(MakeIcon(glyph, Colors::Text, 18));
    button->setIconSize(QSize(18, 18));
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    return button;
}

QPixmap BrandMark(int size, qreal dpr) {
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF rect(0.5, 0.5, size - 1.0, size - 1.0);
    QLinearGradient grad(rect.topLeft(), rect.bottomRight());
    grad.setColorAt(0.0, Colors::Accent);
    grad.setColorAt(1.0, Colors::Accent2);
    QPainterPath path;
    path.addRoundedRect(rect, size * 0.28, size * 0.28);
    p.fillPath(path, grad);
    // Two stacked "screens" — the DS / 3DS silhouette.
    p.setPen(Qt::NoPen);
    p.setBrush(Colors::OnAccent);
    const qreal w = size * 0.52;
    p.drawRoundedRect(QRectF((size - w) / 2, size * 0.2, w, size * 0.26), 2, 2);
    p.drawRoundedRect(QRectF((size - w * 0.84) / 2, size * 0.54, w * 0.84, size * 0.26), 2, 2);
    return pixmap;
}
} // namespace

LibraryHeader::LibraryHeader(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("AzalonLibraryHeader"));
    setAttribute(Qt::WA_StyledBackground);

    layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 10, 14, 10);
    layout->setSpacing(10);

    auto* mark = new QLabel(this);
    mark->setPixmap(BrandMark(28, std::max<qreal>(2.0, devicePixelRatioF())));
    layout->addWidget(mark);

    auto* brand = new QLabel(QStringLiteral("Azalon"), this);
    brand->setObjectName(QStringLiteral("AzalonBrand"));
    auto* brand_accent = new QLabel(QStringLiteral("3-DS"), this);
    brand_accent->setObjectName(QStringLiteral("AzalonBrandAccent"));
    auto* brand_box = new QHBoxLayout;
    brand_box->setSpacing(0);
    brand_box->addWidget(brand);
    brand_box->addWidget(brand_accent);
    layout->addLayout(brand_box);
    layout->addSpacing(14);

    pills = new PillBar(this);
    pills->AddPill(QString{});
    pills->AddPill(QStringLiteral("3DS"));
    pills->AddPill(QStringLiteral("DS"));
    layout->addWidget(pills);
    connect(pills, &PillBar::CurrentChanged, this,
            [this](int index) { emit PlatformChanged(static_cast<PlatformFilter>(index)); });

    count = new QLabel(this);
    count->setObjectName(QStringLiteral("AzalonCount"));
    layout->addWidget(count);

    layout->addStretch(1);

    search_slot = new QHBoxLayout;
    search_slot->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(search_slot);

    auto* view_box = new QWidget(this);
    view_box->setObjectName(QStringLiteral("AzalonPills"));
    view_box->setAttribute(Qt::WA_StyledBackground);
    auto* view_layout = new QHBoxLayout(view_box);
    view_layout->setContentsMargins(3, 3, 3, 3);
    view_layout->setSpacing(2);
    grid_button = MakeHeaderButton(view_box, Glyph::Grid);
    list_button = MakeHeaderButton(view_box, Glyph::List);
    for (QToolButton* button : {grid_button, list_button}) {
        button->setProperty("headerAction", false);
        button->setCheckable(true);
        view_layout->addWidget(button);
    }
    auto* view_group = new QButtonGroup(this);
    view_group->setExclusive(true);
    view_group->addButton(grid_button, 0);
    view_group->addButton(list_button, 1);
    connect(view_group, &QButtonGroup::idClicked, this, &LibraryHeader::ViewModeChanged);
    layout->addWidget(view_box);

    layout->addSpacing(4);
    add_folder_button = MakeHeaderButton(this, Glyph::Folder);
    open_button = MakeHeaderButton(this, Glyph::Plus);
    lobby_button = MakeHeaderButton(this, Glyph::Users);
    settings_button = MakeHeaderButton(this, Glyph::Gear);
    layout->addWidget(add_folder_button);
    layout->addWidget(open_button);
    layout->addWidget(lobby_button);
    layout->addWidget(settings_button);
    connect(add_folder_button, &QToolButton::clicked, this, &LibraryHeader::AddFolderRequested);
    connect(open_button, &QToolButton::clicked, this, &LibraryHeader::OpenFileRequested);
    connect(lobby_button, &QToolButton::clicked, this, &LibraryHeader::LobbyRequested);
    connect(settings_button, &QToolButton::clicked, this, &LibraryHeader::SettingsRequested);

    Retranslate();
}

void LibraryHeader::SetSearchWidget(QWidget* widget) {
    search_slot->addWidget(widget);
}

void LibraryHeader::SetPlatform(PlatformFilter platform) {
    pills->SetCurrent(static_cast<int>(platform), false);
}

void LibraryHeader::SetViewMode(int mode) {
    grid_button->setChecked(mode == 0);
    list_button->setChecked(mode != 0);
}

void LibraryHeader::SetCount(int visible, int total) {
    visible_count = visible;
    total_count = total;
    if (visible == total) {
        count->setText(tr("%n game(s)", nullptr, total));
    } else {
        count->setText(tr("%1 of %2").arg(visible).arg(total));
    }
}

void LibraryHeader::Retranslate() {
    pills->SetPillText(0, tr("All"));
    grid_button->setToolTip(tr("Grid view"));
    list_button->setToolTip(tr("List view"));
    add_folder_button->setToolTip(tr("Add a game folder"));
    open_button->setToolTip(tr("Open a game file"));
    lobby_button->setToolTip(tr("Multiplayer lobby"));
    settings_button->setToolTip(tr("Settings"));
    SetCount(visible_count, total_count);
}

void LibraryHeader::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        Retranslate();
    }
    QWidget::changeEvent(event);
}

} // namespace AzalonUI
