package com.dictate.recorder

import android.app.Application
import android.content.*
import android.os.IBinder
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

enum class Screen { IDLE, RECORDING, CONFIRM, UPLOADING, POLLING }

data class UiState(
    val screen:         Screen  = Screen.IDLE,
    val elapsedSec:     Long    = 0,
    val level:          Int     = 0,
    val uploadProgress: Float   = 0f,
    val jobId:          String  = "",
    val jobStatus:      String  = "",
    val errorMessage:   String  = "",
    val lastFilePath:   String  = "",
    val serverHost:     String  = "192.168.1.100",
    val serverPort:     Int     = 8000,
)

class RecorderViewModel(app: Application) : AndroidViewModel(app) {

    private val prefs = app.getSharedPreferences("dictate", Context.MODE_PRIVATE)
    private val _ui   = MutableStateFlow(UiState(
        serverHost = prefs.getString("host", "192.168.1.100") ?: "192.168.1.100",
        serverPort = prefs.getInt("port", 8000),
    ))
    val ui: StateFlow<UiState> = _ui.asStateFlow()

    private var recordService: RecordingService? = null
    private var uiPollJob: Job? = null
    private var serverPollJob: Job? = null

    // ── Service binding ───────────────────────────────────────────────────────

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            recordService = (binder as RecordingService.LocalBinder).getService()
            startLevelUpdates()
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            recordService = null
        }
    }

    // ── Recording ─────────────────────────────────────────────────────────────

    fun startRecording() {
        val ctx  = getApplication<Application>()
        val name = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
        // External files dir survives app reinstalls and is accessible without FileProvider
        val file = File(ctx.getExternalFilesDir(null), "$name.wav")

        ctx.startForegroundService(
            Intent(ctx, RecordingService::class.java).apply {
                action = RecordingService.ACTION_START
                putExtra("path", file.absolutePath)
            }
        )
        ctx.bindService(
            Intent(ctx, RecordingService::class.java),
            serviceConnection,
            Context.BIND_AUTO_CREATE,
        )
        _ui.update { it.copy(
            screen       = Screen.RECORDING,
            lastFilePath = file.absolutePath,
            elapsedSec   = 0,
            level        = 0,
        )}
    }

    fun stopRecording() {
        uiPollJob?.cancel()
        val ctx = getApplication<Application>()
        ctx.startService(Intent(ctx, RecordingService::class.java).apply {
            action = RecordingService.ACTION_STOP
        })
        try { ctx.unbindService(serviceConnection) } catch (_: IllegalArgumentException) {}
        recordService = null
        _ui.update { it.copy(screen = Screen.CONFIRM) }
    }

    // ── Upload ────────────────────────────────────────────────────────────────

    fun confirmUpload() {
        _ui.update { it.copy(screen = Screen.UPLOADING, uploadProgress = 0f, errorMessage = "") }
        val state = _ui.value

        viewModelScope.launch(Dispatchers.IO) {
            try {
                val uploader = Uploader(state.serverHost, state.serverPort)
                val jobId    = uploader.upload(File(state.lastFilePath)) { sent, total ->
                    val pct = if (total > 0) sent.toFloat() / total else 0f
                    _ui.update { it.copy(uploadProgress = pct) }
                }
                _ui.update { it.copy(
                    screen    = Screen.POLLING,
                    jobId     = jobId,
                    jobStatus = "queued",
                )}
                startServerPolling(uploader, jobId)
            } catch (e: Exception) {
                _ui.update { it.copy(
                    screen       = Screen.IDLE,
                    errorMessage = e.message ?: "Upload fehlgeschlagen",
                )}
            }
        }
    }

    fun discardRecording() {
        // Delete the recorded file and go back to idle
        val path = _ui.value.lastFilePath
        if (path.isNotEmpty()) File(path).delete()
        _ui.update { it.copy(screen = Screen.IDLE, lastFilePath = "") }
    }

    fun dismissResult() {
        serverPollJob?.cancel()
        _ui.update { it.copy(screen = Screen.IDLE) }
    }

    // ── Settings ──────────────────────────────────────────────────────────────

    fun saveSettings(host: String, port: Int) {
        prefs.edit().putString("host", host).putInt("port", port).apply()
        _ui.update { it.copy(serverHost = host, serverPort = port) }
    }

    // ── Internal ──────────────────────────────────────────────────────────────

    /** Refreshes level meter and elapsed time at ~5 Hz while recording. */
    private fun startLevelUpdates() {
        uiPollJob = viewModelScope.launch {
            while (isActive) {
                val svc = recordService ?: break
                _ui.update { it.copy(
                    elapsedSec = svc.recorder.elapsedSeconds,
                    level      = svc.recorder.peakLevel,
                )}
                delay(200)
            }
        }
    }

    /** Polls the server every 60 s for job status. Auto-stops after 10 min. */
    private fun startServerPolling(uploader: Uploader, jobId: String) {
        serverPollJob?.cancel()
        serverPollJob = viewModelScope.launch(Dispatchers.IO) {
            // First poll immediately so user gets feedback without waiting 60 s
            delay(2_000)
            val deadline = System.currentTimeMillis() + 10 * 60 * 1_000L
            while (isActive && System.currentTimeMillis() < deadline) {
                val status = uploader.pollStatus(jobId)
                _ui.update { it.copy(jobStatus = status) }
                if (status == "done" || status == "failed") break
                delay(60_000)
            }
        }
    }

    override fun onCleared() {
        uiPollJob?.cancel()
        serverPollJob?.cancel()
        super.onCleared()
    }
}
