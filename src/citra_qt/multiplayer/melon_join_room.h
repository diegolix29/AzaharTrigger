// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QStandardItemModel>
#include <QTimer>
#include "network/lan_melon.h"

namespace Ui {
class MelonJoinRoom;
}

class MelonJoinRoom : public QDialog {
    Q_OBJECT

public:
    explicit MelonJoinRoom(QWidget* parent = nullptr);
    ~MelonJoinRoom();

private slots:
    void OnRefreshRooms();
    void OnJoinDiscoveredRoom();
    void OnManualConnect();
    void OnDisconnect();
    void OnUpdateDiscovery();
    void OnUpdateRoomInfo();
    void OnUpdatePlayerList();

private:
    void UpdateDiscoveryList();
    void UpdateRoomStatus();
    void UpdatePlayerList();
    void SetConnectedState(bool connected);
    void FormatIpAddress(u32 address, QString& formatted);

    std::unique_ptr<Ui::MelonJoinRoom> ui;
    std::unique_ptr<Network::MelonLANAdapter> melon_lan_adapter;
    QStandardItemModel* discovery_model;
    QTimer* discovery_timer;
    QTimer* update_timer;
    bool is_connected;
};
