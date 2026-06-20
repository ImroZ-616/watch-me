# ESP32 Custom Smartwatch — Build Guide

## Hardware Wiring Summary

| Signal        | ESP32 GPIO | Peripheral            |
|---------------|------------|-----------------------|
| I2S BCLK      | 14         | INMP441 SCK + MAX98357A BCLK |
| I2S WS / LRC  | 15         | INMP441 WS  + MAX98357A LRC  |
| I2S DIN (Mic) | 32         | INMP441 SD              |
| I2S DOUT(Amp) | 25         | MAX98357A DIN           |
| I2C SDA       | 21         | SSD1306 + MPU6050       |
| I2C SCL       | 22         | SSD1306 + MPU6050       |
| MPU6050 INT   | 12         | Raise-to-Wake interrupt |
| Push-to-Talk  | 33         | Tactile button (GND)    |

> **Shared I2S clocks:** BCLK and WS are physically wired to both the
> mic and amp. Because the mic and amp are never active simultaneously
> (the firmware installs/uninstalls the I2S driver between modes) there
> is no bus conflict.

---

## Arduino Firmware

### Dependencies (Arduino Library Manager)
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- ESP32 built-in: `BluetoothSerial`, `driver/i2s`, `esp_sleep`

### Board settings (Arduino IDE)
- Board: **ESP32 Dev Module**
- Partition Scheme: **Default 4MB with spiffs** (BT needs ~1.5 MB)
- CPU Frequency: 240 MHz
- Flash Mode: QIO

### Build & Upload
```
Open smartwatch.ino in Arduino IDE → Select port → Upload
```

### Deep Sleep / Wake behaviour
- After **30 s** of inactivity the watch sleeps.
- **Raise to wake:** MPU6050 fires its motion interrupt → GPIO 12 goes
  HIGH → ESP32 wakes via `ext0`.
- **PTT wake:** GPIO 33 pulled LOW → ESP32 wakes via `ext1`.

---

## Python Companion Script

### Requirements
```bash
pip install pybluez openai-whisper ollama pyttsx3 \
            numpy soundfile scipy
# Optional (for gTTS fallback):
pip install gtts
# gTTS also needs ffmpeg in PATH for MP3→WAV conversion
```

### Ollama setup
```bash
# Install Ollama: https://ollama.com
ollama pull mistral    # or any model you prefer
ollama serve           # keep running in background
```

### Running
```bash
python3 watch_companion.py
```
The script will scan for `ESP32-Watch` over Bluetooth, connect, then
wait for PTT events indefinitely.

### Configuration
Edit the `CONFIG` dict at the top of `watch_companion.py`:

| Key              | Default      | Notes                          |
|------------------|--------------|--------------------------------|
| `bt_device_name` | `ESP32-Watch`| Must match `SerialBT.begin()`  |
| `whisper_model`  | `base`       | `tiny` is faster, less accurate|
| `ollama_model`   | `mistral`    | Must be pulled locally         |
| `pyttsx3_rate`   | `175`        | Words per minute               |
| `tts_volume`     | `1.0`        | Scale output amplitude         |

---

## Data Flow

```
[PTT pressed]
  ESP32 mic (INMP441)
    └─ I2S RX (32-bit frames, 16 kHz)
        └─ Down-shift to 16-bit PCM
            └─ BT Serial stream  ──────────────▶  laptop
                                                    │
                                              Whisper STT
                                                    │
                                            Ollama (Mistral)
                                                    │
                                              pyttsx3 TTS
                                                    │
                                       Raw PCM  ◀──────────
[PTT released → end magic sent]
  ESP32 recv loop
    └─ I2S TX (MAX98357A, 16 kHz)
        └─ Speaker output 🔊
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No audio from speaker | Wrong playback sample rate | Ensure both sides use 16 kHz |
| Scratchy / clipping audio | INMP441 gain too high | Lower `MOT_THR` or add digital attenuation in the down-shift |
| BT not discoverable | BT partition too small | Use "Default 4MB with spiffs" partition |
| MPU6050 never fires | Wrong I2C address | Check AD0 pin; address is 0x68 (AD0=GND) or 0x69 (AD0=VCC) |
| Watch won't sleep | Activity timer not resetting | Ensure `lastActivityMs` updates on all BT events |
| Python `pybluez` install fails | Missing libbluetooth-dev | `sudo apt install libbluetooth-dev` (Linux) |