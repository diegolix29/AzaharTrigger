// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <QObject>
#include <QString>
#include "citra_qt/update_checker.h"

namespace Updater {

/// Whether this build is capable of applying an update automatically (as
/// opposed to only being able to point the user at a download page).
bool CanSelfUpdate();

/// Picks the release asset (from a GitHub release) that matches the platform
/// (and, where relevant, windowing system) this build is running on.
/// Returns nullopt if no suitable asset was found in the list.
std::optional<UpdateChecker::ReleaseAsset> PickAssetForThisPlatform(
    const std::vector<UpdateChecker::ReleaseAsset>& assets);

/// Downloads a release asset and applies it in place, replacing the current
/// installation, then relaunches the application. This class does all of its
/// work asynchronously; construct one, connect to its signals, and call
/// Start().
///
/// Supported platforms:
///  - Windows: extracts the downloaded .zip and uses a small detached batch
///    script to replace the install directory once the app has exited, then
///    relaunches it.
///  - macOS: extracts the downloaded .zip (containing the .app bundle) and
///    uses a detached shell script to replace the installed .app bundle once
///    the app has exited, then relaunches it.
///  - Linux (AppImage only): downloads the new AppImage and atomically
///    replaces the running AppImage file in place (no restart of other
///    processes required; the running instance keeps working off its old
///    inode until it next restarts).
///
/// Any other configuration (e.g. Linux installed from a distro package) is
/// not self-updatable; CanSelfUpdate() will return false and callers should
/// fall back to opening the release page in a browser.
class SelfUpdater : public QObject {
    Q_OBJECT

public:
    explicit SelfUpdater(QObject* parent = nullptr);
    ~SelfUpdater() override;

    /// Begins downloading and applying the given asset. Safe to call once per
    /// instance. All further progress is reported via the signals below.
    void Start(const UpdateChecker::ReleaseAsset& asset);

    /// Cancels an in-progress download, if any. Has no effect once the
    /// update has begun being applied (i.e. after DownloadFinished has been
    /// emitted).
    void Cancel();

signals:
    /// Emitted periodically while the asset is downloading.
    void DownloadProgress(qint64 bytes_received, qint64 bytes_total);

    /// Emitted once the downloaded asset has been fully applied and the
    /// helper process/relaunch has been kicked off. The receiver should quit
    /// the application shortly after receiving this signal.
    void ReadyToRestart();

    /// Emitted if anything goes wrong at any stage. `message` is suitable
    /// for display to the user.
    void Failed(QString message);

private:
    void DownloadAndApply(UpdateChecker::ReleaseAsset asset);
    bool ApplyWindows(const QString& downloaded_path, QString& error);
    bool ApplyMacOS(const QString& downloaded_path, QString& error);
    bool ApplyLinuxAppImage(const QString& downloaded_path, QString& error);

    bool cancel_requested = false;
};

} // namespace Updater
