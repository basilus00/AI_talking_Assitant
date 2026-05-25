#include <driver/i2s.h>

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println("=== FINAL HARDWARE TEST ===");
  Serial.println("DO NOT TOUCH THE MICROPHONE");
  Serial.println("Just let it sit in complete silence");
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
    .bck_io_num = 33,
    .ws_io_num = 25,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = 32
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  Serial.println("Collecting 100 samples...");
  
  int samples[100];
  
  for (int i = 0; i < 100; i++) {
    int32_t sample;
    size_t bytes_read;
    i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);
    
    if (bytes_read > 0) {
      sample >>= 16;
      samples[i] = abs(sample);
      delay(50);  // Wait between samples
    }
  }
  
  // Calculate statistics
  int sum = 0;
  int maxVal = 0;
  int minVal = 999999;
  
  for (int i = 0; i < 100; i++) {
    sum += samples[i];
    if (samples[i] > maxVal) maxVal = samples[i];
    if (samples[i] < minVal) minVal = samples[i];
  }
  
  int avg = sum / 100;
  
  Serial.println();
  Serial.println("=== RESULTS ===");
  Serial.print("Min: "); Serial.println(minVal);
  Serial.print("Max: "); Serial.println(maxVal);
  Serial.print("Avg: "); Serial.println(avg);
  Serial.println();
  
  if (maxVal < 200 && avg < 100) {
    Serial.println("✓ MICROPHONE IS GOOD");
    Serial.println("  You can use it normally");
  } else if (maxVal < 1000 && avg < 500) {
    Serial.println("△ ACCEPTABLE (noisy but usable)");
  } else {
    Serial.println("❌ MICROPHONE IS DAMAGED/DEFECTIVE");
    Serial.println();
    Serial.println("  RECOMMENDATIONS:");
    Serial.println("  1. Buy a new INMP441 from reputable seller");
    Serial.println("  2. Try Adafruit, SparkFun, or official distributors");
    Serial.println("  3. NEVER power it with 5V!");
    Serial.println("  4. Always use 3.3V only");
  }
}

void loop() {
  delay(1000);
}