#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>

// ==================== PINS ====================
#define AMP_BCLK    27
#define AMP_LRC     14
#define AMP_DIN     26
#define AMP_SD      4

#define MIC_WS      25
#define MIC_SD      32
#define MIC_SCK     33

#define SD_CS       5
#define LED_PIN     2

// ==================== AUDIO ====================
#define SAMPLE_RATE     44100    // ← CHANGED: 44.1kHz = accurate clock
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     64
#define BUFFER_SIZE     512

#define I2S_PORT        I2S_NUM_0

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
int32_t micBuf[BUFFER_SIZE];
int16_t ampBuf[BUFFER_SIZE];

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);

  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD failed!");
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
  }
  Serial.printf("SD OK | Free heap: %d bytes\n", ESP.getFreeHeap());

  setupAmp();
  Serial.println("\n=== Voice Record & Play @ 44.1kHz ===");
  Serial.println("r=record | s=stop | p=play | l=loopback | t=test tone | d=delete");
}

void i2sUninstall() {
  i2s_driver_uninstall(I2S_PORT);
  delay(50);
}

void setupAmp() {
  i2sUninstall();
  i2s_config_t c = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = SAMPLE_RATE * 256  // ← ADD THIS: Lock MCLK to exact rate
  };
  i2s_pin_config_t p = {
    .bck_io_num = AMP_BCLK,
    .ws_io_num = AMP_LRC,
    .data_out_num = AMP_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_PORT, &c, 0, NULL);
  i2s_set_pin(I2S_PORT, &p);
  Serial.println("Amp ready");
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
    .use_apll = true,
    .tx_desc_auto_clear = false,
    .fixed_mclk = SAMPLE_RATE * 256  // ← ADD THIS: Same MCLK
  };
  i2s_pin_config_t p = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };
  i2s_driver_install(I2S_PORT, &c, 0, NULL);
  i2s_set_pin(I2S_PORT, &p);
  Serial.println("Mic ready");
}

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

void recordAudio() {
  if (isRecording || isPlaying) return;
  if (SD.exists(filename)) SD.remove(filename);

  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile) { Serial.println("File error!"); return; }

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
  setupAmp();
  digitalWrite(LED_PIN, LOW);
  Serial.printf("Saved: %.1f sec | %d bytes\n", (float)totalSamples / SAMPLE_RATE, SD.open(filename).size());
  isRecording = false;
}

void playAudio() {
  if (isRecording || isPlaying) return;
  if (!SD.exists(filename)) { Serial.println("No file!"); return; }

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

void loopbackMode() {
  if (isRecording || isPlaying) return;
  Serial.println("Loopback ON | 's' to stop");
  digitalWrite(LED_PIN, HIGH);

  while (true) {
    setupMic();
    size_t br = 0;
    i2s_read(I2S_PORT, micBuf, sizeof(micBuf), &br, portMAX_DELAY);
    size_t samples = br / 4;
    convert32to16(micBuf, ampBuf, samples);

    for (size_t i = 0; i < samples; i++) if (abs(ampBuf[i]) < 400) ampBuf[i] = 0;

    setupAmp();
    size_t written = 0;
    i2s_write(I2S_PORT, ampBuf, samples * 2, &written, portMAX_DELAY);

    if (Serial.available() && Serial.read() == 's') break;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  setupAmp();
  digitalWrite(LED_PIN, LOW);
  Serial.println("Loopback OFF");
}

void testTone() {
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