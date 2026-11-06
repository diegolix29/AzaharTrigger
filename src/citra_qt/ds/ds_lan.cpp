// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QTimerEvent>
#include <QTreeView>
#include <QVBoxLayout>

#include <azalon_ds/azalon_ds.h>

#include "citra_qt/ds/ds_config.h"
#include "citra_qt/ds/ds_lan.h"
#include "citra_qt/multiplayer/validation.h"
#include "citra_qt/uisettings.h"

namespace DS::Lan {

namespace {

std::function<bool()> g_game_running;
QString g_game_title;
bool g_game_on = false;
QPointer<LanRoomWindow> g_room_window;
QTimer* g_pump = nullptr;

// Same palette as Azahar's chat room, so DS rooms read like 3DS rooms.
constexpr std::array<const char*, 16> kPlayerColor = {
    {"#0000FF", "#FF0000", "#8A2BE2", "#FF69B4", "#1E90FF", "#008000", "#00FF7F", "#B22222",
     "#DAA520", "#FF4500", "#2E8B57", "#5F9EA0", "#D2691E", "#9ACD32", "#FF7F50", "#FFFF00"}};
constexpr const char* kPingColor = "#FFFF00";

enum RoomColumn { ColName, ColGame, ColHost, ColPlayers, ColAddress, ColCount };
constexpr int kAddressRole = Qt::UserRole + 1;
constexpr int kLockedRole = Qt::UserRole + 2;
constexpr int kFullRole = Qt::UserRole + 3;
constexpr int kPlayingRole = Qt::UserRole + 4;

QString StatusName(int status) {
    switch (status) {
    case 1:
        return QObject::tr("Connected");
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

/// Services the room while no game runs (the game's thread does it otherwise).
void StartPump() {
    if (!g_pump) {
        g_pump = new QTimer(qApp);
        g_pump->setInterval(50);
        QObject::connect(g_pump, &QTimer::timeout, [] {
            if (!azds_lan_in_session()) {
                g_pump->stop();
                return;
            }
            if (!GameRunning())
                azds_mp_process();
        });
    }
    g_pump->start();
}

bool CheckNickname(QWidget* parent, const QString& name) {
    Validation validation;
    QString copy = name;
    int pos = 0;
    if (validation.GetNickname()->validate(copy, pos) == QValidator::Acceptable)
        return true;
    QMessageBox::warning(parent, QObject::tr("DS Local Wireless"),
                         QObject::tr("Nicknames are 4 to 20 characters: letters, numbers, spaces, "
                                     "\".\", \"_\" and \"-\"."));
    return false;
}

void EnteredRoom(QWidget* parent) {
    StartPump();
    if (IsHost() && !g_game_title.isEmpty())
        azds_lan_set_game(g_game_title.toUtf8().constData(), g_game_on ? 1 : 0);
    ShowRoomWindow(parent);
}

bool Join(QWidget* parent, const QString& address, const QString& password) {
    if (!PrepareLan(parent))
        return false;
    const QString name = Nickname();
    azds_lan_end_discovery();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QByteArray n = name.toUtf8(), a = address.toUtf8(), p = password.toUtf8();
    const bool ok = azds_lan_join(n.constData(), a.constData(), p.isEmpty() ? nullptr : p.constData());
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(
            parent, QObject::tr("DS Local Wireless"),
            QObject::tr("Could not join the room at %1.\n\nCheck the password, that both devices "
                        "are on the same network (or VPN), and that the host's firewall allows "
                        "UDP ports 7063 and 7064.")
                .arg(address));
        return false;
    }
    EnteredRoom(parent);
    return true;
}

QString HostNameOf(const azds_lan_room& r) {
    // AzalonDS hosts send their name as the description; other melonDS builds don't.
    return QString::fromUtf8(r.description);
}

QString GameText(const QString& game, bool in_game) {
    if (game.isEmpty() || game == QStringLiteral("Nintendo DS"))
        return QObject::tr("Not started yet");
    return in_game ? QObject::tr("%1 (playing)").arg(game) : game;
}

} // namespace

void SetGameRunningProbe(std::function<bool()> probe) {
    g_game_running = std::move(probe);
}

bool GameRunning() {
    return g_game_running && g_game_running();
}

bool PrepareLan(QWidget* parent) {
    if (GameRunning() && azds_mp_get_mode() != AZDS_MP_LAN) {
        QMessageBox::information(parent, QObject::tr("DS Local Wireless"),
                                 QObject::tr("Stop the DS game first, join or host a room, then "
                                             "start the game again."));
        return false;
    }
    Config& cfg = Config::Get();
    if (cfg.wireless_mode != AZDS_MP_LAN) {
        cfg.wireless_mode = AZDS_MP_LAN;
        cfg.Save();
    }
    if (!GameRunning())
        azds_mp_set_mode(AZDS_MP_LAN);
    return true;
}

QString Nickname() {
    if (!UISettings::values.nickname.isEmpty())
        return UISettings::values.nickname;
    return Config::Get().lan_player_name;
}

void SetNickname(const QString& name) {
    UISettings::values.nickname = name;
    Config& cfg = Config::Get();
    if (cfg.lan_player_name != name) {
        cfg.lan_player_name = name;
        cfg.Save();
    }
}

void SetCurrentGame(const QString& title, bool running) {
    g_game_title = title;
    g_game_on = running;
    if (IsHost())
        azds_lan_set_game(title.toUtf8().constData(), running ? 1 : 0);
}

bool InSession() {
    return azds_mp_get_mode() == AZDS_MP_LAN && azds_lan_in_session() != 0;
}

bool IsHost() {
    return azds_mp_get_mode() == AZDS_MP_LAN && azds_lan_is_host() != 0;
}

QStringList LocalAddresses() {
    char buffer[64];
    if (azds_lan_local_address(buffer, sizeof(buffer)))
        return {QString::fromLatin1(buffer)};
    return {};
}

void ShowRoomWindow(QWidget* parent) {
    if (!InSession())
        return;
    if (!g_room_window) {
        g_room_window = new LanRoomWindow(parent ? parent->window() : nullptr);
        g_room_window->setAttribute(Qt::WA_DeleteOnClose);
    }
    g_room_window->show();
    g_room_window->raise();
    g_room_window->activateWindow();
}

void LeaveRoom(QWidget* parent, bool ask) {
    if (!InSession())
        return;
    if (ask && QMessageBox::question(parent, QObject::tr("DS Local Wireless"),
                                     IsHost() ? QObject::tr("Close the room? Everyone in it "
                                                            "will be disconnected.")
                                              : QObject::tr("Leave the room?")) !=
                   QMessageBox::Yes)
        return;
    azds_lan_leave();
}

void ShowHostDialog(QWidget* parent) {
    if (InSession()) {
        ShowRoomWindow(parent);
        return;
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Host DS Room"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    Validation validation;

    auto* nickname = new QLineEdit(Nickname());
    nickname->setValidator(validation.GetNickname());
    auto* room_name = new QLineEdit(QObject::tr("%1's room").arg(Nickname()));
    room_name->setMaxLength(63);
    auto* password = new QLineEdit;
    password->setMaxLength(32);
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(QObject::tr("(Leave blank for an open room)"));
    auto* max_players = new QSpinBox;
    max_players->setRange(2, 16);
    max_players->setValue(4);
    form->addRow(QObject::tr("Room Name"), room_name);
    form->addRow(QObject::tr("Nickname"), nickname);
    form->addRow(QObject::tr("Password"), password);
    form->addRow(QObject::tr("Max Players"), max_players);
    layout->addLayout(form);

    const QStringList addresses = LocalAddresses();
    auto* info = new QLabel(
        QObject::tr("Devices on the same network see the room by themselves. Others can use "
                    "Direct Connect with %1. Over the internet, use a VPN such as ZeroTier or "
                    "Tailscale.")
            .arg(addresses.isEmpty() ? QObject::tr("your IP address")
                                     : QStringLiteral("<b>%1</b>").arg(addresses.join(
                                           QStringLiteral(", ")))));
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* host = buttons->addButton(QObject::tr("Host Room"), QDialogButtonBox::AcceptRole);
    host->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(host, &QPushButton::clicked, &dialog, [&] {
        const QString name = nickname->text().trimmed();
        if (!CheckNickname(&dialog, name))
            return;
        if (room_name->text().trimmed().isEmpty())
            room_name->setText(QObject::tr("%1's room").arg(name));
        if (!PrepareLan(&dialog))
            return;
        SetNickname(name);
        azds_lan_end_discovery();
        const QByteArray n = name.toUtf8(), r = room_name->text().trimmed().toUtf8(),
                         p = password->text().toUtf8();
        if (!azds_lan_host(n.constData(), max_players->value(), r.constData(),
                           p.isEmpty() ? nullptr : p.constData())) {
            QMessageBox::critical(&dialog, QObject::tr("DS Local Wireless"),
                                  QObject::tr("Could not host the room. Another program (or "
                                              "another emulator window) may be using UDP port "
                                              "7064."));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() == QDialog::Accepted)
        EnteredRoom(parent);
}

void ShowDirectConnect(QWidget* parent) {
    if (InSession()) {
        ShowRoomWindow(parent);
        return;
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Direct Connect to DS Room"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    Validation validation;
    auto* address = new QLineEdit(Config::Get().lan_last_address);
    address->setPlaceholderText(QStringLiteral("192.168.1.20"));
    auto* nickname = new QLineEdit(Nickname());
    nickname->setValidator(validation.GetNickname());
    auto* password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(QObject::tr("(only if the room has one)"));
    form->addRow(QObject::tr("Server Address"), address);
    form->addRow(QObject::tr("Nickname"), nickname);
    form->addRow(QObject::tr("Password"), password);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* connect = buttons->addButton(QObject::tr("Connect"), QDialogButtonBox::AcceptRole);
    connect->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(connect, &QPushButton::clicked, &dialog, [&] {
        const QString name = nickname->text().trimmed();
        if (!CheckNickname(&dialog, name))
            return;
        if (address->text().trimmed().isEmpty())
            return;
        SetNickname(name);
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString host = address->text().trimmed();
    Config& cfg = Config::Get();
    cfg.lan_last_address = host;
    cfg.Save();
    Join(parent, host, password->text());
}

// -------------------------------------------------------------------------------------------
// Room browser (lobby tab)

LanBrowser::LanBrowser(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(3);

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Nickname")));
    Validation validation;
    nickname = new QLineEdit(Nickname());
    nickname->setPlaceholderText(tr("Nickname"));
    nickname->setValidator(validation.GetNickname());
    bar->addWidget(nickname);
    bar->addSpacing(24);
    bar->addWidget(new QLabel(tr("Filters")));
    search = new QLineEdit;
    search->setPlaceholderText(tr("Search"));
    search->setClearButtonEnabled(true);
    bar->addWidget(search);
    hide_full = new QCheckBox(tr("Hide Full Rooms"));
    hide_locked = new QCheckBox(tr("Hide Locked Rooms"));
    hide_playing = new QCheckBox(tr("Hide Rooms In Game"));
    bar->addWidget(hide_full);
    bar->addWidget(hide_locked);
    bar->addWidget(hide_playing);
    bar->addStretch();
    auto* direct = new QPushButton(tr("Direct Connect..."));
    auto* host = new QPushButton(tr("Host Room..."));
    bar->addWidget(direct);
    bar->addWidget(host);
    root->addLayout(bar);

    model = new QStandardItemModel(0, ColCount, this);
    model->setHorizontalHeaderLabels(
        {tr("Room Name"), tr("Game"), tr("Host"), tr("Players"), tr("Address")});
    proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    proxy->setSortLocaleAware(true);

    room_list = new QTreeView;
    room_list->setModel(proxy);
    room_list->setRootIsDecorated(false);
    room_list->setAlternatingRowColors(true);
    room_list->setSelectionMode(QAbstractItemView::SingleSelection);
    room_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    room_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    room_list->setSortingEnabled(true);
    room_list->sortByColumn(ColName, Qt::AscendingOrder);
    room_list->header()->setSectionResizeMode(QHeaderView::Interactive);
    room_list->header()->setStretchLastSection(true);
    room_list->setColumnWidth(ColName, 220);
    room_list->setColumnWidth(ColGame, 240);
    room_list->setColumnWidth(ColHost, 120);
    room_list->setColumnWidth(ColPlayers, 70);
    root->addWidget(room_list);

    status = new QLabel;
    status->setWordWrap(true);
    root->addWidget(status);

    connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(hide_full, &QCheckBox::toggled, this, &LanBrowser::UpdateFilters);
    connect(hide_locked, &QCheckBox::toggled, this, &LanBrowser::UpdateFilters);
    connect(hide_playing, &QCheckBox::toggled, this, &LanBrowser::UpdateFilters);
    connect(room_list, &QTreeView::doubleClicked, this, &LanBrowser::JoinIndex);
    connect(nickname, &QLineEdit::editingFinished, this, [this] {
        if (nickname->hasAcceptableInput())
            SetNickname(nickname->text().trimmed());
    });
    connect(host, &QPushButton::clicked, this, [this] {
        ShowHostDialog(this);
        nickname->setText(Nickname());
        if (InSession())
            emit EnteredRoom();
    });
    connect(direct, &QPushButton::clicked, this, [this] {
        ShowDirectConnect(this);
        nickname->setText(Nickname());
        if (InSession())
            emit EnteredRoom();
    });
}

LanBrowser::~LanBrowser() {
    SetActive(false);
}

void LanBrowser::SetActive(bool on) {
    if (on == active)
        return;
    active = on;
    if (on) {
        if (!InSession() && !GameRunning())
            azds_mp_set_mode(AZDS_MP_LAN); // browsing only; nothing is saved until you join
        if (azds_mp_get_mode() == AZDS_MP_LAN && !InSession())
            azds_lan_start_discovery();
        Refresh();
        timer_id = startTimer(250);
    } else {
        if (timer_id)
            killTimer(timer_id);
        timer_id = 0;
        if (azds_mp_get_mode() == AZDS_MP_LAN && !InSession())
            azds_lan_end_discovery();
    }
}

void LanBrowser::timerEvent(QTimerEvent* event) {
    if (event->timerId() != timer_id)
        return;
    // The room pump services sessions; while only browsing, nobody else does.
    if (!InSession() && !GameRunning() && azds_mp_get_mode() == AZDS_MP_LAN)
        azds_mp_process();
    Refresh();
}

void LanBrowser::UpdateFilters() {
    for (int row = 0; row < model->rowCount(); row++) {
        QStandardItem* item = model->item(row, ColName);
        const bool hidden = (hide_full->isChecked() && item->data(kFullRole).toBool()) ||
                            (hide_locked->isChecked() && item->data(kLockedRole).toBool()) ||
                            (hide_playing->isChecked() && item->data(kPlayingRole).toBool());
        const QModelIndex index = proxy->mapFromSource(model->index(row, 0));
        if (index.isValid())
            room_list->setRowHidden(index.row(), QModelIndex(), hidden);
    }
}

void LanBrowser::Refresh() {
    if (InSession()) {
        azds_lan_room room;
        azds_lan_get_current_room(&room);
        status->setText(tr("You are in the room \"%1\". Leave it from the room window (Multiplayer "
                           "> Show Current DS Room) to join another one.")
                            .arg(QString::fromUtf8(room.room_name)));
        return;
    }
    if (azds_mp_get_mode() != AZDS_MP_LAN) {
        status->setText(tr("A DS game is running with another wireless mode. Stop it to browse "
                           "LAN rooms."));
        return;
    }

    azds_lan_room list[32];
    const int count = azds_lan_get_rooms(list, 32);

    // Rebuild only when something changed, so the selection and scroll stay put.
    QStringList keys;
    for (int i = 0; i < count; i++) {
        const auto& r = list[i];
        keys << QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                    .arg(QString::fromUtf8(r.address), QString::fromUtf8(r.room_name),
                         QString::fromUtf8(r.game_name), QString::fromUtf8(r.description))
                    .arg(r.num_players)
                    .arg(r.max_players)
                    .arg(r.has_password)
                    .arg(r.in_game);
    }
    const QString signature = keys.join(QLatin1Char('\n'));
    if (signature != model->property("signature").toString()) {
        model->setProperty("signature", signature);
        QString selected;
        const QModelIndex current = room_list->currentIndex();
        if (current.isValid())
            selected = proxy->data(proxy->index(current.row(), ColName), kAddressRole).toString();

        model->removeRows(0, model->rowCount());
        for (int i = 0; i < count; i++) {
            const auto& r = list[i];
            const QString address = QString::fromUtf8(r.address);
            auto* name = new QStandardItem(QString::fromUtf8(r.room_name));
            if (r.has_password)
                name->setIcon(QIcon::fromTheme(QStringLiteral("lock")));
            name->setData(address, kAddressRole);
            name->setData(r.has_password != 0, kLockedRole);
            name->setData(r.max_players > 0 && r.num_players >= r.max_players, kFullRole);
            name->setData(r.in_game != 0, kPlayingRole);
            name->setToolTip(tr("Room code %1").arg(QString::fromUtf8(r.room_code)));
            auto* game = new QStandardItem(GameText(QString::fromUtf8(r.game_name), r.in_game));
            auto* host = new QStandardItem(HostNameOf(r));
            auto* players =
                new QStandardItem(QStringLiteral("%1 / %2").arg(r.num_players).arg(r.max_players));
            players->setData(Qt::AlignCenter, Qt::TextAlignmentRole);
            auto* addr = new QStandardItem(address);
            model->appendRow({name, game, host, players, addr});
        }
        UpdateFilters();
        if (!selected.isEmpty()) {
            for (int row = 0; row < proxy->rowCount(); row++) {
                if (proxy->data(proxy->index(row, ColName), kAddressRole).toString() == selected)
                    room_list->setCurrentIndex(proxy->index(row, ColName));
            }
        }
    }

    const QStringList addresses = LocalAddresses();
    const QString me = addresses.isEmpty()
                           ? tr("No network connection found.")
                           : tr("This PC is %1 on your network.").arg(addresses.join(
                                 QStringLiteral(", ")));
    status->setText(count == 0 ? tr("Looking for DS rooms on your network... %1 Phones and PCs "
                                    "must be on the same Wi-Fi/LAN (or VPN), with UDP ports 7063 "
                                    "and 7064 allowed by the firewall.")
                                     .arg(me)
                               : tr("%n room(s) found. Double-click one to join. %1", "", count)
                                     .arg(me));
}

void LanBrowser::JoinIndex(const QModelIndex& index) {
    if (!index.isValid())
        return;
    if (InSession()) {
        ShowRoomWindow(this);
        return;
    }
    const QString name_text = nickname->text().trimmed();
    if (!CheckNickname(this, name_text))
        return;
    SetNickname(name_text);
    const QModelIndex first = proxy->index(index.row(), ColName);
    const QString address = proxy->data(first, kAddressRole).toString();
    QString password;
    if (proxy->data(first, kLockedRole).toBool()) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Password Required to Join"));
        auto* layout = new QFormLayout(&dialog);
        auto* edit = new QLineEdit;
        edit->setEchoMode(QLineEdit::Password);
        layout->addRow(tr("Password:"), edit);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        password = edit->text();
    }
    if (Join(this, address, password))
        emit EnteredRoom();
    else if (azds_mp_get_mode() == AZDS_MP_LAN)
        azds_lan_start_discovery();
}

// -------------------------------------------------------------------------------------------
// Room window

LanRoomWindow::LanRoomWindow(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint |
                          Qt::WindowMinMaxButtonsHint) {
    setWindowTitle(tr("DS Room Window"));
    resize(807, 432);

    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    auto* info = new QVBoxLayout;
    description = new QLabel;
    QFont title_font = description->font();
    title_font.setBold(true);
    title_font.setPointSizeF(title_font.pointSizeF() * 1.2);
    description->setFont(title_font);
    connection_info = new QLabel;
    connection_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    game_info = new QLabel;
    game_info->setWordWrap(true);
    info->addWidget(description);
    info->addWidget(connection_info);
    info->addWidget(game_info);
    top->addLayout(info, 1);
    copy_address = new QPushButton(tr("Copy Address"));
    leave = new QPushButton(tr("Leave Room"));
    top->addWidget(copy_address, 0, Qt::AlignTop);
    top->addWidget(leave, 0, Qt::AlignTop);
    root->addLayout(top);

    auto* splitter = new QSplitter;
    member_view = new QTreeView;
    members = new QStandardItemModel(0, 3, this);
    members->setHorizontalHeaderLabels({tr("Nickname"), tr("Status"), tr("Ping")});
    member_view->setModel(members);
    member_view->setRootIsDecorated(false);
    member_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    member_view->setSelectionMode(QAbstractItemView::SingleSelection);
    member_view->setContextMenuPolicy(Qt::CustomContextMenu);
    member_view->header()->setStretchLastSection(false);
    member_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    member_view->setMinimumWidth(240);
    splitter->addWidget(member_view);

    auto* chat_box = new QWidget;
    auto* chat = new QVBoxLayout(chat_box);
    chat->setContentsMargins(0, 0, 0, 0);
    chat_history = new QTextEdit;
    chat_history->setReadOnly(true);
    chat_history->setUndoRedoEnabled(false);
    chat_history->document()->setMaximumBlockCount(500);
    chat->addWidget(chat_history);
    auto* send_row = new QHBoxLayout;
    chat_message = new QLineEdit;
    chat_message->setPlaceholderText(tr("Send Chat Message"));
    chat_message->setMaxLength(255);
    send_message = new QPushButton(tr("Send Message"));
    send_row->addWidget(chat_message);
    send_row->addWidget(send_message);
    chat->addLayout(send_row);
    splitter->addWidget(chat_box);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto* hint = new QLabel(tr("Everyone in the room starts the same DS game, then uses the "
                               "game's own wireless menu (DS Download Play, multiplayer...)."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    connect(send_message, &QPushButton::clicked, this, &LanRoomWindow::SendChat);
    connect(chat_message, &QLineEdit::returnPressed, this, &LanRoomWindow::SendChat);
    connect(leave, &QPushButton::clicked, this, [this] {
        LeaveRoom(this);
        if (!InSession())
            close();
    });
    connect(copy_address, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(address_text); });
    connect(member_view, &QTreeView::customContextMenuRequested, this,
            &LanRoomWindow::ShowMemberMenu);

    AppendSystem(IsHost() ? tr("Room created. Waiting for players...")
                          : tr("Joined the room."));
    Refresh();
    timer_id = startTimer(250);
}

LanRoomWindow::~LanRoomWindow() {
    if (timer_id)
        killTimer(timer_id);
}

void LanRoomWindow::timerEvent(QTimerEvent* event) {
    if (event->timerId() == timer_id)
        Refresh();
}

void LanRoomWindow::AppendSystem(const QString& text) {
    chat_history->append(QStringLiteral("<font color='#808080'>[%1] * %2</font>")
                             .arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat),
                                  text.toHtmlEscaped()));
}

void LanRoomWindow::SendChat() {
    const QString text = chat_message->text().trimmed();
    if (text.isEmpty() || !InSession())
        return;
    azds_lan_send_chat(text.toUtf8().constData());
    chat_message->clear();
    Refresh();
}

void LanRoomWindow::ShowMemberMenu(const QPoint& pos) {
    const QModelIndex index = member_view->indexAt(pos);
    if (!index.isValid() || !IsHost())
        return;
    QStandardItem* item = members->item(index.row(), 0);
    if (!item || item->data(Qt::UserRole + 1).toBool()) // that's us
        return;
    const int id = item->data(Qt::UserRole).toInt();
    const QString name = item->data(Qt::UserRole + 2).toString();
    QMenu menu(this);
    QAction* kick = menu.addAction(tr("Kick"));
    if (menu.exec(member_view->viewport()->mapToGlobal(pos)) == kick &&
        QMessageBox::question(this, tr("Kick Player"),
                              tr("Are you sure you would like to <b>kick</b> %1?")
                                  .arg(name.toHtmlEscaped())) == QMessageBox::Yes)
        azds_lan_kick(id);
}

void LanRoomWindow::Refresh() {
    const bool in_session = InSession();
    if (!in_session) {
        if (was_in_session) {
            was_in_session = false;
            AppendSystem(tr("You are no longer in the room."));
            members->removeRows(0, members->rowCount());
            description->setText(tr("Not in a room"));
            connection_info->clear();
            game_info->setText(tr("Open Multiplayer > Browse DS LAN Rooms to join another one."));
            chat_message->setEnabled(false);
            send_message->setEnabled(false);
            leave->setEnabled(false);
            copy_address->setEnabled(false);
        }
        return;
    }

    azds_lan_room room;
    azds_lan_get_current_room(&room);
    const bool host = IsHost();
    const QString room_name = QString::fromUtf8(room.room_name);
    setWindowTitle(tr("DS Room Window - %1").arg(room_name));
    description->setText(room_name);
    if (host) {
        const QStringList addresses = LocalAddresses();
        address_text = addresses.join(QStringLiteral(", "));
        connection_info->setText(
            tr("You are hosting - Code %1 - %2 / %3 players - Address %4")
                .arg(QString::fromUtf8(room.room_code))
                .arg(room.num_players)
                .arg(room.max_players)
                .arg(address_text.isEmpty() ? tr("unknown") : address_text));
    } else {
        address_text = QString::fromUtf8(room.address);
        connection_info->setText(tr("Hosted by %1 - Code %2 - %3 / %4 players - Address %5")
                                     .arg(QString::fromUtf8(room.description),
                                          QString::fromUtf8(room.room_code))
                                     .arg(room.num_players)
                                     .arg(room.max_players)
                                     .arg(address_text));
    }
    copy_address->setEnabled(!address_text.isEmpty());
    game_info->setText(tr("Game: %1")
                           .arg(GameText(QString::fromUtf8(room.game_name), room.in_game)));

    // Members, plus join/leave lines in the chat like Azahar rooms.
    azds_lan_player list[16];
    const int count = azds_lan_get_players(list, 16);
    QSet<int> now;
    QStringList rows;
    for (int i = 0; i < count; i++) {
        const auto& p = list[i];
        if (p.status == 0 || p.status == 4)
            continue;
        now.insert(p.id);
        rows << QStringLiteral("%1|%2|%3|%4").arg(p.id).arg(QString::fromUtf8(p.name)).arg(p.status).arg(p.ping / 10);
    }
    const QString signature = rows.join(QLatin1Char('\n'));
    if (signature != members->property("signature").toString()) {
        members->setProperty("signature", signature);
        members->removeRows(0, members->rowCount());
        for (int i = 0; i < count; i++) {
            const auto& p = list[i];
            if (p.status == 0 || p.status == 4)
                continue;
            const QString name = QString::fromUtf8(p.name);
            auto* nick = new QStandardItem(p.is_local ? tr("%1 (You)").arg(name) : name);
            nick->setData(p.id, Qt::UserRole);
            nick->setData(p.is_local != 0, Qt::UserRole + 1);
            nick->setData(name, Qt::UserRole + 2);
            nick->setForeground(QColor(QString::fromLatin1(kPlayerColor[p.id % 16])));
            nick->setToolTip(QString::fromUtf8(p.address));
            auto* state = new QStandardItem(StatusName(p.status));
            auto* ping =
                new QStandardItem(p.is_local ? QStringLiteral("-") : tr("%1 ms").arg(p.ping));
            members->appendRow({nick, state, ping});
        }
        member_view->resizeColumnToContents(1);
    }
    QHash<int, QString> names;
    for (int i = 0; i < count; i++)
        if (now.contains(list[i].id))
            names.insert(list[i].id, QString::fromUtf8(list[i].name));
    if (!first_refresh) {
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            if (!known_players.contains(it.key()))
                AppendSystem(tr("%1 has joined").arg(it.value()));
        for (auto it = known_players.cbegin(); it != known_players.cend(); ++it)
            if (!names.contains(it.key()))
                AppendSystem(tr("%1 has left").arg(it.value()));
    }
    first_refresh = false;
    known_players = names;

    // Chat
    const unsigned total = azds_lan_chat_total();
    if (total < chat_seen)
        chat_seen = 0;
    if (total != chat_seen) {
        static azds_lan_chat messages[100];
        const int got = azds_lan_get_chat_messages(messages, 100);
        const int fresh = std::min<int>(got, static_cast<int>(total - chat_seen));
        const QString me = Nickname();
        const QString stamp = QLocale().toString(QTime::currentTime(), QLocale::ShortFormat);
        for (int i = got - fresh; i < got; i++) {
            const auto& m = messages[i];
            const QString text = QString::fromUtf8(m.text);
            const bool ping = !m.is_local &&
                              QStringLiteral(" %1 ").arg(text).contains(
                                  QStringLiteral(" @%1 ").arg(me), Qt::CaseInsensitive);
            chat_history->append(
                QStringLiteral("[%1] <font color='%2'>&lt;%3&gt;</font> <font style='%4'>%5</font>")
                    .arg(stamp, QString::fromLatin1(kPlayerColor[m.sender_id % 16]),
                         QString::fromUtf8(m.sender).toHtmlEscaped(),
                         ping ? QStringLiteral("background-color: %1; color: #000000")
                                    .arg(QString::fromLatin1(kPingColor))
                              : QString(),
                         text.toHtmlEscaped()));
        }
        chat_seen = total;
        chat_history->verticalScrollBar()->setValue(chat_history->verticalScrollBar()->maximum());
    }
}

} // namespace DS::Lan
