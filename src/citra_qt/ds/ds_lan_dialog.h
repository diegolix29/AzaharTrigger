// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;

namespace DS {

/// Lobby for DS local wireless over LAN/VPN (melonDS LAN): find, host and join rooms,
/// see who is connected and chat. Start the game on every PC once everyone joined.
class LanDialog : public QDialog {
    Q_OBJECT
public:
    /// `game_running` tells whether a DS game is emulating (it then pumps the network).
    LanDialog(std::function<bool()> game_running, QWidget* parent = nullptr);
    ~LanDialog() override;

private:
    void Refresh();
    void Host();
    void Join(const QString& address);
    void JoinSelected();
    void JoinDirect();
    void Leave();
    void SendChat();
    void UpdateSessionState();
    bool CheckName();

    std::function<bool()> game_running;
    bool in_session = false;
    int chat_seen = 0;

    QLineEdit* player_name;
    QTableWidget* rooms;
    QLineEdit* room_name;
    QLineEdit* password;
    QSpinBox* max_players;
    QPushButton* host_button;
    QPushButton* join_button;
    QPushButton* direct_button;
    QPushButton* leave_button;
    QLabel* status;
    QTableWidget* players;
    QTextEdit* chat_log;
    QLineEdit* chat_input;
    int timer_id = 0;

protected:
    void timerEvent(QTimerEvent* event) override;
};

} // namespace DS
