#include <driver/i2s.h>

#define I2S_WS 25
#define I2S_SD 32
#define I2S_SCK 33
#define LED_PIN 2
#define I2S_SAMPLE_RATE 16000
#define I2S_BUFFER_SIZE 512

// Noise filtering parameters
#define NOISE_THRESHOLD 500000     // Minimum level to consider as sound
#define SOUND_TRIGGER 1000000      // Level to trigger LED
#define SMOOTHING_SAMPLES 10       // Average this many readings

int32_t samples[I2S_BUFFER_SIZE];
int readingCount = 0;
int32_t smoothedValue = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("INMP441 with Noise Filtering");

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void loop() {
  size_t bytes_read;
  
  // Read multiple samples
  i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes_read, portMAX_DELAY);
  
  // Calculate RMS (Root Mean Square) for better sound measurement
  int64_t sum = 0;
  int sampleCount = bytes_read / sizeof(int32_t);
  
  for(int i = 0; i < sampleCount; i++) {
    sum += (int64_t)samples[i] * samples[i];  // Square the values
  }
  
  // Calculate RMS
  int32_t rmsValue = sqrt(sum / sampleCount);
  
  // Smoothing - exponential moving average
  smoothedValue = (smoothedValue * (SMOOTHING_SAMPLES - 1) + rmsValue) / SMOOTHING_SAMPLES;
  
  // Print values for debugging
  Serial.print("RMS: ");
  Serial.print(rmsValue);
  Serial.print(" | Smoothed: ");
  Serial.println(smoothedValue);
  
  // Control LED with hysteresis
  if (smoothedValue > SOUND_TRIGGER) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println(">>> SOUND DETECTED!");
  } else if (smoothedValue < NOISE_THRESHOLD) {
    digitalWrite(LED_PIN, LOW);
  }
  
  delay(10);
}