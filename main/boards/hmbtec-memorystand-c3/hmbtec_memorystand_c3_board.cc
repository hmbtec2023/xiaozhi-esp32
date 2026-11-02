/*
 * HMB | TEC Build Information
 * PROJECT_NAME: HMBTEC_MemoryStand_C3
 * PROJECT_VERSION: 0.5.1
 * BOARD: ESP32-C3 SuperMini
 * FRAMEWORK: ESP-IDF 6.1 / XiaoZhi
 *
 * FEATURES:
 * - WiFi / XiaoZhi
 * - INMP441 Mikrofon
 * - MAX98357A Lautsprecher
 * - SSD1306 OLED 128x64, I2C 0x3C
 *
 * OLED: SDA=20, SCL=21
 * I2S: BCLK=4, WS=5, DIN=6, DOUT=7
 */

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "config.h"

#include <esp_log.h>
#include <esp_check.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include "button.h"
#include "application.h"

#define TAG "HMBMemoryStandC3"

class HmbtecMemoryStandC3Board : public WifiBoard {

private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    Button boot_button_;

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();

            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }

            ESP_LOGI(TAG, "GPIO9: ToggleChatState");
            app.ToggleChatState();
        });
    }

    Display* display_ = nullptr;

    void InitializeI2C() {

        ESP_LOGI(TAG, "Initializing I2C SDA=%d SCL=%d",
                 DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);

        i2c_master_bus_config_t bus_config = {};

        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = DISPLAY_SDA_PIN;
        bus_config.scl_io_num = DISPLAY_SCL_PIN;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

        ESP_ERROR_CHECK(
            i2c_new_master_bus(&bus_config, &i2c_bus_)
        );
    }

    void InitializeDisplay() {

        ESP_LOGI(TAG, "Initializing SSD1306 OLED");

        esp_lcd_panel_io_i2c_config_t io_config = {};

        io_config.dev_addr = 0x3C;
        io_config.scl_speed_hz = 400000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_io_i2c(
                i2c_bus_,
                &io_config,
                &panel_io_
            )
        );

        esp_lcd_panel_dev_config_t panel_config = {};

        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = DISPLAY_HEIGHT
        };

        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(
            esp_lcd_new_panel_ssd1306(
                panel_io_,
                &panel_config,
                &panel_
            )
        );

        ESP_ERROR_CHECK(
            esp_lcd_panel_reset(panel_)
        );

        ESP_ERROR_CHECK(
            esp_lcd_panel_init(panel_)
        );

        ESP_ERROR_CHECK(
            esp_lcd_panel_disp_on_off(panel_, true)
        );

        display_ = new OledDisplay(
            panel_io_,
            panel_,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            true,
            true
        );

        ESP_LOGI(TAG, "SSD1306 OLED ready");
    }

public:

    HmbtecMemoryStandC3Board()
        : boot_button_(GPIO_NUM_9, false) {

        ESP_LOGI(TAG, "HMBTEC MemoryStand C3 V0.5.1");

        InitializeI2C();
        InitializeDisplay();
        InitializeButtons();
    }

    virtual AudioCodec* GetAudioCodec() override {

        static NoAudioCodecDuplex codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN
        );

        return &codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(HmbtecMemoryStandC3Board);