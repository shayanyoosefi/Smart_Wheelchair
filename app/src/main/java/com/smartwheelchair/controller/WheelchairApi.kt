package com.smartwheelchair.controller

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

class WheelchairApi(
    private val baseUrl: String = "http://192.168.4.1"
) {
    private val client = OkHttpClient.Builder()
        .connectTimeout(1500, TimeUnit.MILLISECONDS)
        .readTimeout(1500, TimeUnit.MILLISECONDS)
        .writeTimeout(1500, TimeUnit.MILLISECONDS)
        .build()

    suspend fun drive(x: Float, y: Float): Result<String> = get("/drive?x=$x&y=$y")
    suspend fun stop(): Result<String> = drive(0f, 0f)
    suspend fun ledOn(): Result<String> = get("/on")
    suspend fun ledOff(): Result<String> = get("/off")
    suspend fun ping(): Result<String> = get("/")

    private suspend fun get(path: String): Result<String> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$baseUrl$path")
                .get()
                .build()
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) {
                    return@withContext Result.failure(IOException("HTTP ${response.code}"))
                }
                Result.success(response.body?.string().orEmpty())
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }
}
