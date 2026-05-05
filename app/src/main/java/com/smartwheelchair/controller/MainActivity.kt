package com.smartwheelchair.controller

import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    ControllerScreen()
                }
            }
        }
    }
}

@Composable
private fun ControllerScreen() {
    val api = remember { WheelchairApi() }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    var status by remember { mutableStateOf("Status: ready") }
    var activeDriveJob by remember { mutableStateOf<Job?>(null) }

    fun startDriveHold(x: Float, y: Float, label: String) {
        activeDriveJob?.cancel()
        activeDriveJob = scope.launch {
            while (true) {
                val result = api.drive(x, y)
                status = if (result.isSuccess) {
                    "Driving: $label"
                } else {
                    "Drive error: ${result.exceptionOrNull()?.message ?: "unknown"}"
                }
                delay(150)
            }
        }
    }

    fun stopDrive() {
        activeDriveJob?.cancel()
        activeDriveJob = null
        scope.launch {
            val result = api.stop()
            status = if (result.isSuccess) "Stopped" else "Stop error: ${result.exceptionOrNull()?.message}"
        }
    }

    LaunchedEffect(Unit) {
        val ping = api.ping()
        status = if (ping.isSuccess) "Connected to ESP32 (192.168.4.1)" else "Not connected to ESP32 Wi-Fi"
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text("Smart Wheelchair Controller", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text("Connect to Wi-Fi: Wheelchair_CTRL, then press and hold to move.")
        Text(status, color = MaterialTheme.colorScheme.primary)

        Button(
            onClick = {
                val intent = Intent(Settings.ACTION_WIFI_SETTINGS).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                context.startActivity(intent)
            }
        ) {
            Text("Open Wi-Fi Settings")
        }

        Spacer(modifier = Modifier.height(8.dp))

        HoldButton(
            label = "FORWARD",
            onHoldStart = { startDriveHold(0f, 1f, "FORWARD") },
            onHoldEnd = { stopDrive() },
            modifier = Modifier.fillMaxWidth(0.7f),
            color = MaterialTheme.colorScheme.tertiary
        )

        Row(
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            HoldButton(
                label = "LEFT",
                onHoldStart = { startDriveHold(-1f, 0f, "LEFT") },
                onHoldEnd = { stopDrive() },
                modifier = Modifier.weight(1f),
                color = MaterialTheme.colorScheme.secondary
            )
            Button(
                onClick = { stopDrive() },
                modifier = Modifier.weight(1f),
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
            ) {
                Text("STOP")
            }
            HoldButton(
                label = "RIGHT",
                onHoldStart = { startDriveHold(1f, 0f, "RIGHT") },
                onHoldEnd = { stopDrive() },
                modifier = Modifier.weight(1f),
                color = MaterialTheme.colorScheme.secondary
            )
        }

        HoldButton(
            label = "BACK",
            onHoldStart = { startDriveHold(0f, -1f, "BACK") },
            onHoldEnd = { stopDrive() },
            modifier = Modifier.fillMaxWidth(0.7f),
            color = MaterialTheme.colorScheme.tertiary
        )

        Spacer(modifier = Modifier.height(8.dp))

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(
                onClick = {
                    scope.launch {
                        val result = api.ledOn()
                        status = if (result.isSuccess) "LED ON" else "LED ON error: ${result.exceptionOrNull()?.message}"
                    }
                }
            ) { Text("LED ON") }

            Button(
                onClick = {
                    scope.launch {
                        val result = api.ledOff()
                        status = if (result.isSuccess) "LED OFF" else "LED OFF error: ${result.exceptionOrNull()?.message}"
                    }
                }
            ) { Text("LED OFF") }
        }
    }
}

@Composable
private fun HoldButton(
    label: String,
    onHoldStart: () -> Unit,
    onHoldEnd: () -> Unit,
    modifier: Modifier = Modifier,
    color: androidx.compose.ui.graphics.Color = MaterialTheme.colorScheme.primary
) {
    Column(
        modifier = modifier
            .background(color = color, shape = RoundedCornerShape(12.dp))
            .pointerInput(Unit) {
                detectTapGestures(
                    onPress = {
                        onHoldStart()
                        tryAwaitRelease()
                        onHoldEnd()
                    }
                )
            }
            .padding(vertical = 18.dp, horizontal = 14.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(text = label, color = MaterialTheme.colorScheme.onPrimary, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.size(2.dp))
        Text(text = "Hold", color = MaterialTheme.colorScheme.onPrimary)
    }
}
