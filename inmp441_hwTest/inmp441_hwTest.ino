#include <driver/i2s.h>

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println("=== MINIMAL TEST ===");
  Serial.println("Leave mic completely SILENT");
  Serial.println("Just sitting there, no touching");
  Serial.println();
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = 33,  // SCK
    .ws_io_num = 25,   // WS
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = 32  // SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  Serial.println("Reading for 10 seconds...");
  
  uint32_t maxVal = 0;
  uint32_t minVal = 999999;
  
  unsigned long start = millis();
  while (millis() - start < 10000) {
    int32_t sample;
    size_t bytes_read;
    i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, pdMS_TO_TICKS(10));
    
    if (bytes_read > 0) {
      sample >>= 16;
      uint32_t absVal = abs(sample);
      
      if (absVal > maxVal) maxVal = absVal;
      if (absVal < minVal) minVal = absVal;
    }
  }
  
  Serial.println();
  Serial.print("Min value: ");
  Serial.println(minVal);
  Serial.print("Max value: ");
  Serial.println(maxVal);
  Serial.print("Average: ");
  Serial.println((minVal + maxVal) / 2);
  Serial.println();
  
  if (maxVal < 100) {
    Serial.println("✓ GOOD - Mic is working properly!");
  } else if (maxVal < 1000) {
    Serial.println("△ ACCEPTABLE - Some noise but usable");
  } else {
    Serial.println("❌ BAD - Hardware problem confirmed!");
    Serial.println("   Possible causes:");
    Serial.println("   1. Defective microphone");
    Serial.println("   2. L/R not actually connected");
    Serial.println("   3. Powered with 5V (damaged)");
    Serial.println("   4. Counterfeit/fake INMP441");
  }
}

void loop() {
  delay(1000);
}