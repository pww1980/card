package com.dictate.recorder

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {

    private val vm: RecorderViewModel by viewModels()

    private val micPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) vm.startRecording()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            DiktierTheme {
                Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    MainScreen(vm = vm, onRequestMic = ::requestMic)
                }
            }
        }
    }

    private fun requestMic() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            == PackageManager.PERMISSION_GRANTED) {
            vm.startRecording()
        } else {
            micPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }
}

// ── Root screen ───────────────────────────────────────────────────────────────

@Composable
private fun MainScreen(vm: RecorderViewModel, onRequestMic: () -> Unit) {
    val ui by vm.ui.collectAsState()
    var showSettings by remember { mutableStateOf(false) }

    // Show transient error as snackbar
    val snackbarHost = remember { SnackbarHostState() }
    val err = ui.errorMessage
    LaunchedEffect(err) {
        if (err.isNotEmpty()) snackbarHost.showSnackbar(err)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Diktiergerät") },
                actions = {
                    if (ui.screen == Screen.IDLE || ui.screen == Screen.CONFIRM) {
                        IconButton(onClick = { showSettings = true }) {
                            Icon(Icons.Default.Settings, contentDescription = "Einstellungen")
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor       = DarkBlue,
                    titleContentColor    = Color.White,
                    actionIconContentColor = Color.White,
                ),
            )
        },
        snackbarHost = { SnackbarHost(snackbarHost) },
    ) { padding ->
        Box(Modifier.padding(padding).fillMaxSize()) {
            when (ui.screen) {
                Screen.IDLE      -> IdleScreen(onRecord = onRequestMic)
                Screen.RECORDING -> RecordingScreen(ui, onStop = vm::stopRecording)
                Screen.CONFIRM   -> ConfirmScreen(
                    filePath  = ui.lastFilePath,
                    onUpload  = vm::confirmUpload,
                    onDiscard = vm::discardRecording,
                )
                Screen.UPLOADING -> UploadingScreen(ui.uploadProgress)
                Screen.POLLING   -> PollingScreen(
                    jobId    = ui.jobId,
                    status   = ui.jobStatus,
                    onReturn = vm::dismissResult,
                )
            }
        }
    }

    if (showSettings) {
        SettingsDialog(
            host      = ui.serverHost,
            port      = ui.serverPort,
            onSave    = { h, p -> vm.saveSettings(h, p); showSettings = false },
            onDismiss = { showSettings = false },
        )
    }
}

// ── Screens ───────────────────────────────────────────────────────────────────

@Composable
private fun IdleScreen(onRecord: () -> Unit) {
    Column(
        Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text("Bereit zur Aufnahme",
             style = MaterialTheme.typography.headlineSmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.height(40.dp))
        RecordButton(isRecording = false, onClick = onRecord)
        Spacer(Modifier.height(16.dp))
        Text("Tippen zum Starten",
             style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun RecordingScreen(ui: UiState, onStop: () -> Unit) {
    val m = ui.elapsedSec / 60
    val s = ui.elapsedSec % 60

    // Blinking red dot
    var blink by remember { mutableStateOf(true) }
    LaunchedEffect(Unit) { while (true) { delay(600); blink = !blink } }

    Column(
        Modifier.fillMaxSize().padding(horizontal = 24.dp, vertical = 32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        // Header: blink indicator
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier.size(10.dp)
                    .background(
                        if (blink) RecRed else Color.Transparent,
                        CircleShape,
                    )
            )
            Spacer(Modifier.width(6.dp))
            Text("AUFNAHME", color = RecRed,
                 style = MaterialTheme.typography.labelLarge, letterSpacing = 2.sp)
        }

        // Large timer
        Text(
            text       = "%02d:%02d".format(m, s),
            style      = MaterialTheme.typography.displayLarge,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            color      = Color.White,
        )

        // Level meter
        Column(Modifier.fillMaxWidth()) {
            Text("Pegel  ${ui.level}%",
                 style = MaterialTheme.typography.labelSmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(6.dp))
            LinearProgressIndicator(
                progress     = { ui.level / 100f },
                modifier     = Modifier.fillMaxWidth().height(10.dp),
                color        = levelColor(ui.level),
                trackColor   = MaterialTheme.colorScheme.surfaceVariant,
            )
        }

        // Stop button
        RecordButton(isRecording = true, onClick = onStop)
        Text("Tippen zum Stoppen",
             style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun ConfirmScreen(filePath: String, onUpload: () -> Unit, onDiscard: () -> Unit) {
    val name = filePath.substringAfterLast("/")
    Column(
        Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(Icons.Default.MicNone, contentDescription = null,
             modifier = Modifier.size(64.dp), tint = DarkBlue)
        Spacer(Modifier.height(16.dp))
        Text("Aufnahme abgeschlossen",
             style = MaterialTheme.typography.headlineSmall,
             textAlign = TextAlign.Center)
        Spacer(Modifier.height(8.dp))
        Text(name,
             style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant,
             textAlign = TextAlign.Center)
        Spacer(Modifier.height(40.dp))
        Button(onClick = onUpload, modifier = Modifier.fillMaxWidth().height(52.dp)) {
            Icon(Icons.Default.Send, contentDescription = null)
            Spacer(Modifier.width(8.dp))
            Text("An Server senden")
        }
        Spacer(Modifier.height(12.dp))
        OutlinedButton(onClick = onDiscard, modifier = Modifier.fillMaxWidth()) {
            Text("Verwerfen")
        }
    }
}

@Composable
private fun UploadingScreen(progress: Float) {
    Column(
        Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(Icons.Default.CloudUpload, contentDescription = null,
             modifier = Modifier.size(56.dp), tint = DarkBlue)
        Spacer(Modifier.height(20.dp))
        Text("Übertrage Aufnahme...", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(28.dp))
        LinearProgressIndicator(
            progress  = { progress },
            modifier  = Modifier.fillMaxWidth().height(8.dp),
            color     = DarkBlue,
            trackColor = MaterialTheme.colorScheme.surfaceVariant,
        )
        Spacer(Modifier.height(8.dp))
        Text("%.0f%%".format(progress * 100),
             style = MaterialTheme.typography.bodyMedium,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun PollingScreen(jobId: String, status: String, onReturn: () -> Unit) {
    val (color, label) = when (status) {
        "queued"       -> Color(0xFF9E9E9E) to "In Warteschlange..."
        "transcribing" -> Color(0xFFFFC107) to "Transkribiere..."
        "summarizing"  -> Color(0xFFFF9800) to "Erstelle Zusammenfassung..."
        "done"         -> Color(0xFF4CAF50) to "Verarbeitung abgeschlossen!"
        "failed"       -> RecRed           to "Fehler auf dem Server"
        else           -> Color(0xFF9E9E9E) to status
    }
    val isDone = status == "done" || status == "failed"

    Column(
        Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        // Status badge
        Surface(
            color  = color.copy(alpha = 0.15f),
            shape  = RoundedCornerShape(12.dp),
        ) {
            Text(
                text      = statusIcon(status),
                modifier  = Modifier.padding(horizontal = 20.dp, vertical = 12.dp),
                style     = MaterialTheme.typography.headlineMedium,
                fontFamily = FontFamily.Monospace,
                color     = color,
            )
        }
        Spacer(Modifier.height(16.dp))
        Text(label, style = MaterialTheme.typography.titleMedium, color = color,
             textAlign = TextAlign.Center)
        Spacer(Modifier.height(8.dp))
        Text("Job: $jobId",
             style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.height(24.dp))

        if (!isDone) {
            CircularProgressIndicator(color = DarkBlue)
            Spacer(Modifier.height(8.dp))
            Text("Nächste Statusabfrage in 60 s",
                 style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(24.dp))
        }

        OutlinedButton(onClick = onReturn, modifier = Modifier.fillMaxWidth()) {
            Text(if (isDone) "OK" else "Zurück zum Hauptmenü (Job läuft weiter)")
        }
    }
}

// ── Reusable composables ──────────────────────────────────────────────────────

@Composable
private fun RecordButton(isRecording: Boolean, onClick: () -> Unit) {
    FloatingActionButton(
        onClick          = onClick,
        modifier         = Modifier.size(88.dp),
        shape            = CircleShape,
        containerColor   = if (isRecording) Color.DarkGray else RecRed,
        contentColor     = Color.White,
    ) {
        Icon(
            imageVector     = if (isRecording) Icons.Default.Stop else Icons.Default.Mic,
            contentDescription = if (isRecording) "Stoppen" else "Aufnahme starten",
            modifier        = Modifier.size(40.dp),
        )
    }
}

@Composable
private fun SettingsDialog(
    host: String, port: Int,
    onSave: (String, Int) -> Unit,
    onDismiss: () -> Unit,
) {
    var h by remember { mutableStateOf(host) }
    var p by remember { mutableStateOf(port.toString()) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Server-Einstellungen") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value          = h,
                    onValueChange  = { h = it },
                    label          = { Text("Server-IP") },
                    modifier       = Modifier.fillMaxWidth(),
                    singleLine     = true,
                )
                OutlinedTextField(
                    value          = p,
                    onValueChange  = { p = it.filter(Char::isDigit) },
                    label          = { Text("Port") },
                    modifier       = Modifier.fillMaxWidth(),
                    singleLine     = true,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onSave(h.trim(), p.toIntOrNull() ?: 8000) }) {
                Text("Speichern")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Abbrechen") }
        },
    )
}

// ── Helpers ───────────────────────────────────────────────────────────────────

private fun statusIcon(status: String) = when (status) {
    "queued"       -> "[ ]"
    "transcribing" -> "[T]"
    "summarizing"  -> "[S]"
    "done"         -> "[✓]"
    "failed"       -> "[!]"
    else           -> "[?]"
}

@Composable
private fun levelColor(level: Int) = when {
    level < 70 -> Color(0xFF4CAF50)
    level < 90 -> Color(0xFFFFC107)
    else       -> RecRed
}

// ── Theme ─────────────────────────────────────────────────────────────────────

private val DarkBlue = Color(0xFF1565C0)
private val RecRed   = Color(0xFFE53935)

@Composable
private fun DiktierTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary         = DarkBlue,
            onPrimary       = Color.White,
            secondary       = Color(0xFF42A5F5),
            error           = RecRed,
            background      = Color(0xFF121212),
            surface         = Color(0xFF1E1E1E),
            onBackground    = Color.White,
            onSurface       = Color.White,
        ),
        content = content,
    )
}
