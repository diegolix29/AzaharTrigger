// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>

#include <QDialog>
#include <QHash>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTextEdit;
class QTreeView;

/**
 * DS local wireless over LAN/VPN: melonDS's own LAN protocol, run by the DS core.
 *
 * It lives in Azahar's Multiplayer menu: a "DS Local Wireless (LAN)" tab in the
 * lobby window (DS::LanBrowser), a host dialog and a room window styled after
 * Azahar's own room window. Android shows the same rooms (UDP 7063 discovery,
 * UDP 7064 session), so phones and PCs can play together.
 */
namespace DS::Lan {

/// Tells the module whether a DS game is emulating (its thread then services the network).
void SetGameRunningProbe(std::function<bool()> probe);
bool GameRunning();

/// Switches the core to LAN wireless (and remembers it for the next game).
/// Returns false, after telling the user why, while a game runs on another mode.
bool PrepareLan(QWidget* parent);

/// The name used in DS rooms: Azahar's multiplayer nickname.
QString Nickname();
void SetNickname(const QString& name);

/// The main window reports DS games starting and stopping; a hosted room shows them.
void SetCurrentGame(const QString& title, bool running);

bool InSession();
bool IsHost();

/// Opens (or raises) the room window. Does nothing when not in a room.
void ShowRoomWindow(QWidget* parent);
/// Leaves the room, if any, after asking.
void LeaveRoom(QWidget* parent, bool ask = true);
/// Opens the host dialog.
void ShowHostDialog(QWidget* parent);
/// Asks for an address and joins it.
void ShowDirectConnect(QWidget* parent);

/// This device's IPv4 addresses on its local networks, for people joining by IP.
QStringList LocalAddresses();

/// Room list with filters, embedded in the lobby window's DS tab.
class LanBrowser : public QWidget {
    Q_OBJECT
public:
    explicit LanBrowser(QWidget* parent = nullptr);
    ~LanBrowser() override;

    /// Browsing runs only while the tab is visible.
    void SetActive(bool active);

signals:
    /// Hosted or joined a room (the room window is already open).
    void EnteredRoom();

private:
    void Refresh();
    void JoinIndex(const QModelIndex& index);
    void UpdateFilters();

    QLineEdit* nickname;
    QLineEdit* search;
    QCheckBox* hide_full;
    QCheckBox* hide_locked;
    QCheckBox* hide_playing;
    QTreeView* room_list;
    QLabel* status;
    QStandardItemModel* model;
    QSortFilterProxyModel* proxy;
    int timer_id = 0;
    bool active = false;

protected:
    void timerEvent(QTimerEvent* event) override;
};

/// The current room: who is in it, chat, leave. Mirrors Azahar's ClientRoomWindow.
class LanRoomWindow : public QDialog {
    Q_OBJECT
public:
    explicit LanRoomWindow(QWidget* parent = nullptr);
    ~LanRoomWindow() override;

private:
    void Refresh();
    void SendChat();
    void ShowMemberMenu(const QPoint& pos);
    void AppendSystem(const QString& text);

    QLabel* description;
    QLabel* connection_info;
    QLabel* game_info;
    QPushButton* copy_address;
    QPushButton* leave;
    QTreeView* member_view;
    QStandardItemModel* members;
    QTextEdit* chat_history;
    QLineEdit* chat_message;
    QPushButton* send_message;
    unsigned chat_seen = 0;
    QHash<int, QString> known_players; ///< id -> name, for join/leave lines
    bool first_refresh = true;
    QString address_text;
    int timer_id = 0;
    bool was_in_session = true;

protected:
    void timerEvent(QTimerEvent* event) override;
};

} // namespace DS::Lan
