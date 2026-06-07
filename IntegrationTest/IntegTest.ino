#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>

// ==================== PINS ====================
// All on I2S_NUM_0 (shared, time-multiplexed)
#define I2S_BCLK    27
#define I2S_LRC     14
#define I2S_DIN     26   // Data TO amp (TX)
#define I2S_DOUT    32   // Data FROM mic (RX) - using separate pin

// Mic uses its own clock/data pins
#define MIC_WS      25
#define MIC_SD      32
#define MIC_SCK     33

// Amp
#define AMP_SD      4       // Enable pin

// SD Card
#define SD_CS       5
#define LED_PIN     2

// ==================== AUDIO ====================
#define SAMPLE_RATE     16000
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     64
#define BUFFER_SIZE     512

#define I2S_PORT        I2S_NUM_0   // ONLY port we use

// WAV header (16-bit mono)
struct WavHeader {
  char riff[4] = {'R','I','F','F'};
  uint32_t fileSize;
  char wave[4] = {'W','A','V','E'};
  char fmt[4] = {'f','m','t',' '};
  uint32_t fmtSize = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t byteRate = SAMPLE_RATE * 2;
  uint16_t blockAlign = 2;
  uint16_t bitsPerSample = 16;
  char data[4] = {'d','a','t','a'};
  uint32_t dataSize;
};

bool isRecording = false, isPlaying = false;
File audioFile;
const char* filename = "/recording.wav";

int32_t micBuf[BUFFER_SIZE];    // INMP441 = 32-bit
int16_t ampBuf[BUFFER_SIZE];    // MAX98357A = 16-bit

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Enable MAX98357A
  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);
  Serial.println("MAX98357A enabled");

  // Init SD
  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD failed!");
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
  }
  Serial.printf("SD OK | Free heap: %d bytes\n", ESP.getFreeHeap());

  // Start with amp config (proven working)
  setupAmp();

  Serial.println("\n=== ESP32 Voice Record & Play (Single I2S) ===");
  Serial.println("r=record | s=stop | p=play | l=loopback | t=test tone | d=delete");
  Serial.println("NOTE: Mic and amp share I2S0 - not simultaneous");
}

// ==================== I2S CONFIGURATION ====================
void i2sUninstall() {
  i2s_driver_uninstall(I2S_PORT);
  delay(50);  // Let hardware settle
}

void setupAmp() {
  i2sUninstall();

  i2s_config_t c = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t p = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_PORT, &c, 0, NULL);
  i2s_set_pin(I2S_PORT, &p);
  Serial.println("Amp mode (I2S0 TX)");
}

void setupMic() {
  i2sUninstall();

  i2s_config_t c = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t p = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };
  i2s_driver_install(I2S_PORT, &c, 0, NULL);
  i2s_set_pin(I2S_PORT, &p);
  Serial.println("Mic mode (I2S0 RX)");
}

// ==================== HELPERS ====================
void convert32to16(int32_t *src, int16_t *dst, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int32_t s = src[i] >> 8;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    dst[i] = (int16_t)s;
  }
}

void writeWavHeader(File &f, uint32_t dataSize) {
  WavHeader h;
  h.fileSize = 36 + dataSize;
  h.dataSize = dataSize;
  f.write((uint8_t*)&h, sizeof(h));
}

void updateWavHeader(File &f) {
  uint32_t sz = f.size() - 44;
  uint32_t riffSize = 36 + sz;
  f.seek(4);
  f.write((uint8_t*)&riffSize, 4);
  f.seek(40);
  f.write((uint8_t*)&sz, 4);
}

// ==================== RECORD ====================
void recordAudio() {
  if (isRecording || isPlaying) return;
  if (SD.exists(filename)) SD.remove(filename);

  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile) { Serial.println("File error!"); return; }

  // Switch to mic mode
  setupMic();

  writeWavHeader(audioFile, 0);
  isRecording = true;
  Serial.println("Recording... press 's'");
  digitalWrite(LED_PIN, HIGH);

  uint32_t totalSamples = 0;
  unsigned long blink = millis();

  while (isRecording) {
    size_t br = 0;
    i2s_read(I2S_PORT, micBuf, sizeof(micBuf), &br, portMAX_DELAY);
    size_t samples = br / 4;

    convert32to16(micBuf, ampBuf, samples);
    audioFile.write((uint8_t*)ampBuf, samples * 2);
    totalSamples += samples;

    if (millis() - blink > 300) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      blink = millis();
    }
    if (Serial.available() && Serial.read() == 's') isRecording = false;
  }

  updateWavHeader(audioFile);
  audioFile.close();

  // Switch back to amp mode
  setupAmp();

  digitalWrite(LED_PIN, LOW);
  Serial.printf("Saved: %.1f sec | %d bytes\n", (float)totalSamples / SAMPLE_RATE, SD.open(filename).size());
  isRecording = false;
}

// ==================== PLAY ====================
void playAudio() {
  if (isRecording || isPlaying) return;
  if (!SD.exists(filename)) { Serial.println("No file!"); return; }

  // Ensure amp mode
  setupAmp();

  audioFile = SD.open(filename, FILE_READ);
  audioFile.seek(44);
  isPlaying = true;
  Serial.println("Playing...");
  digitalWrite(LED_PIN, HIGH);

  while (isPlaying && audioFile.available()) {
    size_t toRead = min((size_t)sizeof(ampBuf), (size_t)audioFile.available());
    size_t br = audioFile.read((uint8_t*)ampBuf, toRead);
    size_t written = 0;
    i2s_write(I2S_PORT, ampBuf, br, &written, portMAX_DELAY);

    if (Serial.available() && Serial.read() == 's') isPlaying = false;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  audioFile.close();
  digitalWrite(LED_PIN, LOW);
  Serial.println("Done");
  isPlaying = false;
}

// ==================== LOOPBACK (Time-sliced) ====================
void loopbackMode() {
  if (isRecording || isPlaying) return;
  Serial.println("Loopback ON | 's' to stop");
  digitalWrite(LED_PIN, HIGH);

  // We rapidly switch between mic read and amp write
  // Not true full-duplex, but works for voice
  while (true) {
    // Read from mic
    setupMic();
    size_t br = 0;
    i2s_read(I2S_PORT, micBuf, sizeof(micBuf), &br, portMAX_DELAY);
    size_t samples = br / 4;
    convert32to16(micBuf, ampBuf, samples);

    // Noise gate
    for (size_t i = 0; i < samples; i++) {
      if (abs(ampBuf[i]) < 400) ampBuf[i] = 0;
    }

    // Write to amp
    setupAmp();
    size_t written = 0;
    i2s_write(I2S_PORT, ampBuf, samples * 2, &written, portMAX_DELAY);

    if (Serial.available() && Serial.read() == 's') break;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  setupAmp();  // Return to amp mode
  digitalWrite(LED_PIN, LOW);
  Serial.println("Loopback OFF");
}

// ==================== TEST TONE ====================
void testTone() {
  // Ensure amp mode
  setupAmp();

  Serial.println("Test tone 3 sec...");
  digitalWrite(LED_PIN, HIGH);

  int16_t buf[128];
  for (int n = 0; n < SAMPLE_RATE * 3; n += 128) {
    for (int i = 0; i < 128; i++) {
      float p = 2.0 * PI * 1000 * (n + i) / (float)SAMPLE_RATE;
      buf[i] = (int16_t)(12000 * sin(p));
    }
    size_t w = 0;
    i2s_write(I2S_PORT, buf, sizeof(buf), &w, portMAX_DELAY);
  }

  i2s_zero_dma_buffer(I2S_PORT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Tone done");
}

// ==================== LOOP ====================
void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'r': recordAudio(); break;
      case 'p': playAudio(); break;
      case 'l': loopbackMode(); break;
      case 't': testTone(); break;
      case 'd': SD.remove(filename); Serial.println("Deleted"); break;
      case 's': isRecording = isPlaying = false; Serial.println("Stop"); break;
      default:  Serial.println("r=rec | p=play | l=loop | t=tone | d=del | s=stop"); break;
    }
  }
  delay(10);
}