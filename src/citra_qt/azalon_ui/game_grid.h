// Copyright 2026 Azalon3-DS Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <vector>
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QListView>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QWidget>
#include "common/common_types.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QStandardItemModel;
class QToolButton;
class QVariantAnimation;

namespace AzalonUI {

enum class Platform { N3DS = 0, DS = 1, DSi = 2 };
enum class PlatformFilter { All = 0, N3DS = 1, DS = 2 };

/// DS rows use a synthetic program id ("NDS" + game code, see game_list_worker.cpp).
Platform PlatformOf(u64 program_id, const QString& file_type);
bool MatchesPlatform(PlatformFilter filter, Platform platform);
QString PlatformName(Platform platform);

/// Flat list of the games in GameList's folder tree (one row per file, favorites skipped).
/// Row order is unspecified; GameGridFilter sorts.
class GameGridModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        PlatformRole = Qt::UserRole + 100,
        TitleTextRole,
        SubtitleRole,
        FavoriteRole,
        PathRole,
        SearchTextRole,
        ProgramIdTextRole,
        IconRole,
    };

    explicit GameGridModel(QStandardItemModel* source, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    /// Column 0 of the matching row in the GameList model (invalid if it went away).
    QModelIndex SourceIndex(const QModelIndex& index) const;

    /// Re-reads favorites / play time without touching rows.
    void RefreshData();

public slots:
    void ScheduleRebuild();

private:
    struct Entry {
        QPersistentModelIndex index;
        Platform platform;
        QString path;
    };

    void Rebuild();

    QStandardItemModel* source;
    std::vector<Entry> entries;
    QTimer rebuild_timer;
};

class GameGridFilter : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    /// `text` must be lower case (same rules as GameList's filter bar).
    void SetFilter(PlatformFilter platform, const QString& text);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    PlatformFilter platform = PlatformFilter::All;
    QString text;
};

class GameGridView;

/// Paints one game as a rounded "glass" card.
class GameGridDelegate : public QStyledItemDelegate {
public:
    explicit GameGridDelegate(GameGridView* view);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    static QRectF CardRect(const QRect& cell);
    static QRectF PlayButtonRect(const QRect& cell);
    void ClearCache();

private:
    QPixmap Backdrop(const QString& key, const QPixmap& icon, const QSize& size) const;

    GameGridView* view;
    mutable QHash<QString, QPixmap> backdrop_cache;
};

/// Icon-mode list with hover / appear animations and smooth wheel scrolling.
class GameGridView : public QListView {
    Q_OBJECT

public:
    explicit GameGridView(QWidget* parent = nullptr);

    qreal HoverProgress(const QModelIndex& index) const;
    qreal AppearProgress(const QModelIndex& index) const;

    void reset() override;

signals:
    void PlayRequested(const QModelIndex& index);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void rowsInserted(const QModelIndex& parent, int start, int end) override;

private:
    void SetHovered(const QModelIndex& index);
    void MarkAppearing(int start, int end);
    void StartAnimating();
    void Tick();
    void UpdateGrid();

    QString hovered_path;
    QHash<QString, qreal> hover_progress;
    QHash<QString, qint64> appear_start;
    QElapsedTimer clock;
    QTimer anim_timer;
    qint64 last_tick = 0;

    QVariantAnimation* scroll_animation = nullptr;
    int scroll_target = 0;
    QPersistentModelIndex pressed_play;
};

/// Segmented "All | 3DS | DS" control with a sliding highlight.
class PillBar : public QWidget {
    Q_OBJECT

public:
    explicit PillBar(QWidget* parent = nullptr);

    void AddPill(const QString& text);
    void SetPillText(int index, const QString& text);
    void SetCurrent(int index, bool animate = true);
    int Current() const {
        return current;
    }

signals:
    void CurrentChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRectF TargetRect() const;

    QHBoxLayout* layout;
    std::vector<QPushButton*> pills;
    int current = 0;
    QRectF indicator;
    QVariantAnimation* animation;
};

/// Top bar of the game library: brand, platform pills, search, view toggle and quick actions.
class LibraryHeader : public QWidget {
    Q_OBJECT

public:
    explicit LibraryHeader(QWidget* parent = nullptr);

    /// Puts GameList's search field into the bar.
    void SetSearchWidget(QWidget* widget);
    void SetPlatform(PlatformFilter platform);
    void SetViewMode(int mode);
    void SetCount(int visible, int total);
    void Retranslate();

signals:
    void PlatformChanged(AzalonUI::PlatformFilter platform);
    void ViewModeChanged(int mode);
    void AddFolderRequested();
    void OpenFileRequested();
    void LobbyRequested();
    void SettingsRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    QHBoxLayout* layout;
    QHBoxLayout* search_slot;
    PillBar* pills;
    QLabel* count;
    QToolButton* grid_button;
    QToolButton* list_button;
    QToolButton* add_folder_button;
    QToolButton* open_button;
    QToolButton* lobby_button;
    QToolButton* settings_button;
    int visible_count = 0;
    int total_count = 0;
};

} // namespace AzalonUI
