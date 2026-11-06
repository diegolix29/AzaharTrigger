// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <QApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QTimerEvent>
#include <QVBoxLayout>

#include <azalon_ds/azalon_ds.h>

#include "citra_qt/ds/ds_config.h"
#include "citra_qt/ds/ds_lan_dialog.h"

namespace DS {

namespace {

QTableWidget* MakeTable(const QStringList& headers) {
    auto* table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

QString StatusName(int status) {
    switch (status) {
    case 1:
        return QObject::tr("Client");
    case 2:
        return QObject::tr("Host");
    case 3:
        return QObject::tr("Connecting");
    case 4:
        return QObject::tr("Disconnected");
    default:
        return {};
    }
}

/// Keeps a hosted/joined room alive after the lobby closes, until a game starts
/// (the game's emulation thread services the network from then on).
void KeepSessionAlive(std::function<bool()> game_running) {
    static QTimer* pump = nullptr;
    if (!pump) {
        pump = new QTimer(qApp);
        pump->setInterval(50);
    }
    pump->disconnect();
    QObject::connect(pump, &QTimer::timeout, [game_running = std::move(game_running)] {
        if (!azds_lan_in_session()) {
            pump->stop();
            return;
        }
        if (!game_running || !game_running())
            azds_mp_process();
    });
    pump->start();
}

} // namespace

LanDialog::LanDialog(std::function<bool()> game_running_, QWidget* parent)
    : QDialog(parent), game_running(std::move(game_running_)) {
    setWindowTitle(tr("DS Local Wireless (LAN)"));
    resize(640, 560);

    // LAN play needs the LAN wireless backend.
    Config& cfg = Config::Get();
    if (cfg.wireless_mode != AZDS_MP_LAN) {
        cfg.wireless_mode = AZDS_MP_LAN;
        cfg.Save();
    }
    // Never swap the backend under a running game; GMainWindow only opens the lobby
    // mid-game if LAN mode is already active.
    if (!game_running || !game_running())
        azds_mp_set_mode(AZDS_MP_LAN);

    auto* root = new QVBoxLayout(this);

    auto* name_row = new QFormLayout;
    player_name = new QLineEdit(cfg.lan_player_name);
    player_name->setMaxLength(31);
    name_row->addRow(tr("Your name:"), player_name);
    root->addLayout(name_row);

    // Rooms found on the network
    auto* find_box = new QGroupBox(tr("Rooms on your network"));
    auto* find = new QVBoxLayout(find_box);
    rooms = MakeTable({tr("Room"), tr("Game"), tr("Players"), tr("Password"), tr("Address")});
    find->addWidget(rooms);
    auto* find_buttons = new QHBoxLayout;
    join_button = new QPushButton(tr("Join selected"));
    direct_button = new QPushButton(tr("Join by IP..."));
    find_buttons->addStretch();
    find_buttons->addWidget(direct_button);
    find_buttons->addWidget(join_button);
    find->addLayout(find_buttons);
    root->addWidget(find_box);

    // Hosting
    auto* host_box = new QGroupBox(tr("Host a room"));
    auto* host = new QFormLayout(host_box);
    room_name = new QLineEdit(tr("%1's room").arg(cfg.lan_player_name));
    room_name->setMaxLength(63);
    host->addRow(tr("Room name:"), room_name);
    password = new QLineEdit;
    password->setMaxLength(32);
    password->setPlaceholderText(tr("optional"));
    host->addRow(tr("Password:"), password);
    max_players = new QSpinBox;
    max_players->setRange(2, 16);
    max_players->setValue(4);
    host->addRow(tr("Max players:"), max_players);
    host_button = new QPushButton(tr("Host"));
    host->addRow(host_button);
    root->addWidget(host_box);

    // Current session
    auto* session_box = new QGroupBox(tr("Session"));
    auto* session = new QVBoxLayout(session_box);
    status = new QLabel(tr("Not connected."));
    session->addWidget(status);
    players = MakeTable({tr("#"), tr("Name"), tr("Status"), tr("Address"), tr("Ping")});
    players->setMaximumHeight(140);
    session->addWidget(players);
    chat_log = new QTextEdit;
    chat_log->setReadOnly(true);
    chat_log->setMaximumHeight(100);
    session->addWidget(chat_log);
    auto* chat_row = new QHBoxLayout;
    chat_input = new QLineEdit;
    chat_input->setPlaceholderText(tr("Say something..."));
    auto* send = new QPushButton(tr("Send"));
    leave_button = new QPushButton(tr("Leave"));
    chat_row->addWidget(chat_input);
    chat_row->addWidget(send);
    chat_row->addWidget(leave_button);
    session->addLayout(chat_row);
    root->addWidget(session_box);

    auto* hint = new QLabel(tr("Everyone joins the same room first, then starts the game and "
                               "uses the game's own wireless menu."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    connect(host_button, &QPushButton::clicked, this, &LanDialog::Host);
    connect(join_button, &QPushButton::clicked, this, &LanDialog::JoinSelected);
    connect(direct_button, &QPushButton::clicked, this, &LanDialog::JoinDirect);
    connect(rooms, &QTableWidget::cellDoubleClicked, this, &LanDialog::JoinSelected);
    connect(leave_button, &QPushButton::clicked, this, &LanDialog::Leave);
    connect(send, &QPushButton::clicked, this, &LanDialog::SendChat);
    connect(chat_input, &QLineEdit::returnPressed, this, &LanDialog::SendChat);

    in_session = azds_lan_in_session() != 0;
    if (!in_session)
        azds_lan_start_discovery();
    UpdateSessionState();
    Refresh();
    timer_id = startTimer(250);
}

LanDialog::~LanDialog() {
    if (timer_id)
        killTimer(timer_id);
    // Keep an active session (the game needs it); only stop browsing.
    if (!in_session)
        azds_lan_end_discovery();
    else
        KeepSessionAlive(game_running);
    Config& cfg = Config::Get();
    cfg.lan_player_name = player_name->text().trimmed();
    cfg.Save();
}

void LanDialog::timerEvent(QTimerEvent* event) {
    if (event->timerId() != timer_id)
        return;
    if (!game_running || !game_running())
        azds_mp_process(); // nobody else pumps the network while no game runs
    Refresh();
}

bool LanDialog::CheckName() {
    if (player_name->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("AzalonDS"), tr("Please enter your name first."));
        return false;
    }
    return true;
}

void LanDialog::Host() {
    if (!CheckName())
        return;
    azds_lan_end_discovery();
    const QByteArray name = player_name->text().trimmed().toUtf8();
    const QByteArray room = room_name->text().trimmed().toUtf8();
    const QByteArray pass = password->text().toUtf8();
    if (!azds_lan_host(name.constData(), max_players->value(), room.constData(),
                       pass.isEmpty() ? nullptr : pass.constData())) {
        QMessageBox::critical(this, tr("AzalonDS"),
                              tr("Could not host. Is port 7064 already in use?"));
        azds_lan_start_discovery();
        return;
    }
    in_session = true;
    UpdateSessionState();
}

void LanDialog::Join(const QString& address) {
    if (!CheckName() || address.isEmpty())
        return;
    QString pass;
    // Ask for the password if the selected room has one.
    const int row = rooms->currentRow();
    if (row >= 0 && rooms->item(row, 4) && rooms->item(row, 4)->text() == address &&
        rooms->item(row, 3) && !rooms->item(row, 3)->text().isEmpty()) {
        bool ok = false;
        pass = QInputDialog::getText(this, tr("Password"), tr("Room password:"),
                                     QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    azds_lan_end_discovery();
    const QByteArray name = player_name->text().trimmed().toUtf8();
    const QByteArray host = address.toUtf8();
    const QByteArray pw = pass.toUtf8();
    if (!azds_lan_join(name.constData(), host.constData(), pw.isEmpty() ? nullptr : pw.constData())) {
        QMessageBox::critical(this, tr("AzalonDS"), tr("Could not join %1.").arg(address));
        azds_lan_start_discovery();
        return;
    }
    in_session = true;
    UpdateSessionState();
}

void LanDialog::JoinSelected() {
    const int row = rooms->currentRow();
    if (row < 0 || !rooms->item(row, 4))
        return;
    Join(rooms->item(row, 4)->text());
}

void LanDialog::JoinDirect() {
    bool ok = false;
    const QString address = QInputDialog::getText(this, tr("Join by IP"),
                                                  tr("Host address (IPv4):"), QLineEdit::Normal,
                                                  QString(), &ok)
                                .trimmed();
    if (ok)
        Join(address);
}

void LanDialog::Leave() {
    azds_lan_leave();
    in_session = false;
    chat_text.clear();
    chat_log->clear();
    azds_lan_start_discovery();
    UpdateSessionState();
}

void LanDialog::SendChat() {
    const QString text = chat_input->text().trimmed();
    if (text.isEmpty() || !in_session)
        return;
    azds_lan_send_chat(text.toUtf8().constData());
    chat_input->clear();
}

void LanDialog::UpdateSessionState() {
    host_button->setEnabled(!in_session);
    join_button->setEnabled(!in_session);
    direct_button->setEnabled(!in_session);
    rooms->setEnabled(!in_session);
    leave_button->setEnabled(in_session);
    chat_input->setEnabled(in_session);
    if (!in_session)
        status->setText(tr("Not connected."));
}

void LanDialog::Refresh() {
    if (!in_session) {
        azds_lan_room list[32];
        const int count = azds_lan_get_rooms(list, 32);
        const QString selected = rooms->currentRow() >= 0 && rooms->item(rooms->currentRow(), 4)
                                     ? rooms->item(rooms->currentRow(), 4)->text()
                                     : QString();
        rooms->setRowCount(count);
        for (int i = 0; i < count; i++) {
            const auto& r = list[i];
            const QStringList cells = {
                QString::fromUtf8(r.room_name),
                QString::fromUtf8(r.game_name),
                QStringLiteral("%1/%2").arg(r.num_players).arg(r.max_players),
                r.has_password ? tr("yes") : QString(),
                QString::fromUtf8(r.address),
            };
            for (int c = 0; c < cells.size(); c++)
                rooms->setItem(i, c, new QTableWidgetItem(cells[c]));
            if (cells[4] == selected)
                rooms->selectRow(i);
        }
        return;
    }

    azds_lan_player list[16];
    const int count = azds_lan_get_players(list, 16);
    players->setRowCount(count);
    for (int i = 0; i < count; i++) {
        const auto& p = list[i];
        const QStringList cells = {
            QString::number(p.id + 1),
            QString::fromUtf8(p.name) + (p.is_local ? tr(" (you)") : QString()),
            StatusName(p.status),
            QString::fromUtf8(p.address),
            p.is_local ? QString() : QStringLiteral("%1 ms").arg(p.ping),
        };
        for (int c = 0; c < cells.size(); c++)
            players->setItem(i, c, new QTableWidgetItem(cells[c]));
    }
    status->setText(tr("Connected: %1 of %2 players.")
                        .arg(azds_lan_num_players())
                        .arg(azds_lan_max_players()));

    // The core keeps the last 100 lines, so a line count alone can stop changing:
    // redraw whenever the text differs.
    static char lines[64][300];
    const int chat_count = azds_lan_get_chat(lines, 64);
    QStringList chat;
    for (int i = 0; i < chat_count; i++)
        chat.append(QString::fromUtf8(lines[i]));
    const QString text = chat.join(QLatin1Char('\n'));
    if (text != chat_text) {
        chat_text = text;
        chat_log->setPlainText(text);
        chat_log->moveCursor(QTextCursor::End);
    }
}

} // namespace DS
