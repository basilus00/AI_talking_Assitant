#include <driver/i2s.h>

#define BCK     27
#define WS      14
#define DIN     26
#define SD_PIN  4   // If your module lacks pull-up, wire SD to this GPIO

void setup() {
  Serial.begin(115200);
  delay(1000);

  // FORCE amp ON - critical!
  pinMode(SD_PIN, OUTPUT);
  digitalWrite(SD_PIN, HIGH);
  Serial.println("SD = HIGH");

  // Same config as your working Bluetooth code
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = BCK,
    .ws_io_num = WS,
    .data_out_num = DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  Serial.println("I2S ready - playing 1kHz tone for 3 sec...");

  // Generate tone in chunks
  int16_t buf[128];
  for (int n = 0; n < 44100 * 3; n += 128) {
    for (int i = 0; i < 128; i++) {
      float p = 2.0 * PI * 1000 * (n + i) / 44100.0;
      buf[i] = (int16_t)(10000 * sin(p));
    }
    size_t w = 0;
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &w, portMAX_DELAY);
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("Done - did you hear it?");
}

void loop() {}