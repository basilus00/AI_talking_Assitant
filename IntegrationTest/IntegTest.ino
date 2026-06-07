#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>

// ==================== PIN CONFIGURATION ====================
// INMP441 Microphone
#define MIC_WS      25
#define MIC_SD      32
#define MIC_SCK     33

// MAX98357A Amplifier
#define AMP_BCLK    27
#define AMP_LRC     14
#define AMP_DIN     26

// SD Card (SPI)
#define SD_CS       5

#define LED_PIN     2

// ==================== AUDIO CONFIGURATION ====================
#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 32
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     64
#define BUFFER_SIZE     512

// I2S ports
#define I2S_MIC_PORT    I2S_NUM_0
#define I2S_AMP_PORT    I2S_NUM_1

// WAV file header
struct WavHeader {
  char riff[4] = {'R','I','F','F'};
  uint32_t fileSize;
  char wave[4] = {'W','A','V','E'};
  char fmt[4] = {'f','m','t',' '};
  uint32_t fmtSize = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t byteRate = SAMPLE_RATE * 4;
  uint16_t blockAlign = 4;
  uint16_t bitsPerSample = 32;
  char data[4] = {'d','a','t','a'};
  uint32_t dataSize;
};

// State
bool isRecording = false;
bool isPlaying = false;
File audioFile;
const char* filename = "/recording.wav";

// Small RAM buffer for transfers
int32_t transferBuffer[BUFFER_SIZE];

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize SD card
  SPI.begin(18, 19, 23, SD_CS); // SCK, MISO, MOSI, CS
  pinMode(SD_CS, OUTPUT);
  
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    Serial.println("Check wiring and format as FAT32");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card initialized. Size: %lluMB\n", cardSize);
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  setupMicrophone();
  setupAmplifier();
  
  Serial.println("\n=== ESP32 Voice Record & Play (SD Card) ===");
  Serial.println("Commands: r=record, s=stop, p=play, l=loopback, d=delete");
}

// ==================== I2S SETUP ====================
void setupMicrophone() {
  i2s_config_t cfg = {
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

  i2s_pin_config_t pins = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &pins);
  Serial.println("Microphone ready");
}

void setupAmplifier() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = AMP_BCLK,
    .ws_io_num = AMP_LRC,
    .data_out_num = AMP_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_AMP_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_AMP_PORT, &pins);
  Serial.println("Amplifier ready");
}

// ==================== RECORD TO SD ====================
void writeWavHeader(File &file, uint32_t dataSize) {
  WavHeader header;
  header.fileSize = 36 + dataSize;
  header.dataSize = dataSize;
  file.write((uint8_t*)&header, sizeof(header));
}

void updateWavHeader(File &file) {
  uint32_t fileSize = file.size();
  uint32_t dataSize = fileSize - 44;
  
  file.seek(4);
  uint32_t riffSize = 36 + dataSize;
  file.write((uint8_t*)&riffSize, 4);
  
  file.seek(40);
  file.write((uint8_t*)&dataSize, 4);
}

void recordAudio() {
  if (isRecording || isPlaying) return;
  
  // Delete old file if exists
  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  
  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile) {
    Serial.println("Failed to create file!");
    return;
  }
  
  // Write placeholder header
  writeWavHeader(audioFile, 0);
  
  isRecording = true;
  Serial.println("Recording to SD... Press 's' to stop");
  digitalWrite(LED_PIN, HIGH);
  
  unsigned long lastBlink = millis();
  bool ledState = true;
  uint32_t totalSamples = 0;
  
  while (isRecording) {
    size_t bytesRead = 0;
    i2s_read(I2S_MIC_PORT, transferBuffer, sizeof(transferBuffer), &bytesRead, portMAX_DELAY);
    
    if (bytesRead > 0) {
      audioFile.write((uint8_t*)transferBuffer, bytesRead);
      totalSamples += bytesRead / 4;
    }
    
    // Blink LED
    if (millis() - lastBlink > 300) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastBlink = millis();
    }
    
    // Check stop command
    if (Serial.available() && Serial.read() == 's') {
      isRecording = false;
    }
  }
  
  // Update header with actual size
  updateWavHeader(audioFile);
  audioFile.close();
  
  digitalWrite(LED_PIN, LOW);
  Serial.printf("Saved! Duration: %.1f seconds, Size: %d bytes\n", 
                (float)totalSamples / SAMPLE_RATE, SD.open(filename).size());
}

// ==================== PLAY FROM SD ====================
void playAudio() {
  if (isRecording || isPlaying) return;
  
  if (!SD.exists(filename)) {
    Serial.println("No recording found!");
    return;
  }
  
  audioFile = SD.open(filename, FILE_READ);
  if (!audioFile) {
    Serial.println("Failed to open file!");
    return;
  }
  
  // Skip WAV header (44 bytes)
  audioFile.seek(44);
  
  isPlaying = true;
  Serial.println("Playing from SD...");
  digitalWrite(LED_PIN, HIGH);
  
  while (isPlaying && audioFile.available()) {
    size_t bytesToRead = min((size_t)sizeof(transferBuffer), (size_t)audioFile.available());
    size_t bytesRead = audioFile.read((uint8_t*)transferBuffer, bytesToRead);
    
    if (bytesRead > 0) {
      size_t bytesWritten = 0;
      i2s_write(I2S_AMP_PORT, transferBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    }
    
    if (Serial.available() && Serial.read() == 's') {
      isPlaying = false;
    }
  }
  
  i2s_zero_dma_buffer(I2S_AMP_PORT);
  audioFile.close();
  
  digitalWrite(LED_PIN, LOW);
  Serial.println("Playback done");
  isPlaying = false;
}

// ==================== LOOPBACK ====================
void loopbackMode() {
  if (isRecording || isPlaying) return;
  
  Serial.println("Loopback ON. Press 's' to stop.");
  digitalWrite(LED_PIN, HIGH);
  
  while (true) {
    size_t bytesRead = 0;
    i2s_read(I2S_MIC_PORT, transferBuffer, sizeof(transferBuffer), &bytesRead, portMAX_DELAY);
    
    // Noise gate
    for (int i = 0; i < bytesRead / 4; i++) {
      if (abs(transferBuffer[i]) < 30000) transferBuffer[i] = 0;
    }
    
    size_t bytesWritten = 0;
    i2s_write(I2S_AMP_PORT, transferBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    
    if (Serial.available() && Serial.read() == 's') break;
  }
  
  i2s_zero_dma_buffer(I2S_AMP_PORT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Loopback OFF");
}

// ==================== DELETE FILE ====================
void deleteRecording() {
  if (SD.exists(filename)) {
    SD.remove(filename);
    Serial.println("Recording deleted");
  } else {
    Serial.println("No file to delete");
  }
}

// ==================== MAIN LOOP ====================
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 'r': recordAudio(); break;
      case 'p': playAudio(); break;
      case 'l': loopbackMode(); break;
      case 'd': deleteRecording(); break;
      case 's': 
        isRecording = false;
        isPlaying = false;
        Serial.println("Stop");
        break;
      default:
        Serial.println("r=record, p=play, l=loopback, d=delete, s=stop");
        break;
    }
  }
  delay(10);
}