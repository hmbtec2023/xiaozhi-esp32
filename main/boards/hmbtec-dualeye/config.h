#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_
#include <driver/gpio.h>
#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX
#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16
#define BUILTIN_LED_GPIO GPIO_NUM_48
#define BOOT_BUTTON_GPIO GPIO_NUM_0
// HMB | TEC DualEye: 0 = XiaoZhi baseline only, 1 = GC9D01 eyes enabled
#define HMB_DUALEYE_ENABLED 1
#define HMB_EYE_SCLK GPIO_NUM_11 // grün
#define HMB_EYE_MOSI GPIO_NUM_12 // blau
#define HMB_EYE_DC GPIO_NUM_13 // weiß
#define HMB_EYE_CS_LEFT GPIO_NUM_14 // orange
#define HMB_EYE_CS_RIGHT GPIO_NUM_21 // gelb
#define HMB_EYE_RST_LEFT GPIO_NUM_43    // TX braun
#define HMB_EYE_RST_RIGHT GPIO_NUM_43 // TX violett

#define HMBTEC_BUTTON_GPIO       GPIO_NUM_44   // RX -> Lichtblick-Taster

// HMB | TEC Betriebsmodus: 0 = Lichtblick One-Shot, 1 = Seelsorge-Dialog
#define SEELSORGE_EN             1

//--- IR Remote Control ! hier nicht verwendet !
//#define HMBTEC_IR_RX_GPIO        GPIO_NUM_18   // TX -> IR receiver 

#endif
