#include <driver/i2s.h>

// INMP441 Pins - ESP32 WROOM-32
#define I2S_WS 25
#define I2S_SCK 32
#define I2S_SD 33

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== Voice Detection Test ===");
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  Serial.println("Ready! Make some noise...");
}

void loop() {
  int32_t sample;
  size_t bytes_read;
  static int32_t maxAmplitude = 0;
  static unsigned long lastPrint = 0;
  
  i2s_read(I2S_NUM_0, &sample, sizeof(int32_t), &bytes_read, portMAX_DELAY);
  
  if (bytes_read > 0) {
    sample >>= 16;  // CHANGED FROM >>8 to >>16
    int32_t absSample = abs(sample);
    
    if (absSample > maxAmplitude) {
      maxAmplitude = absSample;
    }
    
    if (millis() - lastPrint > 100) {
      Serial.print("Current: ");
      Serial.print(sample);
      Serial.print(" | Max: ");
      Serial.print(maxAmplitude);
      
      if (maxAmplitude > 1000) {
        Serial.print(" | *** SOUND DETECTED! ***");
      }
      Serial.println();
      
      maxAmplitude = 0;
      lastPrint = millis();
    }
  }
}