#ifndef HMBTEC_MEMORYSTAND_C3_CONFIG_H
#define HMBTEC_MEMORYSTAND_C3_CONFIG_H
#include <driver/gpio.h>
// HMB|TEC MemoryStand C3 V0.4.0 – ESP-IDF/XiaoZhi
// Gemeinsamer I2S-Takt: RX/TX muessen dieselbe Hardware-Samplerate nutzen.
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_4
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7
// Fuer spaetere Ausbaustufen reserviert, noch nicht initialisiert:
#define DISPLAY_SDA_PIN GPIO_NUM_20
#define DISPLAY_SCL_PIN GPIO_NUM_21
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define STATUS_PIXEL_GPIO GPIO_NUM_8
#define IR_RX_GPIO GPIO_NUM_3
#define IR_TX_GPIO GPIO_NUM_10
#define LD2410_RX_GPIO GPIO_NUM_1
#define LD2410_TX_GPIO GPIO_NUM_2
#endif

#define HMB_IR_ENABLED 1
