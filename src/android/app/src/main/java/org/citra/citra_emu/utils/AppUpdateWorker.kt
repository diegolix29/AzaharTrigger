// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.app.NotificationCompat
import androidx.core.content.FileProvider
import androidx.work.Data
import androidx.work.ForegroundInfo
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import org.citra.citra_emu.R

/**
 * Downloads an update APK (found by [AppUpdater]) in the background, showing
 * progress via a notification, then hands it off to the system package
 * installer.
 *
 * Only used in the vanilla (non-googlePlay) flavor; see
 * [BuildUtil.isGooglePlayBuild].
 */
class AppUpdateWorker(private val context: Context, params: WorkerParameters) :
    Worker(context, params) {

    private val notificationManager = context.getSystemService(NotificationManager::class.java)
    private val notificationId = 0xA9DA7E
    private var lastNotifiedTime = 0L

    private val progressBuilder = NotificationCompat.Builder(
        context,
        context.getString(R.string.app_update_notification_channel_id)
    )
        .setContentTitle(context.getString(R.string.app_update_notification_title))
        .setSmallIcon(R.drawable.ic_stat_notification_logo)
        .setOnlyAlertOnce(true)
        .setOngoing(true)

    override fun doWork(): Result {
        val downloadUrl = inputData.getString(KEY_DOWNLOAD_URL) ?: return Result.failure()
        val fileName = inputData.getString(KEY_FILE_NAME) ?: return Result.failure()

        progressBuilder.setProgress(100, 0, true)
        notificationManager.notify(notificationId, progressBuilder.build())

        val updatesDir = File(context.getExternalFilesDir(null), "updates").apply { mkdirs() }
        val outputFile = File(updatesDir, fileName)

        val downloaded = try {
            downloadTo(downloadUrl, outputFile)
        } catch (e: Exception) {
            false
        }

        if (!downloaded) {
            outputFile.delete()
            notifyFailure()
            return Result.failure()
        }

        notifyReadyToInstall(outputFile)
        promptInstall(outputFile)
        return Result.success()
    }

    private fun downloadTo(downloadUrl: String, outputFile: File): Boolean {
        val connection = URL(downloadUrl).openConnection() as HttpURLConnection
        connection.instanceFollowRedirects = true
        connection.connectTimeout = 15_000
        connection.readTimeout = 30_000
        try {
            connection.connect()
            if (connection.responseCode !in 200..299) {
                return false
            }

            val totalBytes = connection.contentLengthLong
            var bytesRead = 0L

            connection.inputStream.use { input ->
                outputFile.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (!isStopped) {
                        val read = input.read(buffer)
                        if (read == -1) break
                        output.write(buffer, 0, read)
                        bytesRead += read
                        reportProgress(bytesRead, totalBytes)
                    }
                }
            }
            return !isStopped
        } finally {
            connection.disconnect()
        }
    }

    private fun reportProgress(current: Long, total: Long) {
        val now = System.currentTimeMillis()
        // Notification updates are rate-limited by the system if posted too
        // frequently; avoid spamming them.
        if (now - lastNotifiedTime < 500) return
        lastNotifiedTime = now

        if (total > 0) {
            progressBuilder.setProgress(100, ((current * 100) / total).toInt(), false)
        } else {
            progressBuilder.setProgress(0, 0, true)
        }
        setProgressAsync(
            Data.Builder()
                .putLong(KEY_PROGRESS_CURRENT, current)
                .putLong(KEY_PROGRESS_TOTAL, total)
                .build()
        )
        notificationManager.notify(notificationId, progressBuilder.build())
    }

    private fun apkUri(file: File): Uri = FileProvider.getUriForFile(
        context,
        "${context.packageName}.fileprovider",
        file
    )

    private fun installIntent(file: File): Intent {
        val uri = apkUri(file)
        return Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
    }

    private fun notifyReadyToInstall(file: File) {
        val pendingIntent = PendingIntent.getActivity(
            context,
            0,
            installIntent(file),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val notification = NotificationCompat.Builder(
            context,
            context.getString(R.string.app_update_notification_channel_id)
        )
            .setContentTitle(context.getString(R.string.app_update_notification_success_title))
            .setContentText(context.getString(R.string.app_update_install_prompt))
            .setSmallIcon(R.drawable.ic_stat_notification_logo)
            .setAutoCancel(true)
            .setContentIntent(pendingIntent)
            .build()
        notificationManager.cancel(notificationId)
        notificationManager.notify(notificationId, notification)
    }

    private fun notifyFailure() {
        val notification = NotificationCompat.Builder(
            context,
            context.getString(R.string.app_update_notification_channel_id)
        )
            .setContentTitle(context.getString(R.string.app_update_notification_error_title))
            .setContentText(context.getString(R.string.app_update_download_failed))
            .setSmallIcon(R.drawable.ic_stat_notification_logo)
            .setAutoCancel(true)
            .build()
        notificationManager.cancel(notificationId)
        notificationManager.notify(notificationId, notification)
    }

    private fun promptInstall(file: File) {
        // Also try to launch the installer right away (in addition to the
        // tap-to-install notification above), in case the app is still in
        // the foreground. If Android blocks the background activity start,
        // the notification is the fallback.
        try {
            context.startActivity(installIntent(file))
        } catch (e: Exception) {
            // Ignored; the notification's PendingIntent still works.
        }
    }

    override fun getForegroundInfo(): ForegroundInfo =
        ForegroundInfo(notificationId, progressBuilder.build())

    companion object {
        private const val KEY_DOWNLOAD_URL = "DOWNLOAD_URL"
        private const val KEY_FILE_NAME = "FILE_NAME"
        private const val KEY_PROGRESS_CURRENT = "PROGRESS_CURRENT"
        private const val KEY_PROGRESS_TOTAL = "PROGRESS_TOTAL"

        /** Enqueues a background download+install of the given update APK. */
        fun enqueue(context: Context, downloadUrl: String, fileName: String) {
            val data = Data.Builder()
                .putString(KEY_DOWNLOAD_URL, downloadUrl)
                .putString(KEY_FILE_NAME, fileName)
                .build()
            val request = OneTimeWorkRequestBuilder<AppUpdateWorker>()
                .setInputData(data)
                .build()
            WorkManager.getInstance(context).enqueue(request)
        }
    }
}
