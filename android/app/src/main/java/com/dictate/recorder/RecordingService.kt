package com.dictate.recorder

import android.app.*
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.*
import androidx.core.app.NotificationCompat

/**
 * Foreground service that keeps the microphone alive when the app is backgrounded.
 * Without this, Android will kill the process after ~1 minute during a 2-hour meeting.
 */
class RecordingService : Service() {

    companion object {
        const val ACTION_START      = "START"
        const val ACTION_STOP       = "STOP"
        const val CHANNEL_ID        = "recording"
        const val NOTIFICATION_ID   = 1
    }

    val recorder = AudioRecorder()

    private val handler  = Handler(Looper.getMainLooper())
    private val binder   = LocalBinder()
    private var wakeLock: PowerManager.WakeLock? = null

    inner class LocalBinder : Binder() {
        fun getService() = this@RecordingService
    }

    override fun onBind(intent: Intent?) = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        val wm = getSystemService(POWER_SERVICE) as PowerManager
        wakeLock = wm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DiktierApp:recording")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val path = intent.getStringExtra("path") ?: return START_NOT_STICKY
                startForeground(
                    NOTIFICATION_ID,
                    buildNotification("Aufnahme läuft..."),
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE,
                )
                wakeLock?.acquire(4 * 60 * 60 * 1_000L)  // max 4 h
                recorder.start(java.io.File(path))
                tickNotification()
            }
            ACTION_STOP -> {
                recorder.stop()
                wakeLock?.takeIf { it.isHeld }?.release()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }
        return START_NOT_STICKY
    }

    /** Updates the notification every second with the current elapsed time. */
    private fun tickNotification() {
        handler.post(object : Runnable {
            override fun run() {
                if (!recorder.isRecording) return
                val sec = recorder.elapsedSeconds
                val nm  = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
                nm.notify(NOTIFICATION_ID,
                    buildNotification("Aufnahme: %02d:%02d".format(sec / 60, sec % 60)))
                handler.postDelayed(this, 1_000)
            }
        })
    }

    private fun buildNotification(text: String): Notification {
        val stopPi = PendingIntent.getService(
            this, 0,
            Intent(this, RecordingService::class.java).apply { action = ACTION_STOP },
            PendingIntent.FLAG_IMMUTABLE,
        )
        val openPi = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Diktiergerät")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentIntent(openPi)
            .addAction(android.R.drawable.ic_media_pause, "Stopp", stopPi)
            .setOngoing(true)
            .build()
    }

    private fun createNotificationChannel() {
        val ch = NotificationChannel(
            CHANNEL_ID, "Aufnahme", NotificationManager.IMPORTANCE_LOW
        ).apply { description = "Laufende Sprachaufnahme" }
        (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)
            .createNotificationChannel(ch)
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        recorder.stop()
        wakeLock?.takeIf { it.isHeld }?.release()
        super.onDestroy()
    }
}
