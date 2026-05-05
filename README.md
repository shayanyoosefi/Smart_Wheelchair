# Smart Wheelchair Controller

This repository contains:

- `WheelchairRFID.ino`: ESP32 firmware for wheelchair control, RFID/keypad access, LCD feedback, and safety watchdog logic.
- `app/`: Android controller app (Kotlin + Jetpack Compose) for drive and LED control over Wi-Fi.

The system is designed so the Android app connects directly to the ESP32 over Wi-Fi, without manual IP entry in the app UI.

## Architecture

### ESP32 side

- Runs as a Wi-Fi Access Point (AP):
  - SSID: `Wheelchair_CTRL`
  - Password: `wheelchair123`
  - AP IP: `192.168.4.1`
- Hosts an HTTP server with endpoints:
  - `/drive?x=<float>&y=<float>` for motion commands
  - `/on` and `/off` for LED control
  - `/` for basic connectivity check
- Includes:
  - drive ramping
  - drive watchdog timeout / emergency stop behavior
  - RFID allowlist check
  - keypad password + lockout logic
  - LCD + buzzer feedback

### Android side

- Connects to ESP32 endpoint: `http://192.168.4.1`
- UI includes:
  - Hold-to-drive buttons (`FORWARD`, `BACK`, `LEFT`, `RIGHT`)
  - `STOP` button
  - `LED ON` / `LED OFF` buttons
  - status text
  - shortcut button to open Android Wi-Fi settings
- Sends drive updates every ~150 ms while a direction is held.

## Project Structure

- `WheelchairRFID.ino` - ESP32 firmware
- `settings.gradle.kts`, `build.gradle.kts`, `gradle.properties` - Android project root config
- `app/build.gradle.kts` - Android app module config
- `app/src/main/java/com/smartwheelchair/controller/MainActivity.kt` - main Compose UI + controls
- `app/src/main/java/com/smartwheelchair/controller/WheelchairApi.kt` - HTTP API client for ESP32 endpoints

## Requirements

- ESP32 board (with your connected wheelchair hardware, LCD, RFID, keypad, buzzer as wired in firmware)
- Arduino IDE (or PlatformIO) with required libraries for the firmware
- Android SDK command-line tools (or Android Studio)
- Android phone connected to ESP32 Wi-Fi AP
- JDK 17 or JDK 21 (recommended for Android Gradle Plugin compatibility)

## Flash Firmware (`WheelchairRFID.ino`)

1. Open `WheelchairRFID.ino` in Arduino IDE.
2. Install required libraries if missing:
   - `WiFi` (ESP32 core)
   - `WebServer` (ESP32 core)
   - `Keypad`
   - `LiquidCrystal_I2C`
   - `MFRC522`
3. Select your ESP32 board and COM port.
4. Upload firmware.
5. Confirm Serial Monitor shows AP startup and IP information.

## Build and Run Android App

1. Open project folder in Android Studio.
2. Wait for Gradle sync.
3. Connect an Android phone or start emulator.
4. Run the `app` configuration.
5. In app, tap **Open Wi-Fi Settings** and connect phone to:
   - SSID: `Wheelchair_CTRL`
   - Password: `wheelchair123`
6. Return to app and test controls.

## CLI Build (No Android Studio)

1. Ensure Java is set to JDK 17 or 21:
   - `java -version`
2. Generate or refresh wrapper:
   - `gradle wrapper`
3. Build debug APK:
   - `./gradlew assembleDebug`
4. Output APK:
   - `app/build/outputs/apk/debug/app-debug.apk`

## Generate APK

### Debug APK

From Android Studio:

- `Build` -> `Build Bundle(s) / APK(s)` -> `Build APK(s)`
- Output file:
  - `app/build/outputs/apk/debug/app-debug.apk`

### Release APK (signed)

From Android Studio:

- `Build` -> `Generate Signed Bundle / APK`
- Choose `APK`
- Create/select keystore
- Choose `release`
- Output file:
  - `app/build/outputs/apk/release/app-release.apk`

## Troubleshooting Build Failures

If you see:

- `Plugin [id: 'com.android.application', version: '8.8.2'] was not found`

it means Gradle could not download the Android Gradle Plugin from remote repositories.

What to check:

1. verify internet/DNS:
   - `curl -I https://dl.google.com`
   - `curl -I https://repo.maven.apache.org`
2. if you use a proxy, export it before build:
   - `export HTTPS_PROXY=http://<host>:<port>`
   - `export HTTP_PROXY=http://<host>:<port>`
3. retry with details:
   - `gradle wrapper --refresh-dependencies --stacktrace`

## Toolchain Baseline (Migration Path)

The Android project has been migrated to a newer baseline:

- Android Gradle Plugin: `8.8.2`
- Kotlin: `2.0.21`
- Gradle wrapper distribution: `8.10.2`
- Compile/Target SDK: `35`

Why this baseline:

- modern but stable Android toolchain
- better compatibility with current Compose and dependency versions
- cleaner path for future upgrades

## Safety Notes

- Always test with wheels raised/off-ground before real driving.
- Keep emergency stop physically accessible.
- Verify watchdog stop behavior before field use.
- Network endpoints are currently plain HTTP and unauthenticated; add token/auth before production deployment.

## GitHub Push (after creating remote repo)

```bash
git remote add origin https://github.com/<your-username>/Smart_Wheelchair.git
git push -u origin main
```

## Maintenance Rule

This project follows a simple maintenance rule:

- keep `.gitignore` updated whenever new generated files or secrets appear
- keep `README.md` updated whenever setup steps, architecture, or behavior changes

Why this matters:

- avoids committing build artifacts and credentials
- keeps onboarding and deployment steps accurate
