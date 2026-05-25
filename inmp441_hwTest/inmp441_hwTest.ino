#include <driver/i2s.h>

#define I2S_WS 25
#define I2S_SD 32
#define I2S_SCK 26

#define LED_PIN 2

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
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
  int32_t sample = 0;
  size_t bytes_read;

  i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);

  int level = abs(sample);  // sound strength

  Serial.println(level);

  //  Threshold (adjust if needed)
  if (level > 2000000) {
    digitalWrite(LED_PIN, HIGH); // sound detected
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}