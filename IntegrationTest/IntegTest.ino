#include <driver/i2s.h>
#include <Arduino.h>

// ==================== PIN CONFIGURATION ====================
// INMP441 Microphone (I2S Input)
#define MIC_WS      25
#define MIC_SD      32
#define MIC_SCK     33

// MAX98357A Amplifier (I2S Output)
#define AMP_BCLK    27
#define AMP_LRC     14
#define AMP_DIN     26

#define LED_PIN     2

// ==================== AUDIO CONFIGURATION ====================
#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 32
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     64

// WROOM RAM OPTIMIZATION
// 16kHz * 4 bytes * 10 seconds = ~640KB (too much for WROOM)
// Use 8-bit compression or reduce duration
#define RECORD_SECONDS  3           // Max ~3 seconds for WROOM safety
#define MAX_SAMPLES     (SAMPLE_RATE * RECORD_SECONDS)
#define BUFFER_SIZE     512         // Smaller chunks

// I2S ports
#define I2S_MIC_PORT    I2S_NUM_0
#define I2S_AMP_PORT    I2S_NUM_1

// Recording buffer (in normal RAM)
int32_t recordBuffer[MAX_SAMPLES];  // ~192KB for 3 seconds
size_t recordedSamples = 0;
bool isRecording = false;
bool isPlaying = false;

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Check available RAM
  Serial.printf("Total heap: %d bytes\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Recording buffer: %d bytes (%d seconds @ 16kHz)\n", 
                MAX_SAMPLES * 4, RECORD_SECONDS);

  setupMicrophone();
  setupAmplifier();
  
  Serial.println("\n=== ESP32-WROOM Voice Record & Play ===");
  Serial.println("Commands: 'r'=record, 's'=stop, 'p'=play, 'l'=loopback, 'm'=monitor");
  Serial.printf("Max recording: %d seconds\n", RECORD_SECONDS);
}

// ==================== I2S MICROPHONE SETUP ====================
void setupMicrophone() {
  i2s_config_t i2s_mic_config = {
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

  i2s_pin_config_t mic_pin_config = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  esp_err_t err = i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Mic I2S driver install failed: %d\n", err);
    return;
  }
  
  i2s_set_pin(I2S_MIC_PORT, &mic_pin_config);
  Serial.println("Microphone ready");
}

// ==================== I2S AMPLIFIER SETUP ====================
void setupAmplifier() {
  i2s_config_t i2s_amp_config = {
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

  i2s_pin_config_t amp_pin_config = {
    .bck_io_num = AMP_BCLK,
    .ws_io_num = AMP_LRC,
    .data_out_num = AMP_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_AMP_PORT, &i2s_amp_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Amp I2S driver install failed: %d\n", err);
    return;
  }
  
  i2s_set_pin(I2S_AMP_PORT, &amp_pin_config);
  Serial.println("Amplifier ready");
}

// ==================== AUDIO FUNCTIONS ====================

size_t readMicrophone(int32_t *samples, size_t count) {
  size_t bytes_read = 0;
  i2s_read(I2S_MIC_PORT, samples, count * sizeof(int32_t), &bytes_read, portMAX_DELAY);
  return bytes_read / sizeof(int32_t);
}

size_t writeAmplifier(const int32_t *samples, size_t count) {
  size_t bytes_written = 0;
  i2s_write(I2S_AMP_PORT, samples, count * sizeof(int32_t), &bytes_written, portMAX_DELAY);
  return bytes_written / sizeof(int32_t);
}

// ==================== RECORD ====================
void recordAudio() {
  if (isRecording || isPlaying) return;
  
  isRecording = true;
  recordedSamples = 0;
  Serial.println("Recording...");
  digitalWrite(LED_PIN, HIGH);
  
  unsigned long lastBlink = millis();
  bool ledState = true;
  
  while (isRecording && recordedSamples < MAX_SAMPLES) {
    size_t samplesToRead = min((size_t)BUFFER_SIZE, MAX_SAMPLES - recordedSamples);
    size_t samplesRead = readMicrophone(&recordBuffer[recordedSamples], samplesToRead);
    recordedSamples += samplesRead;
    
    // Blink LED every 300ms
    if (millis() - lastBlink > 300) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastBlink = millis();
    }
    
    // Check for stop
    if (Serial.available() && Serial.read() == 's') {
      isRecording = false;
    }
  }
  
  digitalWrite(LED_PIN, LOW);
  Serial.printf("Recorded: %d samples (%.1fs)\n", 
                recordedSamples, (float)recordedSamples / SAMPLE_RATE);
  isRecording = false;
}

// ==================== PLAYBACK ====================
void playAudio() {
  if (isRecording || isPlaying || recordedSamples == 0) return;
  
  isPlaying = true;
  Serial.println("Playing...");
  digitalWrite(LED_PIN, HIGH);
  
  size_t played = 0;
  int32_t tempBuffer[BUFFER_SIZE];
  
  while (isPlaying && played < recordedSamples) {
    size_t chunk = min((size_t)BUFFER_SIZE, recordedSamples - played);
    
    for (size_t i = 0; i < chunk; i++) {
      // Apply volume (1.5x boost)
      int64_t sample = (int64_t)recordBuffer[played + i] * 3 / 2;
      if (sample > INT32_MAX) sample = INT32_MAX;
      if (sample < INT32_MIN) sample = INT32_MIN;
      tempBuffer[i] = (int32_t)sample;
    }
    
    writeAmplifier(tempBuffer, chunk);
    played += chunk;
    
    if (Serial.available() && Serial.read() == 's') {
      isPlaying = false;
    }
  }
  
  i2s_zero_dma_buffer(I2S_AMP_PORT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Playback done");
  isPlaying = false;
}

// ==================== LOOPBACK (Real-time) ====================
void loopbackMode() {
  if (isRecording || isPlaying) return;
  
  Serial.println("Loopback ON. Press 's' to stop.");
  digitalWrite(LED_PIN, HIGH);
  
  int32_t buffer[BUFFER_SIZE];
  
  while (true) {
    size_t samples = readMicrophone(buffer, BUFFER_SIZE);
    
    // Simple noise gate
    for (size_t i = 0; i < samples; i++) {
      if (abs(buffer[i]) < 30000) buffer[i] = 0;
    }
    
    writeAmplifier(buffer, samples);
    
    if (Serial.available() && Serial.read() == 's') break;
  }
  
  i2s_zero_dma_buffer(I2S_AMP_PORT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Loopback OFF");
}

// ==================== MONITOR LEVELS ====================
void monitorLevels() {
  Serial.println("Monitoring 10 seconds...");
  unsigned long start = millis();
  
  while (millis() - start < 10000) {
    int32_t sample = 0;
    size_t bytes_read;
    i2s_read(I2S_MIC_PORT, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);
    
    int level = abs(sample) / 10000;  // Scale for readability
    Serial.println(level);
    
    digitalWrite(LED_PIN, level > 50 ? HIGH : LOW);
    delay(10);
  }
  
  digitalWrite(LED_PIN, LOW);
  Serial.println("Monitor done");
}

// ==================== MAIN LOOP ====================
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 'r': recordAudio(); break;
      case 'p': playAudio(); break;
      case 'l': loopbackMode(); break;
      case 'm': monitorLevels(); break;
      case 's': 
        isRecording = false;
        isPlaying = false;
        Serial.println("Stop");
        break;
      default:
        Serial.println("r=record, p=play, l=loopback, m=monitor, s=stop");
        break;
    }
  }
  delay(10);
}