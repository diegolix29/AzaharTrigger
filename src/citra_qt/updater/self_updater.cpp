// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdlib>
#include <memory>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <httplib.h>
#include "citra_qt/updater/self_updater.h"
#include "common/logging/log.h"

namespace Updater {

namespace {

// Returns the single top-level directory contained in `parent`, if there is
// exactly one. Our release archives always contain exactly one directory
// (see .ci/pack.sh), so this is how we find our way to the actual payload
// after extraction.
std::optional<QString> FindSoleSubdirectory(const QString& parent) {
    QDir dir(parent);
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.size() != 1) {
        return std::nullopt;
    }
    return dir.filePath(entries.first());
}

} // namespace

bool CanSelfUpdate() {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    return true;
#elif defined(Q_OS_LINUX)
    return std::getenv("APPIMAGE") != nullptr;
#else
    return false;
#endif
}

std::optional<UpdateChecker::ReleaseAsset> PickAssetForThisPlatform(
    const std::vector<UpdateChecker::ReleaseAsset>& assets) {
    auto contains = [](const std::string& haystack, const std::string& needle) {
        return haystack.find(needle) != std::string::npos;
    };
    auto ends_with = [](const std::string& haystack, const std::string& needle) {
        return haystack.size() >= needle.size() &&
               haystack.compare(haystack.size() - needle.size(), needle.size(), needle) == 0;
    };

#if defined(Q_OS_WIN)
    // Prefer the MSVC build; fall back to the MSYS2 build if that's all
    // that's available.
    const UpdateChecker::ReleaseAsset* msys2_asset = nullptr;
    LOG_INFO(Frontend, "Looking for Windows update asset among {} assets", assets.size());
    for (const auto& asset : assets) {
        LOG_INFO(Frontend, "Checking asset: {}", asset.name);
        if (!ends_with(asset.name, ".zip")) {
            continue;
        }
        if (contains(asset.name, "windows-msvc")) {
            LOG_INFO(Frontend, "Found matching MSVC asset: {}", asset.name);
            return asset;
        }
        if (contains(asset.name, "windows-msys2")) {
            LOG_INFO(Frontend, "Found matching MSYS2 asset: {}", asset.name);
            msys2_asset = &asset;
        }
    }
    if (msys2_asset != nullptr) {
        LOG_INFO(Frontend, "Using MSYS2 asset as fallback");
        return *msys2_asset;
    }
    LOG_ERROR(Frontend, "No suitable Windows update asset found");
    return std::nullopt;
#elif defined(Q_OS_MAC)
    for (const auto& asset : assets) {
        if (contains(asset.name, "macos-universal") && ends_with(asset.name, ".zip")) {
            return asset;
        }
    }
    return std::nullopt;
#elif defined(Q_OS_LINUX)
    // Only AppImage builds are self-updatable; pick the variant (Wayland or
    // not) that matches how this AppImage was packaged, based on our own
    // executable name.
    const QString self_path = QString::fromLocal8Bit(std::getenv("APPIMAGE"));
    const bool is_wayland_build = self_path.contains(QStringLiteral("wayland"));

    // Releases can ship the AppImage two different ways depending on how it
    // was packaged: either wrapped in a "*linux-appimage*.tar.gz" archive
    // (via .ci/pack.sh), or as a bare "*.AppImage" file uploaded directly
    // (the current "linux" CI job does this - it never calls pack.sh for
    // the appimage targets). Recognize both so self-update doesn't silently
    // fall back to "open the download page" just because of how a given
    // release happened to be packaged.
    const UpdateChecker::ReleaseAsset* fallback_asset = nullptr;
    for (const auto& asset : assets) {
        const bool is_archived =
            contains(asset.name, "linux-appimage") && ends_with(asset.name, ".tar.gz");
        const bool is_bare_appimage = ends_with(asset.name, ".AppImage");
        if (!is_archived && !is_bare_appimage) {
            continue;
        }
        const bool asset_is_wayland = contains(asset.name, "wayland");
        if (asset_is_wayland == is_wayland_build) {
            return asset;
        }
        // Keep the first plausible asset around in case nothing matches the
        // Wayland/non-Wayland split exactly (e.g. naming drifts again).
        if (fallback_asset == nullptr) {
            fallback_asset = &asset;
        }
    }
    if (fallback_asset != nullptr) {
        return *fallback_asset;
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

SelfUpdater::SelfUpdater(QObject* parent) : QObject(parent) {}

SelfUpdater::~SelfUpdater() = default;

void SelfUpdater::Cancel() {
    cancel_requested = true;
}

void SelfUpdater::Start(const UpdateChecker::ReleaseAsset& asset) {
    // All of the actual work happens off the GUI thread; progress and
    // completion are reported back via queued signal/slot connections, which
    // Qt handles safely across threads automatically.
    (void)QtConcurrent::run([this, asset] { DownloadAndApply(asset); });
}

void SelfUpdater::DownloadAndApply(UpdateChecker::ReleaseAsset asset) {
    QTemporaryDir temp_dir;
    if (!temp_dir.isValid()) {
        emit Failed(tr("Could not create a temporary directory to download the update to."));
        return;
    }
    temp_dir.setAutoRemove(false); // The helper script/AppImage rename needs these files to
                                   // survive past this function returning.

    const QString download_path =
        QDir(temp_dir.path()).filePath(QString::fromStdString(asset.name));

    // Split the asset URL into host and path for httplib.
    const std::string& full_url = asset.browser_download_url;
    LOG_INFO(Frontend, "Downloading update from: {}", full_url);
    const auto scheme_end = full_url.find("://");
    if (scheme_end == std::string::npos) {
        LOG_ERROR(Frontend, "Malformed URL: {}", full_url);
        emit Failed(tr("The update download URL was malformed."));
        return;
    }
    const auto path_start = full_url.find('/', scheme_end + 3);
    const std::string host = full_url.substr(scheme_end + 3, path_start == std::string::npos
                                                                 ? std::string::npos
                                                                 : path_start - scheme_end - 3);
    const std::string path = path_start == std::string::npos ? "/" : full_url.substr(path_start);
    const std::string scheme = full_url.substr(0, scheme_end);
    bool use_https = (scheme == "https");
    LOG_INFO(Frontend, "Scheme: {}, Host: {}, Path: {}", scheme, host, path);

    QFile out_file(download_path);
    if (!out_file.open(QIODevice::WriteOnly)) {
        emit Failed(tr("Could not create the update download file."));
        return;
    }

    httplib::Result result;
    bool write_error = false;

    LOG_INFO(Frontend, "Downloading from: {}{}", host, path);

    if (use_https) {
        httplib::SSLClient ssl_client(host);
        ssl_client.set_connection_timeout(15);
        ssl_client.set_read_timeout(60);
        ssl_client.set_write_timeout(60);
        ssl_client.set_follow_location(true);
        ssl_client.enable_server_certificate_verification(false);
        ssl_client.set_default_headers(
            {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
                            "like Gecko) Chrome/91.0.4472.124 Safari/537.36"},
             {"Accept", "*/*"},
             {"Accept-Encoding", "gzip, deflate, br"},
             {"Connection", "keep-alive"}});

        result = ssl_client.Get(
            path,
            [&](const char* data, std::size_t data_length) {
                if (cancel_requested) {
                    return false;
                }
                const auto written = out_file.write(data, static_cast<qint64>(data_length));
                if (written != static_cast<qint64>(data_length)) {
                    write_error = true;
                    return false;
                }
                return true;
            },
            [&](std::uint64_t current, std::uint64_t total) {
                emit DownloadProgress(static_cast<qint64>(current), static_cast<qint64>(total));
                return !cancel_requested;
            });
    } else {
        httplib::Client http_client(host);
        http_client.set_connection_timeout(15);
        http_client.set_read_timeout(60);
        http_client.set_write_timeout(60);
        http_client.set_follow_location(true);
        http_client.set_default_headers(
            {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
                            "like Gecko) Chrome/91.0.4472.124 Safari/537.36"},
             {"Accept", "*/*"},
             {"Accept-Encoding", "gzip, deflate, br"},
             {"Connection", "keep-alive"}});

        result = http_client.Get(
            path,
            [&](const char* data, std::size_t data_length) {
                if (cancel_requested) {
                    return false;
                }
                const auto written = out_file.write(data, static_cast<qint64>(data_length));
                if (written != static_cast<qint64>(data_length)) {
                    write_error = true;
                    return false;
                }
                return true;
            },
            [&](std::uint64_t current, std::uint64_t total) {
                emit DownloadProgress(static_cast<qint64>(current), static_cast<qint64>(total));
                return !cancel_requested;
            });
    }

    out_file.close();

    if (cancel_requested) {
        QFile::remove(download_path);
        return;
    }
    if (write_error) {
        LOG_ERROR(Frontend, "Write error while downloading update asset from {}", full_url);
        QFile::remove(download_path);
        emit Failed(tr("Failed to write the update file to disk. Please check your disk space and "
                       "permissions."));
        return;
    }
    if (!result) {
        LOG_ERROR(Frontend, "No response from server while downloading from {}", full_url);
        QFile::remove(download_path);
        emit Failed(tr("Failed to connect to the download server. Please check your internet "
                       "connection and try again."));
        return;
    }
    if (result->status >= 400) {
        LOG_ERROR(Frontend, "Server returned error {} while downloading from {}", result->status,
                  full_url);
        LOG_ERROR(Frontend, "Response body: {}", result->body);
        QFile::remove(download_path);
        QString error_msg =
            QString::fromLatin1("Download server returned error %1. Please try again later.")
                .arg(result->status);
        emit Failed(error_msg);
        return;
    }

    QString error;
    bool applied = false;
#if defined(Q_OS_WIN)
    applied = ApplyWindows(download_path, error);
#elif defined(Q_OS_MAC)
    applied = ApplyMacOS(download_path, error);
#elif defined(Q_OS_LINUX)
    applied = ApplyLinuxAppImage(download_path, error);
#else
    error = tr("Automatic updates are not supported on this platform.");
#endif

    if (!applied) {
        emit Failed(error);
        return;
    }

    emit ReadyToRestart();
}

bool SelfUpdater::ApplyWindows(const QString& downloaded_path, QString& error) {
    const QString staging_dir = downloaded_path + QStringLiteral(".extracted");
    QDir().mkpath(staging_dir);

    QProcess extract;
    extract.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"), QStringLiteral("-Command"),
                   QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                       .arg(downloaded_path, staging_dir)});
    if (!extract.waitForStarted() || !extract.waitForFinished(120000) || extract.exitCode() != 0) {
        error = tr("Failed to extract the downloaded update archive.");
        return false;
    }

    const auto payload_dir = FindSoleSubdirectory(staging_dir);
    if (!payload_dir) {
        error = tr("The downloaded update archive did not have the expected layout.");
        return false;
    }

    const QString install_dir = QCoreApplication::applicationDirPath();
    const QString exe_name = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const qint64 pid = QCoreApplication::applicationPid();

    // Write a small batch script that waits for us to exit, mirrors the new
    // files over the install directory, relaunches the app, then deletes
    // itself and the temporary files.
    const QString script_path = downloaded_path + QStringLiteral(".update.bat");
    QFile script(script_path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = tr("Failed to write the update helper script.");
        return false;
    }

    const QString native_payload_dir = QDir::toNativeSeparators(*payload_dir);
    const QString native_install_dir = QDir::toNativeSeparators(install_dir);
    const QString native_staging_dir = QDir::toNativeSeparators(staging_dir);

    QTextStream ts(&script);
    ts << "@echo off\r\n"
       << ":wait\r\n"
       << "tasklist /FI \"PID eq " << pid << "\" | find \"" << pid << "\" >nul\r\n"
       << "if not errorlevel 1 (\r\n"
       << "    timeout /t 1 /nobreak >nul\r\n"
       << "    goto wait\r\n"
       << ")\r\n"
       << "robocopy \"" << native_payload_dir << "\" \"" << native_install_dir
       << "\" /E /IS /IT /R:3 /W:1 >nul\r\n"
       << "start \"\" \"" << native_install_dir << "\\" << exe_name << "\"\r\n"
       << "rmdir /s /q \"" << native_staging_dir << "\"\r\n"
       << "del \"%~f0\"\r\n";
    script.close();

    // /c so the console window closes itself once the script finishes.
    const bool started = QProcess::startDetached(
        QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QDir::toNativeSeparators(script_path)});
    if (!started) {
        error = tr("Failed to launch the update helper script.");
        return false;
    }
    return true;
}

bool SelfUpdater::ApplyMacOS(const QString& downloaded_path, QString& error) {
    const QString staging_dir = downloaded_path + QStringLiteral(".extracted");
    QDir().mkpath(staging_dir);

    QProcess extract;
    extract.start(QStringLiteral("ditto"), {QStringLiteral("-xk"), downloaded_path, staging_dir});
    if (!extract.waitForStarted() || !extract.waitForFinished(120000) || extract.exitCode() != 0) {
        error = tr("Failed to extract the downloaded update archive.");
        return false;
    }

    const auto payload_dir = FindSoleSubdirectory(staging_dir);
    if (!payload_dir) {
        error = tr("The downloaded update archive did not have the expected layout.");
        return false;
    }

    // Find the .app bundle inside the payload directory.
    QDirIterator it(*payload_dir, QStringList{QStringLiteral("*.app")}, QDir::Dirs);
    if (!it.hasNext()) {
        error = tr("The downloaded update archive did not contain an application bundle.");
        return false;
    }
    const QString new_app_bundle = it.next();

    // Walk up from the running executable (.../Azahar.app/Contents/MacOS/azahar)
    // to find the installed .app bundle path.
    QDir bundle_dir(QCoreApplication::applicationDirPath());
    bundle_dir.cdUp(); // Contents
    bundle_dir.cdUp(); // Azahar.app
    const QString installed_app_bundle = bundle_dir.absolutePath();
    const qint64 pid = QCoreApplication::applicationPid();

    const QString script_path = downloaded_path + QStringLiteral(".update.sh");
    QFile script(script_path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = tr("Failed to write the update helper script.");
        return false;
    }
    QTextStream ts(&script);
    ts << "#!/bin/sh\n"
       << "while kill -0 " << pid << " 2>/dev/null; do sleep 1; done\n"
       << "rm -rf \"" << installed_app_bundle << "\"\n"
       << "ditto \"" << new_app_bundle << "\" \"" << installed_app_bundle << "\"\n"
       << "rm -rf \"" << staging_dir << "\"\n"
       << "open \"" << installed_app_bundle << "\"\n"
       << "rm -- \"$0\"\n";
    script.close();
    script.setPermissions(script.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);

    const bool started = QProcess::startDetached(QStringLiteral("/bin/sh"), {script_path});
    if (!started) {
        error = tr("Failed to launch the update helper script.");
        return false;
    }
    return true;
}

bool SelfUpdater::ApplyLinuxAppImage(const QString& downloaded_path, QString& error) {
    const char* running_appimage = std::getenv("APPIMAGE");
    if (running_appimage == nullptr) {
        error = tr("Automatic updates are only supported for AppImage builds on Linux.");
        return false;
    }

    QString new_appimage_path;
    QString staging_dir;
    if (downloaded_path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
        // The asset was a bare AppImage (not wrapped in a tar.gz), so there's
        // nothing to extract - use it as-is.
        new_appimage_path = downloaded_path;
    } else {
        staging_dir = downloaded_path + QStringLiteral(".extracted");
        QDir().mkpath(staging_dir);

        QProcess extract;
        extract.start(QStringLiteral("tar"),
                      {QStringLiteral("-xzf"), downloaded_path, QStringLiteral("-C"), staging_dir});
        if (!extract.waitForStarted() || !extract.waitForFinished(120000) ||
            extract.exitCode() != 0) {
            error = tr("Failed to extract the downloaded update archive.");
            return false;
        }

        const auto payload_dir = FindSoleSubdirectory(staging_dir);
        if (!payload_dir) {
            error = tr("The downloaded update archive did not have the expected layout.");
            return false;
        }

        QDirIterator it(*payload_dir, QStringList{QStringLiteral("*.AppImage")}, QDir::Files);
        if (!it.hasNext()) {
            error = tr("The downloaded update archive did not contain an AppImage.");
            return false;
        }
        new_appimage_path = it.next();
    }

    QFile new_appimage(new_appimage_path);
    // Make it executable.
    new_appimage.setPermissions(new_appimage.permissions() | QFileDevice::ExeOwner |
                                QFileDevice::ExeGroup | QFileDevice::ExeOther);

    const QString target_path = QString::fromLocal8Bit(running_appimage);

    // An AppImage is a single ELF file that the kernel keeps backing via its
    // inode even while it's running (the same trick used to replace a
    // running binary on Linux in general), so we can atomically move the new
    // file over the old path without needing to wait for the current process
    // to exit.
    QFile::remove(target_path + QStringLiteral(".old"));
    if (!QFile::rename(target_path, target_path + QStringLiteral(".old"))) {
        error = tr("Failed to replace the running AppImage (could not move the old file aside).");
        return false;
    }
    if (!QFile::copy(new_appimage_path, target_path)) {
        // Best-effort rollback.
        QFile::rename(target_path + QStringLiteral(".old"), target_path);
        error = tr("Failed to replace the running AppImage (could not write the new file).");
        return false;
    }
    QFile updated(target_path);
    updated.setPermissions(updated.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup |
                           QFileDevice::ExeOther);

    QFile::remove(target_path + QStringLiteral(".old"));
    // staging_dir is empty when the asset was a bare AppImage (nothing was
    // extracted); QDir("") resolves to the current working directory, so
    // guard against recursively deleting that.
    if (!staging_dir.isEmpty()) {
        QDir(staging_dir).removeRecursively();
    }
    QFile::remove(downloaded_path);

    // Relaunch the new AppImage in place of this process.
    QProcess::startDetached(target_path, QCoreApplication::arguments().mid(1));
    return true;
}

} // namespace Updater
