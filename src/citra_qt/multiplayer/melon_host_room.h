// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>
#include <QTimer>
#include <memory>
#include "network/lan_melon.h"

class QStandardItemModel;

namespace Ui {
class MelonHostRoom;
}

class MelonHostRoom : public QDialog {
    Q_OBJECT

public:
    explicit MelonHostRoom(QWidget* parent = nullptr);
    ~MelonHostRoom();

private slots:
    void OnHostRoom();
    void OnCloseRoom();
    void OnUpdateRoomInfo();
    void OnUpdatePlayerList();

private:
    void UpdateRoomStatus();
    void UpdatePlayerList();
    void SetHostedState(bool hosted);
    
    std::unique_ptr<Ui::MelonHostRoom> ui;
    std::unique_ptr<Network::MelonLANAdapter> melon_lan_adapter;
    QTimer* update_timer;
    bool is_hosted;
};
