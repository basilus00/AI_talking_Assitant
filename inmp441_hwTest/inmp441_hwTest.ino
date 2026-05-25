
#include <driver/i2s.h>

//I2S pins
#define I2S_WS 25
#define I2S_SD 32
#define I2S_SCK 33

//I2S configuration
#define I2S_SAMPLE_RATE 16000 //16khz 
#define I2S_BUFFER_SIZE 1024

void setup() {
  Serial.begin(115200);
  Serial.println("INMP441 Microphonr Test -Serial plotter");

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate =I2S_SAMPLE_RATE, //we changed sample rate from numbers we define to this
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4, //num of DMA buffers
    .dma_buf_len =  I2S_BUFFER_SIZE, //rather than values we write 
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL); 
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1, //not used 
    .data_in_num = I2S_SD
  };

  
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void loop() {
  int32_t audio_samples[I2S_BUFFER_SIZE];
  size_t bytes_read;

  i2s_read(I2S_NUM_0,audio_samples, sizeof(audio_samples), &bytes_read, portMAX_DELAY);
  int64_t sum = 0;
  for(int i=0; i < bytes_read / sizeof(int32_t);i++){
    sum += abs(audio_samples[i]);
  }
  int32_t average_amplitude = sum /(bytes_read/sizeof(int32_t));
  Serial.println(average_amplitude);
  delay(10);
}