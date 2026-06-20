/**
 * ============================================================
 *  Custom ESP32 Smartwatch Firmware
 *  Hardware:
 *    - SSD1306 0.96" OLED  (I2C: SDA=21, SCL=22)
 *    - MPU6050 IMU         (I2C: SDA=21, SCL=22  INT=GPIO12)
 *    - INMP441 Mic         (I2S: BCLK=14, WS=15, DIN=32)
 *    - MAX98357A Amp        (I2S: BCLK=14, WS=15, DOUT=25)
 *    - Push-to-Talk button (GPIO33, active-LOW w/ internal pull-up)
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <BluetoothSerial.h>
#include <driver/i2s.h>
#include <esp_sleep.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── Pin Definitions ─────────────────────────────────────────
#define I2S_BCLK          14
#define I2S_WS            15
#define I2S_MIC_DIN       32
#define I2S_AMP_DOUT      25

#define MPU6050_INT_PIN   12
#define PTT_BUTTON_PIN    33

// ─── I2S Config ───────────────────────────────────────────────
#define I2S_PORT          I2S_NUM_0
#define SAMPLE_RATE       16000
#define SAMPLE_BITS       16
#define DMA_BUF_COUNT     8
#define DMA_BUF_LEN       512        // samples per DMA buffer

// ─── Audio Streaming ──────────────────────────────────────────
// BT chunk size: keep small to stay responsive
#define BT_AUDIO_CHUNK    512        // bytes per Bluetooth write
// Receive buffer for playback (bytes)
#define RX_PLAYBACK_BUF   4096

// ─── OLED ─────────────────────────────────────────────────────
#define OLED_WIDTH        128
#define OLED_HEIGHT       64
#define OLED_RESET        -1
#define OLED_I2C_ADDR     0x3C

// ─── Deep Sleep ───────────────────────────────────────────────
// Time of inactivity before entering deep sleep (ms)
#define SLEEP_TIMEOUT_MS  30000UL

// ─── Globals ──────────────────────────────────────────────────
BluetoothSerial SerialBT;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// I2S mode tracking so we reconfigure only when direction changes
typedef enum { I2S_MODE_IDLE, I2S_MODE_REC, I2S_MODE_PLAY } i2s_dir_t;
static i2s_dir_t currentI2SMode = I2S_MODE_IDLE;

// Activity timer (reset on button press or BT data received)
static unsigned long lastActivityMs = 0;

// Raw audio DMA buffer shared between record and playback paths
static uint8_t audioBuf[DMA_BUF_LEN * 2]; // 16-bit samples → 2 bytes each

// ─── Function Prototypes ──────────────────────────────────────
void configureI2S_Record();
void configureI2S_Play();
void stopI2S();
void recordAndStream();
void receiveBTAndPlay();
void enterDeepSleep();
void updateDisplay(const char* line1, const char* line2 = "");
void initMPU6050();
bool isPTTPressed();

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // GPIO init
    pinMode(PTT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(MPU6050_INT_PIN, INPUT);

    // ── I2C / OLED ─────────────────────────────────────────
    Wire.begin(21, 22);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println(F("[OLED] init failed"));
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.display();
    }

    updateDisplay("Booting...");

    // ── MPU6050 ────────────────────────────────────────────
    initMPU6050();

    // ── Bluetooth ─────────────────────────────────────────
    if (!SerialBT.begin("ESP32-Watch")) {
        Serial.println(F("[BT] init failed"));
        updateDisplay("BT Init Fail!");
        while (true) delay(1000);
    }
    Serial.println(F("[BT] Discoverable as 'ESP32-Watch'"));

    updateDisplay("Waiting for", "BT connect...");
    lastActivityMs = millis();
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
    bool btConnected = SerialBT.connected();

    // ── Deep Sleep watchdog ─────────────────────────────────
    if (millis() - lastActivityMs > SLEEP_TIMEOUT_MS) {
        enterDeepSleep();
    }

    // ── Push-to-Talk (record → stream) ─────────────────────
    if (isPTTPressed()) {
        lastActivityMs = millis();
        if (btConnected) {
            updateDisplay("Recording...", "PTT active");
            recordAndStream();
        } else {
            updateDisplay("No BT link", "Hold PTT later");
        }
        // Debounce: wait for button release
        while (isPTTPressed()) delay(10);
        updateDisplay("Connected", "Ready");
        return;
    }

    // ── Incoming BT audio → speaker ────────────────────────
    if (btConnected && SerialBT.available()) {
        lastActivityMs = millis();
        updateDisplay("Playing...", "BT audio");
        receiveBTAndPlay();
        updateDisplay("Connected", "Ready");
        return;
    }

    // ── Idle display ───────────────────────────────────────
    if (btConnected) {
        updateDisplay("Connected", "Press PTT");
    } else {
        updateDisplay("Waiting for", "BT connect...");
    }

    delay(100);
}

// ─────────────────────────────────────────────────────────────
//  I2S — Configure for RECORDING  (INMP441 mic)
// ─────────────────────────────────────────────────────────────
void configureI2S_Record() {
    if (currentI2SMode == I2S_MODE_REC) return;
    if (currentI2SMode != I2S_MODE_IDLE) stopI2S();

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 outputs 24-bit in 32-bit frame
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_LEN,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_MIC_DIN
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));

    currentI2SMode = I2S_MODE_REC;
    Serial.println(F("[I2S] Record mode"));
}

// ─────────────────────────────────────────────────────────────
//  I2S — Configure for PLAYBACK  (MAX98357A amp)
// ─────────────────────────────────────────────────────────────
void configureI2S_Play() {
    if (currentI2SMode == I2S_MODE_PLAY) return;
    if (currentI2SMode != I2S_MODE_IDLE) stopI2S();

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,  // stereo frame, mono data
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_LEN,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,   // silence on underrun
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_AMP_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));

    currentI2SMode = I2S_MODE_PLAY;
    Serial.println(F("[I2S] Playback mode"));
}

// ─────────────────────────────────────────────────────────────
//  I2S — Stop / uninstall driver
// ─────────────────────────────────────────────────────────────
void stopI2S() {
    i2s_driver_uninstall(I2S_PORT);
    currentI2SMode = I2S_MODE_IDLE;
    Serial.println(F("[I2S] Stopped"));
}

// ─────────────────────────────────────────────────────────────
//  Record from mic and stream raw PCM over Bluetooth
//  Format: 16-bit signed PCM, mono, 16 kHz
//  INMP441 gives 32-bit frames; we down-shift to 16-bit.
// ─────────────────────────────────────────────────────────────
void recordAndStream() {
    configureI2S_Record();

    // Send a simple 4-byte header so the laptop knows audio is starting
    // Magic: 0xAA 0xBB 0xCC 0xDD
    const uint8_t startMagic[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    SerialBT.write(startMagic, 4);

    static int32_t rawBuf[DMA_BUF_LEN];    // 32-bit from DMA
    static int16_t pcmBuf[DMA_BUF_LEN];    // converted 16-bit output

    while (isPTTPressed()) {
        size_t bytesRead = 0;
        esp_err_t err = i2s_read(I2S_PORT,
                                 rawBuf,
                                 sizeof(rawBuf),
                                 &bytesRead,
                                 portMAX_DELAY);
        if (err != ESP_OK || bytesRead == 0) continue;

        int samples = bytesRead / sizeof(int32_t);

        // Down-shift: INMP441 data is left-justified in 32-bit frame
        for (int i = 0; i < samples; i++) {
            pcmBuf[i] = (int16_t)(rawBuf[i] >> 16);
        }

        // Stream in BT_AUDIO_CHUNK-byte blocks
        int bytesToSend = samples * sizeof(int16_t);
        uint8_t* ptr = (uint8_t*)pcmBuf;
        while (bytesToSend > 0) {
            int chunk = min(bytesToSend, BT_AUDIO_CHUNK);
            SerialBT.write(ptr, chunk);
            ptr         += chunk;
            bytesToSend -= chunk;
        }
    }

    // Send end-of-stream marker: 0xFF 0xFF 0xFF 0xFF
    const uint8_t endMagic[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    SerialBT.write(endMagic, 4);

    stopI2S();
    Serial.println(F("[REC] PTT released, stream ended"));
}

// ─────────────────────────────────────────────────────────────
//  Receive raw PCM bytes from Bluetooth and play via I2S amp
//  Expects: 16-bit signed PCM, mono, 16 kHz (same as output)
// ─────────────────────────────────────────────────────────────
void receiveBTAndPlay() {
    configureI2S_Play();

    static uint8_t rxBuf[RX_PLAYBACK_BUF];

    // Drain BT stream until it goes dry (end of TTS reply)
    unsigned long lastRxMs = millis();
    const unsigned long RX_TIMEOUT_MS = 800; // silence gap = end of reply

    while (true) {
        int avail = SerialBT.available();
        if (avail > 0) {
            lastRxMs = millis();
            int toRead = min(avail, (int)sizeof(rxBuf));
            int got = SerialBT.readBytes(rxBuf, toRead);
            if (got > 0) {
                size_t written = 0;
                i2s_write(I2S_PORT, rxBuf, got, &written, portMAX_DELAY);
            }
        } else {
            if (millis() - lastRxMs > RX_TIMEOUT_MS) break;
            delay(5);
        }
    }

    stopI2S();
    Serial.println(F("[PLAY] Playback complete"));
}

// ─────────────────────────────────────────────────────────────
//  Enter deep sleep; MPU6050 INT pin wakes us on raise-to-wake
// ─────────────────────────────────────────────────────────────
void enterDeepSleep() {
    Serial.println(F("[SLEEP] Entering deep sleep. Raise wrist to wake."));
    updateDisplay("Sleeping...", "Raise to wake");
    delay(500);

    display.clearDisplay();
    display.display();

    // Configure ext0 wakeup: HIGH signal on MPU6050 INT pin
    esp_sleep_enable_ext0_wakeup((gpio_num_t)MPU6050_INT_PIN, 1);

    // Also allow PTT button to wake (ext1, active LOW = 0)
    esp_sleep_enable_ext1_wakeup(1ULL << PTT_BUTTON_PIN,
                                  ESP_EXT1_WAKEUP_ALL_LOW);

    esp_deep_sleep_start();
    // Never returns
}

// ─────────────────────────────────────────────────────────────
//  MPU6050 Init — enable motion interrupt for raise-to-wake
//  Configures Wake-on-Motion interrupt via direct I2C register writes
// ─────────────────────────────────────────────────────────────
void initMPU6050() {
    const uint8_t MPU_ADDR = 0x68;

    auto writeReg = [&](uint8_t reg, uint8_t val) {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    };

    // Wake from sleep, set clock to PLL with X gyro
    writeReg(0x6B, 0x01);   // PWR_MGMT_1
    delay(100);

    // Sample rate divider: 8 kHz / (1+9) = ~800 Hz
    writeReg(0x19, 0x09);   // SMPLRT_DIV

    // DLPF config: ~44 Hz accel bandwidth (smooth out noise)
    writeReg(0x1A, 0x03);   // CONFIG

    // Accel config: ±2g full scale
    writeReg(0x1C, 0x00);   // ACCEL_CONFIG

    // Motion detection threshold: ~20 mg  (1 LSB = ~2 mg)
    writeReg(0x1F, 0x0A);   // MOT_THR  — tune as needed

    // Motion detection duration: 1 sample
    writeReg(0x20, 0x01);   // MOT_DUR

    // Enable motion interrupt
    writeReg(0x38, 0x40);   // INT_ENABLE — MOT_EN bit

    // INT pin: active high, push-pull, latch until read
    writeReg(0x37, 0xA0);   // INT_PIN_CFG

    Serial.println(F("[MPU] Motion interrupt configured"));
}

// ─────────────────────────────────────────────────────────────
//  Helper: debounced PTT read (active LOW)
// ─────────────────────────────────────────────────────────────
bool isPTTPressed() {
    return digitalRead(PTT_BUTTON_PIN) == LOW;
}

// ─────────────────────────────────────────────────────────────
//  Helper: update OLED (two lines, centred)
// ─────────────────────────────────────────────────────────────
void updateDisplay(const char* line1, const char* line2) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1 — slightly larger
    display.setTextSize(2);
    display.setCursor(0, 10);
    display.println(line1);

    if (line2 && strlen(line2) > 0) {
        display.setTextSize(1);
        display.setCursor(0, 46);
        display.println(line2);
    }

    display.display();
}
