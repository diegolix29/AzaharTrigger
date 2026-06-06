// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include <QString>
#include "citra_qt/multiplayer/melon_host_room.h"
#include "citra_qt/multiplayer/message.h"
#include "citra_qt/multiplayer/validation.h"
#include "citra_qt/uisettings.h"
#include "common/logging/log.h"
#include "network/network_settings.h"
#include "ui_melon_host_room.h"

MelonHostRoom::MelonHostRoom(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::MelonHostRoom>()), is_hosted(false) {

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

    // Load default values
    ui->nickname->setText(UISettings::values.nickname);
    if (ui->nickname->text().isEmpty() && !NetSettings::values.citra_username.empty()) {
        ui->nickname->setText(QString::fromStdString(NetSettings::values.citra_username));
    }

    // Set default room name
    if (ui->room_name->text().isEmpty()) {
        ui->room_name->setText(tr("%1's game").arg(ui->nickname->text()));
    }

    // Set up update timer
    update_timer = new QTimer(this);
    connect(update_timer, &QTimer::timeout, this, &MelonHostRoom::OnUpdateRoomInfo);
    connect(update_timer, &QTimer::timeout, this, &MelonHostRoom::OnUpdatePlayerList);

    // Connect buttons
    connect(ui->host_button, &QPushButton::clicked, this, &MelonHostRoom::OnHostRoom);
    connect(ui->close_button, &QPushButton::clicked, this, &MelonHostRoom::OnCloseRoom);

    // Update initial state
    UpdateRoomStatus();
}

MelonHostRoom::~MelonHostRoom() = default;

void MelonHostRoom::OnHostRoom() {
    if (!is_hosted) {
        // Validate input
        if (!ui->nickname->hasAcceptableInput()) {
            NetworkMessage::ErrorManager::ShowError(
                NetworkMessage::ErrorManager::USERNAME_NOT_VALID);
            return;
        }

        if (ui->room_name->text().isEmpty()) {
            QMessageBox::warning(this, tr("Error"), tr("Room name cannot be empty"));
            return;
        }

        const std::string nickname = ui->nickname->text().toStdString();
        const int max_players = ui->max_players->value();

        // Start hosting
        if (melon_lan_adapter->StartHost(nickname, max_players)) {
            is_hosted = true;
            SetHostedState(true);
            update_timer->start(1000); // Update every second
            LOG_INFO(Network, "Started MelonDS LAN room hosting");
        } else {
            NetworkMessage::ErrorManager::ShowError(
                NetworkMessage::ErrorManager::UNABLE_TO_CONNECT);
            LOG_ERROR(Network, "Failed to start MelonDS LAN room hosting");
        }
    } else {
        // Stop hosting
        melon_lan_adapter->EndSession();
        is_hosted = false;
        SetHostedState(false);
        update_timer->stop();
        LOG_INFO(Network, "Stopped MelonDS LAN room hosting");
    }
}

void MelonHostRoom::OnCloseRoom() {
    if (is_hosted) {
        melon_lan_adapter->EndSession();
        update_timer->stop();
    }
    accept();
}

void MelonHostRoom::OnUpdateRoomInfo() {
    if (is_hosted && melon_lan_adapter) {
        melon_lan_adapter->Process();
        UpdateRoomStatus();
    }
}

void MelonHostRoom::OnUpdatePlayerList() {
    if (is_hosted && melon_lan_adapter) {
        UpdatePlayerList();
    }
}

void MelonHostRoom::UpdateRoomStatus() {
    if (is_hosted && melon_lan_adapter && melon_lan_adapter->IsActive()) {
        ui->status_label->setText(tr("Hosting"));
        ui->status_label->setStyleSheet(QStringLiteral("color: green;"));

        const int num_players = melon_lan_adapter->GetNumPlayers();
        const int max_players = melon_lan_adapter->GetMaxPlayers();
        ui->label_players->setText(
            tr("Connected Players: %1/%2").arg(num_players).arg(max_players));
    } else {
        ui->status_label->setText(tr("Not Hosted"));
        ui->status_label->setStyleSheet(QStringLiteral("color: red;"));
        ui->label_players->setText(tr("Connected Players: 0/0"));
    }
}

void MelonHostRoom::UpdatePlayerList() {
    ui->players_list->clear();

    if (is_hosted && melon_lan_adapter) {
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

void MelonHostRoom::SetHostedState(bool hosted) {
    if (hosted) {
        ui->host_button->setText(tr("Stop Hosting"));
        ui->nickname->setEnabled(false);
        ui->room_name->setEnabled(false);
        ui->max_players->setEnabled(false);
    } else {
        ui->host_button->setText(tr("Host Room"));
        ui->nickname->setEnabled(true);
        ui->room_name->setEnabled(true);
        ui->max_players->setEnabled(true);
    }
}
