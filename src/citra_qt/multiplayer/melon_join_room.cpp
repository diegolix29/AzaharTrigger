// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QHeaderView>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QString>
#include "citra_qt/multiplayer/melon_join_room.h"
#include "citra_qt/multiplayer/message.h"
#include "citra_qt/multiplayer/validation.h"
#include "citra_qt/uisettings.h"
#include "common/logging/log.h"
#include "network/network_settings.h"
#include "ui_melon_join_room.h"

MelonJoinRoom::MelonJoinRoom(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::MelonJoinRoom>()), is_connected(false) {

    ui->setupUi(this);

    // Initialize MelonLAN adapter
    melon_lan_adapter = std::make_unique<Network::MelonLANAdapter>();
    if (!melon_lan_adapter->Init()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to initialize MelonDS LAN adapter"));
        reject();
        return;
    }

    // Set up validation
    Validation validation;
    ui->nickname->setValidator(validation.GetNickname());

    // Load default nickname
    ui->nickname->setText(UISettings::values.nickname);
    if (ui->nickname->text().isEmpty() && !NetSettings::values.citra_username.empty()) {
        ui->nickname->setText(QString::fromStdString(NetSettings::values.citra_username));
    }

    // Set up discovery model
    discovery_model = new QStandardItemModel(this);
    discovery_model->insertColumns(0, 4);
    discovery_model->setHeaderData(0, Qt::Horizontal, tr("Session Name"), Qt::DisplayRole);
    discovery_model->setHeaderData(1, Qt::Horizontal, tr("IP Address"), Qt::DisplayRole);
    discovery_model->setHeaderData(2, Qt::Horizontal, tr("Players"), Qt::DisplayRole);
    discovery_model->setHeaderData(3, Qt::Horizontal, tr("Status"), Qt::DisplayRole);

    ui->room_list->setModel(discovery_model);
    ui->room_list->header()->setSectionResizeMode(QHeaderView::Interactive);
    ui->room_list->header()->stretchLastSection();
    ui->room_list->setAlternatingRowColors(true);
    ui->room_list->setSelectionMode(QHeaderView::SingleSelection);
    ui->room_list->setSelectionBehavior(QHeaderView::SelectRows);
    ui->room_list->setEditTriggers(QHeaderView::NoEditTriggers);
    ui->room_list->setSortingEnabled(true);

    // Set up timers
    discovery_timer = new QTimer(this);
    connect(discovery_timer, &QTimer::timeout, this, &MelonJoinRoom::OnUpdateDiscovery);

    update_timer = new QTimer(this);
    connect(update_timer, &QTimer::timeout, this, &MelonJoinRoom::OnUpdateRoomInfo);
    connect(update_timer, &QTimer::timeout, this, &MelonJoinRoom::OnUpdatePlayerList);

    // Connect buttons
    connect(ui->refresh_button, &QPushButton::clicked, this, &MelonJoinRoom::OnRefreshRooms);
    connect(ui->room_list, &QTreeView::doubleClicked, this, &MelonJoinRoom::OnJoinDiscoveredRoom);
    connect(ui->connect_button, &QPushButton::clicked, this, &MelonJoinRoom::OnManualConnect);
    connect(ui->disconnect_button, &QPushButton::clicked, this, &MelonJoinRoom::OnDisconnect);
    connect(ui->close_button, &QPushButton::clicked, this, &MelonJoinRoom::accept);

    // Start discovery
    discovery_timer->start(2000); // Update every 2 seconds

    // Update initial state
    UpdateRoomStatus();
}

MelonJoinRoom::~MelonJoinRoom() = default;

void MelonJoinRoom::OnRefreshRooms() {
    UpdateDiscoveryList();
}

void MelonJoinRoom::OnJoinDiscoveredRoom() {
    if (is_connected) {
        return;
    }

    // Validate nickname
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::USERNAME_NOT_VALID);
        return;
    }

    // Get selected room
    auto selection = ui->room_list->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("Please select a room to join"));
        return;
    }

    const int row = selection.first().row();
    const QString ip_address = discovery_model->item(row, 1)->text();

    const std::string nickname = ui->nickname->text().toStdString();

    // Connect to room
    if (melon_lan_adapter->StartClient(nickname, ip_address.toStdString())) {
        is_connected = true;
        SetConnectedState(true);
        update_timer->start(1000); // Update every second
        LOG_INFO(Network, "Connected to MelonDS LAN room at {}", ip_address.toStdString());
    } else {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::UNABLE_TO_CONNECT);
        LOG_ERROR(Network, "Failed to connect to MelonDS LAN room at {}", ip_address.toStdString());
    }
}

void MelonJoinRoom::OnManualConnect() {
    if (is_connected) {
        return;
    }

    // Validate nickname
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::USERNAME_NOT_VALID);
        return;
    }

    const QString ip_address = ui->ip_address->text().trimmed();
    if (ip_address.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please enter an IP address"));
        return;
    }

    const std::string nickname = ui->nickname->text().toStdString();

    // Connect to room
    if (melon_lan_adapter->StartClient(nickname, ip_address.toStdString())) {
        is_connected = true;
        SetConnectedState(true);
        update_timer->start(1000); // Update every second
        ui->ip_address->clear();
        LOG_INFO(Network, "Connected to MelonDS LAN room at {}", ip_address.toStdString());
    } else {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::UNABLE_TO_CONNECT);
        LOG_ERROR(Network, "Failed to connect to MelonDS LAN room at {}", ip_address.toStdString());
    }
}

void MelonJoinRoom::OnDisconnect() {
    if (is_connected) {
        melon_lan_adapter->EndSession();
        is_connected = false;
        SetConnectedState(false);
        update_timer->stop();
        LOG_INFO(Network, "Disconnected from MelonDS LAN room");
    }
}

void MelonJoinRoom::OnUpdateDiscovery() {
    if (!is_connected) {
        UpdateDiscoveryList();
    }
}

void MelonJoinRoom::OnUpdateRoomInfo() {
    if (is_connected && melon_lan_adapter) {
        melon_lan_adapter->Process();
        UpdateRoomStatus();
    }
}

void MelonJoinRoom::OnUpdatePlayerList() {
    if (is_connected && melon_lan_adapter) {
        UpdatePlayerList();
    }
}

void MelonJoinRoom::UpdateDiscoveryList() {
    if (!melon_lan_adapter) {
        return;
    }

    melon_lan_adapter->Process();

    auto discovery_list = melon_lan_adapter->GetDiscoveryList();
    discovery_model->clear();
    discovery_model->insertColumns(0, 4);
    discovery_model->setHeaderData(0, Qt::Horizontal, tr("Session Name"), Qt::DisplayRole);
    discovery_model->setHeaderData(1, Qt::Horizontal, tr("IP Address"), Qt::DisplayRole);
    discovery_model->setHeaderData(2, Qt::Horizontal, tr("Players"), Qt::DisplayRole);
    discovery_model->setHeaderData(3, Qt::Horizontal, tr("Status"), Qt::DisplayRole);

    for (const auto& [address, data] : discovery_list) {
        auto item_name = new QStandardItem(QString::fromUtf8(data.SessionName));
        auto item_address = new QStandardItem();
        auto item_players =
            new QStandardItem(tr("%1/%2").arg(data.NumPlayers).arg(data.MaxPlayers));
        auto item_status = new QStandardItem(data.Status == 0 ? tr("Idle") : tr("Playing"));

        // Format IP address
        QString ip_str;
        FormatIpAddress(address, ip_str);
        item_address->setText(ip_str);

        discovery_model->appendRow({item_name, item_address, item_players, item_status});
    }

    ui->discovery_status->setText(tr("Found %1 room(s)").arg(discovery_list.size()));
}

void MelonJoinRoom::UpdateRoomStatus() {
    if (is_connected && melon_lan_adapter && melon_lan_adapter->IsActive()) {
        ui->status_label->setText(tr("Connected"));
        ui->status_label->setStyleSheet(QStringLiteral("color: green;"));

        // Try to get room name from player list (host is usually first)
        auto players = melon_lan_adapter->GetPlayerList();
        QString room_name = tr("Unknown Room");
        for (const auto& player : players) {
            if (player.Status == Network::MelonPlayerStatus::Player_Host) {
                room_name = tr("%1's game").arg(QString::fromUtf8(player.Name));
                break;
            }
        }
        ui->label_room_name->setText(tr("Room: %1").arg(room_name));

        const int num_players = melon_lan_adapter->GetNumPlayers();
        const int max_players = melon_lan_adapter->GetMaxPlayers();
        ui->label_players->setText(tr("Players: %1/%2").arg(num_players).arg(max_players));
    } else {
        ui->status_label->setText(tr("Not Connected"));
        ui->status_label->setStyleSheet(QStringLiteral("color: red;"));
        ui->label_room_name->setText(tr("Room: Not Connected"));
        ui->label_players->setText(tr("Players: 0/0"));
    }
}

void MelonJoinRoom::UpdatePlayerList() {
    ui->players_list->clear();

    if (is_connected && melon_lan_adapter) {
        auto players = melon_lan_adapter->GetPlayerList();
        for (const auto& player : players) {
            QString player_info = tr("%1 (ID: %2) - %3")
                                      .arg(QString::fromUtf8(player.Name))
                                      .arg(player.ID)
                                      .arg(player.IsLocalPlayer ? tr("You") : tr("Remote"));

            // Add status indicator
            QString status_text;
            switch (player.Status) {
            case Network::MelonPlayerStatus::Player_Host:
                status_text = tr("Host");
                break;
            case Network::MelonPlayerStatus::Player_Client:
                status_text = tr("Client");
                break;
            case Network::MelonPlayerStatus::Player_Connecting:
                status_text = tr("Connecting");
                break;
            case Network::MelonPlayerStatus::Player_Disconnected:
                status_text = tr("Disconnected");
                break;
            default:
                status_text = tr("Unknown");
                break;
            }

            player_info += tr(" - %1").arg(status_text);

            auto item = new QListWidgetItem(player_info);
            ui->players_list->addItem(item);
        }
    }
}

void MelonJoinRoom::SetConnectedState(bool connected) {
    if (connected) {
        ui->disconnect_button->setEnabled(true);
        ui->nickname->setEnabled(false);
        ui->ip_address->setEnabled(false);
        ui->connect_button->setEnabled(false);
        ui->room_list->setEnabled(false);
        ui->refresh_button->setEnabled(false);
    } else {
        ui->disconnect_button->setEnabled(false);
        ui->nickname->setEnabled(true);
        ui->ip_address->setEnabled(true);
        ui->connect_button->setEnabled(true);
        ui->room_list->setEnabled(true);
        ui->refresh_button->setEnabled(true);
    }
}

void MelonJoinRoom::FormatIpAddress(u32 address, QString& formatted) {
    formatted = QStringLiteral("%1.%2.%3.%4")
                    .arg((address >> 24) & 0xFF)
                    .arg((address >> 16) & 0xFF)
                    .arg((address >> 8) & 0xFF)
                    .arg(address & 0xFF);
}
