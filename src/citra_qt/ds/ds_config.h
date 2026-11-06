// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace DS {

/// Screen arrangement of the two DS screens.
enum class ScreenLayout : int {
    Vertical = 0,   ///< top above bottom
    Horizontal = 1, ///< side by side
    TopOnly = 2,
    BottomOnly = 3,
};

/// Frontend settings of the DS core, stored in <config dir>/azalon_ds.ini.
struct Config {
    // System
    int console_type = 0; ///< 0 = DS, 1 = DSi
    bool direct_boot = true;
    bool jit = true;
    bool threaded_renderer = true;

    // BIOS / firmware (empty = built-in FreeBIOS and generated firmware)
    QString ds_bios9, ds_bios7, ds_firmware;
    QString dsi_bios9, dsi_bios7, dsi_firmware, dsi_nand;

    // Firmware user settings (generated firmware)
    QString nickname = QStringLiteral("Azalon");
    int language = 1; ///< 0 JP, 1 EN, 2 FR, 3 DE, 4 IT, 5 ES, 6 ZH, 7 KO
    int favorite_color = 0;

    // Homebrew SD card (DLDI)
    bool dldi_enable = true;
    QString dldi_sync_folder;

    // Screens
    ScreenLayout layout = ScreenLayout::Vertical;
    bool swap_screens = false;
    bool integer_scale = false;
    bool smooth = false;

    // Wireless / internet
    int wireless_mode = 0; ///< azds_mp_mode: -1 off, 0 local, 1 LAN
    bool internet = true;
    QString lan_player_name = QStringLiteral("Player");

    void Load();
    void Save() const;

    static Config& Get();
};

/// Folder for DS data: <user dir>/ds/ (saves, states, wfcsettings.bin, SD images).
QString DataDir();
QString SavesDir();
QString StatesDir();

/// True for files the DS core should run (.nds, .srl, .dsi, .ids).
bool IsDSFile(const QString& path);
QStringList FileExtensions();

class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget* parent = nullptr);

private:
    void Accept();
    QLineEdit* AddPathRow(QFormLayout* form, const QString& label, const QString& value,
                          bool directory = false);

    QComboBox* console_type;
    QCheckBox* direct_boot;
    QCheckBox* jit;
    QCheckBox* threaded;
    QLineEdit *ds_bios9, *ds_bios7, *ds_firmware;
    QLineEdit *dsi_bios9, *dsi_bios7, *dsi_firmware, *dsi_nand;
    QLineEdit* nickname;
    QComboBox* language;
    QSpinBox* favorite_color;
    QCheckBox* dldi_enable;
    QLineEdit* dldi_folder;
    QComboBox* wireless_mode;
    QCheckBox* internet;
};

} // namespace DS
