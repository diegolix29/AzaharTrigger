// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "citra_qt/ds/ds_config.h"
#include "common/file_util.h"

namespace DS {

namespace {

QString ConfigPath() {
    return QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir)) +
           QStringLiteral("azalon_ds.ini");
}

} // namespace

Config& Config::Get() {
    static Config config = [] {
        Config c;
        c.Load();
        return c;
    }();
    return config;
}

void Config::Load() {
    QSettings s(ConfigPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("System"));
    console_type = s.value(QStringLiteral("console_type"), console_type).toInt();
    direct_boot = s.value(QStringLiteral("direct_boot"), direct_boot).toBool();
    jit = s.value(QStringLiteral("jit"), jit).toBool();
    threaded_renderer = s.value(QStringLiteral("threaded_renderer"), threaded_renderer).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("BIOS"));
    ds_bios9 = s.value(QStringLiteral("ds_bios9")).toString();
    ds_bios7 = s.value(QStringLiteral("ds_bios7")).toString();
    ds_firmware = s.value(QStringLiteral("ds_firmware")).toString();
    dsi_bios9 = s.value(QStringLiteral("dsi_bios9")).toString();
    dsi_bios7 = s.value(QStringLiteral("dsi_bios7")).toString();
    dsi_firmware = s.value(QStringLiteral("dsi_firmware")).toString();
    dsi_nand = s.value(QStringLiteral("dsi_nand")).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("Firmware"));
    nickname = s.value(QStringLiteral("nickname"), nickname).toString();
    language = s.value(QStringLiteral("language"), language).toInt();
    favorite_color = s.value(QStringLiteral("favorite_color"), favorite_color).toInt();
    s.endGroup();

    s.beginGroup(QStringLiteral("DLDI"));
    dldi_enable = s.value(QStringLiteral("enable"), dldi_enable).toBool();
    dldi_sync_folder = s.value(QStringLiteral("sync_folder")).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("Screen"));
    layout = static_cast<ScreenLayout>(
        s.value(QStringLiteral("layout"), static_cast<int>(layout)).toInt());
    swap_screens = s.value(QStringLiteral("swap_screens"), swap_screens).toBool();
    integer_scale = s.value(QStringLiteral("integer_scale"), integer_scale).toBool();
    smooth = s.value(QStringLiteral("smooth"), smooth).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("Network"));
    wireless_mode = s.value(QStringLiteral("wireless_mode"), wireless_mode).toInt();
    internet = s.value(QStringLiteral("internet"), internet).toBool();
    lan_player_name = s.value(QStringLiteral("lan_player_name"), lan_player_name).toString();
    s.endGroup();
}

void Config::Save() const {
    QSettings s(ConfigPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("System"));
    s.setValue(QStringLiteral("console_type"), console_type);
    s.setValue(QStringLiteral("direct_boot"), direct_boot);
    s.setValue(QStringLiteral("jit"), jit);
    s.setValue(QStringLiteral("threaded_renderer"), threaded_renderer);
    s.endGroup();

    s.beginGroup(QStringLiteral("BIOS"));
    s.setValue(QStringLiteral("ds_bios9"), ds_bios9);
    s.setValue(QStringLiteral("ds_bios7"), ds_bios7);
    s.setValue(QStringLiteral("ds_firmware"), ds_firmware);
    s.setValue(QStringLiteral("dsi_bios9"), dsi_bios9);
    s.setValue(QStringLiteral("dsi_bios7"), dsi_bios7);
    s.setValue(QStringLiteral("dsi_firmware"), dsi_firmware);
    s.setValue(QStringLiteral("dsi_nand"), dsi_nand);
    s.endGroup();

    s.beginGroup(QStringLiteral("Firmware"));
    s.setValue(QStringLiteral("nickname"), nickname);
    s.setValue(QStringLiteral("language"), language);
    s.setValue(QStringLiteral("favorite_color"), favorite_color);
    s.endGroup();

    s.beginGroup(QStringLiteral("DLDI"));
    s.setValue(QStringLiteral("enable"), dldi_enable);
    s.setValue(QStringLiteral("sync_folder"), dldi_sync_folder);
    s.endGroup();

    s.beginGroup(QStringLiteral("Screen"));
    s.setValue(QStringLiteral("layout"), static_cast<int>(layout));
    s.setValue(QStringLiteral("swap_screens"), swap_screens);
    s.setValue(QStringLiteral("integer_scale"), integer_scale);
    s.setValue(QStringLiteral("smooth"), smooth);
    s.endGroup();

    s.beginGroup(QStringLiteral("Network"));
    s.setValue(QStringLiteral("wireless_mode"), wireless_mode);
    s.setValue(QStringLiteral("internet"), internet);
    s.setValue(QStringLiteral("lan_player_name"), lan_player_name);
    s.endGroup();
}

QString DataDir() {
    const QString dir =
        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir)) +
        QStringLiteral("ds/");
    QDir().mkpath(dir);
    return dir;
}

QString SavesDir() {
    const QString dir = DataDir() + QStringLiteral("saves/");
    QDir().mkpath(dir);
    return dir;
}

QString StatesDir() {
    const QString dir = DataDir() + QStringLiteral("states/");
    QDir().mkpath(dir);
    return dir;
}

QStringList FileExtensions() {
    return {QStringLiteral("nds"), QStringLiteral("srl"), QStringLiteral("dsi"),
            QStringLiteral("ids")};
}

bool IsDSFile(const QString& path) {
    return FileExtensions().contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

// ----------------------------------------------------------------------------

ConfigDialog::ConfigDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("DS / DSi Settings"));
    const Config& cfg = Config::Get();
    auto* root = new QVBoxLayout(this);

    // System
    auto* system_box = new QGroupBox(tr("System"));
    auto* system = new QFormLayout(system_box);
    console_type = new QComboBox;
    console_type->addItems({tr("Nintendo DS"), tr("Nintendo DSi (needs DSi BIOS, firmware and NAND)")});
    console_type->setCurrentIndex(cfg.console_type);
    system->addRow(tr("Console:"), console_type);
    direct_boot = new QCheckBox(tr("Boot games directly (skip the firmware menu)"));
    direct_boot->setChecked(cfg.direct_boot);
    system->addRow(direct_boot);
    jit = new QCheckBox(tr("JIT recompiler (faster)"));
    jit->setChecked(cfg.jit);
    system->addRow(jit);
    threaded = new QCheckBox(tr("Threaded 3D rendering"));
    threaded->setChecked(cfg.threaded_renderer);
    system->addRow(threaded);
    root->addWidget(system_box);

    // BIOS / firmware
    auto* bios_box = new QGroupBox(tr("BIOS and firmware (leave empty to use the built-in ones)"));
    auto* bios = new QFormLayout(bios_box);
    ds_bios9 = AddPathRow(bios, tr("DS ARM9 BIOS:"), cfg.ds_bios9);
    ds_bios7 = AddPathRow(bios, tr("DS ARM7 BIOS:"), cfg.ds_bios7);
    ds_firmware = AddPathRow(bios, tr("DS firmware:"), cfg.ds_firmware);
    dsi_bios9 = AddPathRow(bios, tr("DSi ARM9 BIOS:"), cfg.dsi_bios9);
    dsi_bios7 = AddPathRow(bios, tr("DSi ARM7 BIOS:"), cfg.dsi_bios7);
    dsi_firmware = AddPathRow(bios, tr("DSi firmware:"), cfg.dsi_firmware);
    dsi_nand = AddPathRow(bios, tr("DSi NAND:"), cfg.dsi_nand);
    root->addWidget(bios_box);

    // Firmware user settings
    auto* user_box = new QGroupBox(tr("Console owner (built-in firmware)"));
    auto* user = new QFormLayout(user_box);
    nickname = new QLineEdit(cfg.nickname);
    nickname->setMaxLength(10);
    user->addRow(tr("Nickname:"), nickname);
    language = new QComboBox;
    language->addItems({tr("Japanese"), tr("English"), tr("French"), tr("German"), tr("Italian"),
                        tr("Spanish"), tr("Chinese"), tr("Korean")});
    language->setCurrentIndex(cfg.language);
    user->addRow(tr("Language:"), language);
    favorite_color = new QSpinBox;
    favorite_color->setRange(0, 15);
    favorite_color->setValue(cfg.favorite_color);
    user->addRow(tr("Favorite color (0-15):"), favorite_color);
    root->addWidget(user_box);

    // Homebrew SD
    auto* sd_box = new QGroupBox(tr("Homebrew SD card (DLDI)"));
    auto* sd = new QFormLayout(sd_box);
    dldi_enable = new QCheckBox(tr("Give homebrew a virtual SD card"));
    dldi_enable->setChecked(cfg.dldi_enable);
    sd->addRow(dldi_enable);
    dldi_folder = AddPathRow(sd, tr("Sync with folder:"), cfg.dldi_sync_folder, true);
    root->addWidget(sd_box);

    // Network
    auto* net_box = new QGroupBox(tr("Wireless and internet"));
    auto* net = new QFormLayout(net_box);
    wireless_mode = new QComboBox;
    wireless_mode->addItem(tr("Off"), -1);
    wireless_mode->addItem(tr("Local (same PC)"), 0);
    wireless_mode->addItem(tr("LAN / VPN (other PCs)"), 1);
    wireless_mode->setCurrentIndex(std::max(0, wireless_mode->findData(cfg.wireless_mode)));
    net->addRow(tr("Local wireless:"), wireless_mode);
    internet = new QCheckBox(
        tr("Internet access (Nintendo WFC replacements such as Wiimmfi / AltWFC)"));
    internet->setChecked(cfg.internet);
    net->addRow(internet);
    root->addWidget(net_box);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ConfigDialog::Accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

QLineEdit* ConfigDialog::AddPathRow(QFormLayout* form, const QString& label, const QString& value,
                                    bool directory) {
    auto* row = new QHBoxLayout;
    auto* edit = new QLineEdit(value);
    auto* browse = new QPushButton(tr("..."));
    browse->setFixedWidth(32);
    row->addWidget(edit);
    row->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, directory] {
        const QString start = QFileInfo(edit->text()).absolutePath();
        const QString path = directory ? QFileDialog::getExistingDirectory(this, QString(), start)
                                       : QFileDialog::getOpenFileName(this, QString(), start);
        if (!path.isEmpty())
            edit->setText(QDir::toNativeSeparators(path));
    });
    form->addRow(label, row);
    return edit;
}

void ConfigDialog::Accept() {
    Config& cfg = Config::Get();
    cfg.console_type = console_type->currentIndex();
    cfg.direct_boot = direct_boot->isChecked();
    cfg.jit = jit->isChecked();
    cfg.threaded_renderer = threaded->isChecked();
    cfg.ds_bios9 = ds_bios9->text().trimmed();
    cfg.ds_bios7 = ds_bios7->text().trimmed();
    cfg.ds_firmware = ds_firmware->text().trimmed();
    cfg.dsi_bios9 = dsi_bios9->text().trimmed();
    cfg.dsi_bios7 = dsi_bios7->text().trimmed();
    cfg.dsi_firmware = dsi_firmware->text().trimmed();
    cfg.dsi_nand = dsi_nand->text().trimmed();
    cfg.nickname = nickname->text().trimmed();
    cfg.language = language->currentIndex();
    cfg.favorite_color = favorite_color->value();
    cfg.dldi_enable = dldi_enable->isChecked();
    cfg.dldi_sync_folder = dldi_folder->text().trimmed();
    cfg.wireless_mode = wireless_mode->currentData().toInt();
    cfg.internet = internet->isChecked();
    cfg.Save();
    accept();
}

} // namespace DS
