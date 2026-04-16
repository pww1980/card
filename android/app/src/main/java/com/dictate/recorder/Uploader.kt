package com.dictate.recorder

import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okio.BufferedSink
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * Uploads a WAV file to the dictation server and polls job status.
 * Uses the same /upload and /status/{id} endpoints as the Cardputer.
 */
class Uploader(private val host: String, private val port: Int) {

    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .writeTimeout(10, TimeUnit.MINUTES)   // large files take time
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    /**
     * Uploads [file] as multipart/form-data.
     * [onProgress] is called regularly with (byteSent, totalBytes).
     * Returns the job_id from the server response.
     * Throws IOException on any network or HTTP error.
     */
    fun upload(
        file: File,
        onProgress: (sent: Long, total: Long) -> Unit = { _, _ -> },
    ): String {
        val total = file.length()

        val fileBody = object : RequestBody() {
            override fun contentType() = "audio/wav".toMediaType()
            override fun contentLength() = total
            override fun writeTo(sink: BufferedSink) {
                var sent = 0L
                file.inputStream().use { input ->
                    val buf = ByteArray(8_192)
                    var n: Int
                    while (input.read(buf).also { n = it } != -1) {
                        sink.write(buf, 0, n)
                        sent += n
                        onProgress(sent, total)
                    }
                }
            }
        }

        val body = MultipartBody.Builder()
            .setType(MultipartBody.FORM)
            .addFormDataPart("file", file.name, fileBody)
            .build()

        val request = Request.Builder()
            .url("http://$host:$port/upload")
            .post(body)
            .build()

        client.newCall(request).execute().use { response ->
            val bodyStr = response.body?.string() ?: throw IOException("Leere Antwort")
            if (!response.isSuccessful)
                throw IOException("HTTP ${response.code}: $bodyStr")
            return JSONObject(bodyStr).getString("job_id")
        }
    }

    /**
     * Polls /status/{jobId}.
     * Returns the status string ("queued", "transcribing", "summarizing", "done", "failed")
     * or an error token ("no_conn", "http_NNN") on failure.
     */
    fun pollStatus(jobId: String): String {
        val request = Request.Builder()
            .url("http://$host:$port/status/$jobId")
            .get()
            .build()

        return try {
            client.newCall(request).execute().use { response ->
                val body = response.body?.string() ?: return "no_response"
                if (!response.isSuccessful) return "http_${response.code}"
                JSONObject(body).getString("status")
            }
        } catch (_: Exception) {
            "no_conn"
        }
    }
}
