#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_METHOD_SIMPLEX
#ifdef AUDIO_I2S_METHOD_SIMPLEX
    #define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
    #define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
    #define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
    // I2S microphone L/R channel-select pin must be hard-wired to GND.
    #define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
    #define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
    #define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16
#endif

#define BUILTIN_LED_GPIO GPIO_NUM_48
#define BOOT_BUTTON_GPIO GPIO_NUM_0

// ============================================================================
// HMB | TEC TalkCard V0.3.0
// ============================================================================
#define HMB_TC_PTT_GPIO      GPIO_NUM_44
#define HMB_TC_PIXEL_GPIO    GPIO_NUM_43
#define HMB_TC_PIXEL_COUNT   1
#define HMB_TC_BUTTON_1_GPIO GPIO_NUM_11
#define HMB_TC_BUTTON_2_GPIO GPIO_NUM_12
#define HMB_TC_BUTTON_3_GPIO GPIO_NUM_13
#define HMB_TC_BUTTON_4_GPIO GPIO_NUM_14
#define HMB_TC_TIMEOUT_MS    30000

// Optional RC522 (SPI2). GPIO33..37 only if NOT reserved for Octal memory.
#define NFC_EN
#ifdef NFC_EN
#include <driver/spi_master.h>
#define NFC_SPI_HOST SPI2_HOST
#define NFC_SS_GPIO GPIO_NUM_33
#define NFC_SCK_GPIO GPIO_NUM_34
#define NFC_MOSI_GPIO GPIO_NUM_35
#define NFC_MISO_GPIO GPIO_NUM_36
#define NFC_RST_GPIO GPIO_NUM_37
#endif
#endif // _BOARD_CONFIG_H_
