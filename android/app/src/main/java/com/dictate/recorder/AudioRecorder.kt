package com.dictate.recorder

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/**
 * Records 16 kHz / mono / 16-bit PCM to a WAV file.
 * Matches the Cardputer's audio spec so the server processes both identically.
 *
 * Thread safety: start/stop from any thread; internal recording runs on its own thread.
 */
class AudioRecorder {

    companion object {
        const val SAMPLE_RATE      = 16_000
        const val CHANNEL_CONFIG   = AudioFormat.CHANNEL_IN_MONO
        const val AUDIO_FORMAT     = AudioFormat.ENCODING_PCM_16BIT
        const val BYTES_PER_FRAME  = 2   // 16-bit mono
    }

    private var audioRecord: AudioRecord? = null
    private var thread: Thread?           = null

    @Volatile var isRecording = false; private set
    @Volatile var peakLevel   = 0;     private set   // 0–100
    @Volatile var dataBytes   = 0L;    private set   // bytes written (excl. header)

    val elapsedSeconds: Long get() = dataBytes / (SAMPLE_RATE * BYTES_PER_FRAME)

    /** Starts recording to [outputFile]. Returns false if mic cannot be opened. */
    fun start(outputFile: File): Boolean {
        if (isRecording) return false

        val minBuf  = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
        val bufSize = maxOf(minBuf, 8192)

        val rec = AudioRecord(
            MediaRecorder.AudioSource.MIC,
            SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT,
            bufSize,
        )
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            return false
        }

        outputFile.parentFile?.mkdirs()
        // Write 44-byte placeholder header; PCM data follows immediately.
        RandomAccessFile(outputFile, "rw").use { it.write(ByteArray(44)) }

        dataBytes   = 0
        isRecording = true
        audioRecord = rec
        rec.startRecording()

        thread = Thread {
            val shortBuf = ShortArray(bufSize / 2)
            RandomAccessFile(outputFile, "rw").use { raf ->
                raf.seek(44)
                while (isRecording) {
                    val read = rec.read(shortBuf, 0, shortBuf.size)
                    if (read <= 0) continue

                    var peak = 0
                    val bytes = ByteArray(read * 2)
                    for (i in 0 until read) {
                        val s = shortBuf[i].toInt()
                        if (abs(s) > peak) peak = abs(s)
                        // Little-endian PCM
                        bytes[i * 2]     = (s and 0xFF).toByte()
                        bytes[i * 2 + 1] = ((s shr 8) and 0xFF).toByte()
                    }
                    peakLevel  = (peak.toLong() * 100 / 32_767).toInt()
                    raf.write(bytes)
                    dataBytes += bytes.size
                }
                finaliseHeader(raf, dataBytes)
            }
        }.also { it.start() }

        return true
    }

    /** Stops recording and writes the final WAV header. Blocks until the I/O thread exits. */
    fun stop() {
        isRecording = false
        thread?.join(3_000)
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
    }

    // ── WAV header ────────────────────────────────────────────────────────────

    private fun finaliseHeader(raf: RandomAccessFile, dataSize: Long) {
        raf.seek(0)
        val buf = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray())
            putInt((dataSize + 36).toInt())          // chunkSize
            put("WAVE".toByteArray())
            put("fmt ".toByteArray())
            putInt(16)                               // fmtSize
            putShort(1)                              // PCM
            putShort(1)                              // mono
            putInt(SAMPLE_RATE)
            putInt(SAMPLE_RATE * BYTES_PER_FRAME)   // byteRate
            putShort(BYTES_PER_FRAME.toShort())      // blockAlign
            putShort(16)                             // bitsPerSample
            put("data".toByteArray())
            putInt(dataSize.toInt())
        }
        raf.write(buf.array())
    }
}
