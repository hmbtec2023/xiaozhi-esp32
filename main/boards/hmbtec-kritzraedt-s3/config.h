#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

//音频iis部分
#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_4  //MCLK
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_5  //SCK
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_6  //DIN
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_7  //LRC
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_8  //DOUT
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_1  //PA

//音频iic部分
#define AUDIO_CODEC_I2C_NUM      I2C_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_15
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_16
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

//boot引脚
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BUILTIN_LED_GPIO GPIO_NUM_42

//--- HMB|TEC Lichtblick Erweiterungen
#define HMBTEC_BUZZER_GPIO       GPIO_NUM_2
#define HMBTEC_BUTTON_GPIO       GPIO_NUM_3
#define HMBTEC_PIXEL_RING_GPIO   GPIO_NUM_14
#define HMBTEC_PIXEL_RING_COUNT  16

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_45
#define DISPLAY_RST_PIN       GPIO_NUM_NC
#define DISPLAY_SCK_PIN       GPIO_NUM_12
#define DISPLAY_DC_PIN        GPIO_NUM_46
#define DISPLAY_CS_PIN        GPIO_NUM_10
#define DISPLAY_MOSI_PIN      GPIO_NUM_11
#define DISPLAY_MIS0_PIN      GPIO_NUM_13
#define DISPLAY_SPI_SCLK_HZ   (20 * 1000 * 1000)

#define LCD_SPI_HOST          SPI3_HOST

#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH         240
#define DISPLAY_HEIGHT        320
#define DISPLAY_MIRROR_X      true
#define DISPLAY_MIRROR_Y      false
#define DISPLAY_SWAP_XY       false
#define DISPLAY_INVERT_COLOR  true
#define DISPLAY_RGB_ORDER     LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X      0
#define DISPLAY_OFFSET_Y      0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE      0

// HMB|TEC NFC: vierpoliger Erweiterungsconnector, Software-SPI.
// GPIO2/3/14 sind exklusiv fuer RC522 reserviert; Buzzer/Taster/Pixel deaktiviert.
#define HMB_NFC_NONE   0
#define HMB_NFC_RC522  1
#define HMB_NFC_PN5180 2
#define HMB_NFC_READER HMB_NFC_RC522

#if HMB_NFC_READER == HMB_NFC_RC522
#define NFC_EN 1
#define NFC_SOFT_SPI 1
#define NFC_SCK_GPIO  GPIO_NUM_38 //14
#define NFC_MOSI_GPIO GPIO_NUM_48 //21
#define NFC_MISO_GPIO GPIO_NUM_47 //2
#define NFC_SS_GPIO   GPIO_NUM_41 //39 //3
#define NFC_RST_GPIO  GPIO_NUM_40
// RC522 NRSTPD dauerhaft HIGH an 3V3 (nur bei geeignetem 3,3-V-Modul).
#elif HMB_NFC_READER == HMB_NFC_PN5180
#error "PN5180-Treiber noch nicht integriert"
#endif

#endif  // _BOARD_CONFIG_H_
