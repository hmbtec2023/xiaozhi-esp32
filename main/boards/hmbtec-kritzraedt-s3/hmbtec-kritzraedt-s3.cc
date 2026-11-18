#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <wifi_station.h>
#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include "application.h"
#include "button.h"
#include "config.h"
#ifdef NFC_EN
#include "hmb_talkcard_nfc.h"
#endif
#include "mcp_server.h"
#include "adc_battery_monitor.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "led/single_led.h"
#include "system_reset.h"
#include "esp_lcd_ili9341.h"

#include "buzzer_controller.h"
#include "led/circular_strip.h"
#include "light_controller.h"

#define TAG "HmbtecKritzraedt"

// HMB|TEC Kritzraedt – Touchstatus fuer LVGL 9
static portMUX_TYPE touch_mux = portMUX_INITIALIZER_UNLOCKED;

static struct {
    bool pressed = false;
    uint16_t x = 0;
    uint16_t y = 0;
} lv_touch_state;

static void KritzraedtTouchRead(lv_indev_t *indev,
                               lv_indev_data_t *data) {
    (void)indev;

    portENTER_CRITICAL(&touch_mux);
    data->state = lv_touch_state.pressed
        ? LV_INDEV_STATE_PRESSED
        : LV_INDEV_STATE_RELEASED;
    data->point.x = lv_touch_state.x;
    data->point.y = lv_touch_state.y;
    portEXIT_CRITICAL(&touch_mux);
}

class TouchDriver {
public:
    TouchDriver() : dev_(nullptr) {}

    bool Init(i2c_master_bus_handle_t bus, uint8_t addr) {
        i2c_device_config_t cfg = {
            .device_address = addr,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
        };
        return i2c_master_bus_add_device(bus, &cfg, &dev_) == ESP_OK;
    }

    bool Read(bool &touched, uint16_t &x, uint16_t &y) {
        touched = false;
        x = y = 0;
        if (!dev_) return false;

        uint8_t reg = 0x02;
        uint8_t buf[5];
        if (i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 50) != ESP_OK) return false;

        uint8_t points = buf[0] & 0x0F;
        if (points == 0) return true;

        touched = true;
        x = ((buf[1] & 0x0F) << 8) | buf[2];
        y = ((buf[3] & 0x0F) << 8) | buf[4];
        return true;
    }

private:
    i2c_master_dev_handle_t dev_;
};

class HmbtecKritzraedt : public WifiBoard {
private:
    Button boot_button_;
    CircularStrip* pixel_ring_ = nullptr;
    HmbtecLightController* light_controller_ = nullptr;
    LcdDisplay *display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    TouchDriver touch_;
#ifdef NFC_EN
    HmbTalkCardNfc nfc_;
    bool nfc_ready_ = false;
    static void NfcTask(void* arg) {
        static_cast<HmbtecKritzraedt*>(arg)->NfcLoop();
    }
    void NfcLoop() {
    static const char* triggers[8] = {
        "HMBKRITZRAEDT_KRITZRAEDT",
        "HMBKRITZRAEDT_GANGELT",
        "HMBKRITZRAEDT_SITTARD",
        "HMBKRITZRAEDT_KIRCHEN",
        "HMBKRITZRAEDT_ALLTAG",
        "HMBKRITZRAEDT_HANDEL",
        "HMBKRITZRAEDT_GESCHICHTE",
        "HMBKRITZRAEDT_GEHEIMNISSE"
    };

    std::string previous_uid;
    int misses = 0;

    while (true) {
        std::string uid;

        if (nfc_.detect(uid)) {
            misses = 0;

            if (uid != previous_uid) {
                previous_uid = uid;

                ESP_LOGI(TAG, "NFC UID=%s", uid.c_str());

                uint8_t payload[256] = {};
                size_t payload_len = 0;

                if (nfc_.read_payload(
                        payload, sizeof(payload), payload_len)) {

                    // NDEF Text Record:
                    // 03 LEN D1 01 ... 54 02 65 6E room=6 ...
                    // Nur den Textparameter room auswerten.

                    std::string data(
                        reinterpret_cast<const char*>(payload),
                        payload_len
                    );

                    size_t pos = data.find("room=");

                    if (pos != std::string::npos) {
                        pos += 5;

                        if (pos < data.size() &&
                            data[pos] >= '1' &&
                            data[pos] <= '8' &&
                            (pos + 1 == data.size() ||
                             data[pos + 1] < '0' ||
                             data[pos + 1] > '9')) {

                            int room = data[pos] - '0';

                            const char* trigger =
                                triggers[room - 1];

                            ESP_LOGI(
                                TAG,
                                "NFC ROOM=%d -> %s",
                                room,
                                trigger
                            );

                            Application::GetInstance()
                                .QueueWakeWordInvoke(trigger);

                        } else {
                            ESP_LOGW(
                                TAG,
                                "NFC: ungueltiger room-Wert"
                            );
                        }
                    } else {
                        ESP_LOGW(
                            TAG,
                            "NFC: kein room-Parameter"
                        );
                    }
                }
            }

        } else if (++misses >= 5) {
            previous_uid.clear();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

    void InitializeNfc() {
        nfc_ready_ = nfc_.begin();
        if (nfc_ready_) {
            xTaskCreate(NfcTask, "kritz_nfc", 4096, this, 4, nullptr);
        } else {
            ESP_LOGE(TAG, "RC522-Initialisierung fehlgeschlagen");
        }
    }
#endif
    AdcBatteryMonitor* adc_battery_monitor_;

    void InitializeBatteryMonitor() {
        adc_battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_8, 200000, 200000, GPIO_NUM_NC);
    }

    static void TouchTask(void *arg) {
        auto *self = static_cast<HmbtecKritzraedt*>(arg);
        auto &app = Application::GetInstance();

        uint32_t down_start = 0;
        bool down = false;

        while (true) {
            bool touched = false;
            uint16_t x = 0, y = 0;
            bool read_ok = self->touch_.Read(touched, x, y);

            if (read_ok) {
                portENTER_CRITICAL(&touch_mux);
                lv_touch_state.pressed = touched;
                if (touched) {
                    lv_touch_state.x = x;
                    lv_touch_state.y = y;
                }
                portEXIT_CRITICAL(&touch_mux);
            }

            uint32_t now = esp_timer_get_time() / 1000;

            if (touched && !down) {
                down = true;
                down_start = now;
                ESP_LOGI(TAG, "TOUCH RAW: X=%u Y=%u", x, y);
            }

            if (!touched && down) {
                down = false;

                uint32_t duration = now - down_start;

                if (duration >= 3000) {
                    ESP_LOGI(TAG, "Kritzraedt: WLAN-Konfiguration");
                    self->EnterWifiConfigMode();
                } else {
                    ESP_LOGI(TAG, "Kritzraedt: Touch -> ToggleChatState");
                    app.ToggleChatState();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    void InitializeTouch() {
        if (!touch_.Init(codec_i2c_bus_, 0x38)) {
            ESP_LOGE(TAG, "Touchcontroller 0x38 nicht gefunden");
            return;
        }

        // LVGL 9: Touchscreen als Pointer registrieren
        if (lvgl_port_lock(1000)) {
            lv_indev_t* indev = lv_indev_create();

            if (indev != nullptr) {
                lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
                lv_indev_set_read_cb(indev, KritzraedtTouchRead);
                lv_indev_set_display(indev, lv_display_get_default());

                ESP_LOGI(TAG, "LVGL Touch registriert: 240x320");
            } else {
                ESP_LOGE(TAG, "LVGL Touchregistrierung fehlgeschlagen");
            }

            lvgl_port_unlock();
        } else {
            ESP_LOGE(TAG, "LVGL Lock fehlgeschlagen");
        }

        xTaskCreatePinnedToCore(
            TouchTask, "touch_task", 4096, this, 5, nullptr, 0
        );
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MIS0_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
            }
            app.ToggleChatState();
        });


    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        ESP_LOGI(TAG, "Install LCD driver ILI9341");
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeBuzzer() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = 1ULL << HMBTEC_BUZZER_GPIO;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;

        ESP_ERROR_CHECK(gpio_config(&io_conf));

        // Aktiver Buzzer: HIGH = ein, LOW = aus
        gpio_set_level(HMBTEC_BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(HMBTEC_BUZZER_GPIO, 0);

        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(HMBTEC_BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(HMBTEC_BUZZER_GPIO, 0);

        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(HMBTEC_BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(HMBTEC_BUZZER_GPIO, 0);
    }

    void InitializePixelRing() {
        ESP_LOGI(TAG, "Initializing HMBTEC NeoPixel ring: GPIO%d, %d pixels",
                HMBTEC_PIXEL_RING_GPIO, HMBTEC_PIXEL_RING_COUNT);

        pixel_ring_ = new CircularStrip(HMBTEC_PIXEL_RING_GPIO, HMBTEC_PIXEL_RING_COUNT);

        // Hardwaretest: alle 8 Pixel kurz warmweiß
        pixel_ring_->SetAllColor({80, 60, 30});
        vTaskDelay(pdMS_TO_TICKS(500));

        // Danach aus
        pixel_ring_->SetAllColor({0, 0, 0});
    }



    // HMB|TEC Kritzraedt AI - Gluecksrad MCP V0.2
    void InitializeKritzraedtTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool(
            "self.hmbtec.kritzraedt_kritzraedt",
            "When the user input is exactly HMBKRITZRAEDT_KRITZRAEDT, always "
            "call this tool. Topic: Leben und Chronik des Jacobus Kritzraedt, "
            "nicht eine allgemeine Stadtgeschichte. Execute the returned "
            "instruction. Do not mention the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: kritzraedt");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: dein eigenes Leben als Jacobus Kritzraedt "
                    "(1602-1672), deine Arbeit als Chronist, deine Quellen und deine "
                    "Sicht auf die damalige Zeit. Trenne belegte biografische Angaben "
                    "von Vermutungen. Beginne unmittelbar mit einer lebendigen "
                    "historischen Erzaehlung in der Ich-Form und auf Deutsch. Sprich "
                    "aus der historischen Perspektive, ohne vorzugeben, unbelegte "
                    "Ereignisse selbst erlebt zu haben. Erfinde keine Fakten, Zitate "
                    "oder Quellen. Kennzeichne Unsicherheiten und vermeide "
                    "Anachronismen. Bleibe beim gewaehlten Thema, auch wenn einzelne "
                    "Woerter mehrdeutig sind. Die Erzaehlung soll etwa 30 bis 45 "
                    "Sekunden dauern. Stelle anschliessend genau eine passende offene "
                    "Frage und warte auf die Antwort des Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_gangelt",
            "When the user input is exactly HMBKRITZRAEDT_GANGELT, always call "
            "this tool. Topic: Ortsgeschichte von Gangelt, nicht Sittard oder "
            "die Biografie allein. Execute the returned instruction. Do not "
            "mention the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: gangelt");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: Gangelt im 17. Jahrhundert: den Ort, seine "
                    "Bewohner, lokale Lebensverhaeltnisse und historisch belegte "
                    "Entwicklungen. Bleibe bei Gangelt; erwaehne andere Orte nur zum "
                    "Verstaendnis. Beginne unmittelbar mit einer lebendigen "
                    "historischen Erzaehlung in der Ich-Form und auf Deutsch. Sprich "
                    "aus der historischen Perspektive, ohne vorzugeben, unbelegte "
                    "Ereignisse selbst erlebt zu haben. Erfinde keine Fakten, Zitate "
                    "oder Quellen. Kennzeichne Unsicherheiten und vermeide "
                    "Anachronismen. Bleibe beim gewaehlten Thema, auch wenn einzelne "
                    "Woerter mehrdeutig sind. Die Erzaehlung soll etwa 30 bis 45 "
                    "Sekunden dauern. Stelle anschliessend genau eine passende offene "
                    "Frage und warte auf die Antwort des Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_sittard",
            "When the user input is exactly HMBKRITZRAEDT_SITTARD, always call "
            "this tool. Topic: Geschichte der Stadt Sittard, nicht Gangelt. "
            "Execute the returned instruction. Do not mention the trigger or "
            "tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: sittard");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: Sittard im 17. Jahrhundert: Stadtleben, "
                    "regionale Verbindungen und historisch belegte Entwicklungen. "
                    "Bleibe bei Sittard und unterscheide damalige Verhaeltnisse von "
                    "heutigen. Beginne unmittelbar mit einer lebendigen historischen "
                    "Erzaehlung in der Ich-Form und auf Deutsch. Sprich aus der "
                    "historischen Perspektive, ohne vorzugeben, unbelegte Ereignisse "
                    "selbst erlebt zu haben. Erfinde keine Fakten, Zitate oder Quellen. "
                    "Kennzeichne Unsicherheiten und vermeide Anachronismen. Bleibe beim "
                    "gewaehlten Thema, auch wenn einzelne Woerter mehrdeutig sind. Die "
                    "Erzaehlung soll etwa 30 bis 45 Sekunden dauern. Stelle "
                    "anschliessend genau eine passende offene Frage und warte auf die "
                    "Antwort des Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_kirchen",
            "When the user input is exactly HMBKRITZRAEDT_KIRCHEN, always call "
            "this tool. Topic: Kirchen und religioeses Leben im 17. "
            "Jahrhundert, nicht moderne Kirchenpolitik. Execute the returned "
            "instruction. Do not mention the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: kirchen");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: Kirchen, Glaubensalltag und religioese "
                    "Einrichtungen im 17. Jahrhundert in der Region Gangelt und "
                    "Sittard. Unterscheide belegte Bau- und Ortsgeschichte von "
                    "allgemeinen Braeuchen. Beginne unmittelbar mit einer lebendigen "
                    "historischen Erzaehlung in der Ich-Form und auf Deutsch. Sprich "
                    "aus der historischen Perspektive, ohne vorzugeben, unbelegte "
                    "Ereignisse selbst erlebt zu haben. Erfinde keine Fakten, Zitate "
                    "oder Quellen. Kennzeichne Unsicherheiten und vermeide "
                    "Anachronismen. Bleibe beim gewaehlten Thema, auch wenn einzelne "
                    "Woerter mehrdeutig sind. Die Erzaehlung soll etwa 30 bis 45 "
                    "Sekunden dauern. Stelle anschliessend genau eine passende offene "
                    "Frage und warte auf die Antwort des Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_alltag",
            "When the user input is exactly HMBKRITZRAEDT_ALLTAG, always call "
            "this tool. Topic: Alltagsleben im 17. Jahrhundert, nicht heutige "
            "Tagesnachrichten. Execute the returned instruction. Do not mention "
            "the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: alltag");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: Alltagsleben der Menschen im 17. Jahrhundert "
                    "in der Region: Familie, Wohnen, Ernaehrung, Arbeit und soziale "
                    "Unterschiede. Vermeide moderne Verhaeltnisse als historische "
                    "Tatsachen. Beginne unmittelbar mit einer lebendigen historischen "
                    "Erzaehlung in der Ich-Form und auf Deutsch. Sprich aus der "
                    "historischen Perspektive, ohne vorzugeben, unbelegte Ereignisse "
                    "selbst erlebt zu haben. Erfinde keine Fakten, Zitate oder Quellen. "
                    "Kennzeichne Unsicherheiten und vermeide Anachronismen. Bleibe beim "
                    "gewaehlten Thema, auch wenn einzelne Woerter mehrdeutig sind. Die "
                    "Erzaehlung soll etwa 30 bis 45 Sekunden dauern. Stelle "
                    "anschliessend genau eine passende offene Frage und warte auf die "
                    "Antwort des Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_handel",
            "When the user input is exactly HMBKRITZRAEDT_HANDEL, always call "
            "this tool. Topic: Warenhandel und Maerkte im 17. Jahrhundert, "
            "ausdruecklich nicht der Komponist Georg Friedrich Haendel. Execute "
            "the returned instruction. Do not mention the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: handel");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: Warenhandel und Wirtschaft im 17. Jahrhundert "
                    "in Gangelt, Sittard und Umgebung: Kaufleute, Maerkte, Waren, "
                    "Handwerk und Handelswege. Mit Handel ist Warenhandel gemeint, "
                    "NICHT Georg Friedrich Haendel, Musik oder Oper. Beginne "
                    "unmittelbar mit einer lebendigen historischen Erzaehlung in der "
                    "Ich-Form und auf Deutsch. Sprich aus der historischen Perspektive, "
                    "ohne vorzugeben, unbelegte Ereignisse selbst erlebt zu haben. "
                    "Erfinde keine Fakten, Zitate oder Quellen. Kennzeichne "
                    "Unsicherheiten und vermeide Anachronismen. Bleibe beim gewaehlten "
                    "Thema, auch wenn einzelne Woerter mehrdeutig sind. Die Erzaehlung "
                    "soll etwa 30 bis 45 Sekunden dauern. Stelle anschliessend genau "
                    "eine passende offene Frage und warte auf die Antwort des "
                    "Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_geschichte",
            "When the user input is exactly HMBKRITZRAEDT_GESCHICHTE, always "
            "call this tool. Topic: Historische Ereignisse des 17. Jahrhunderts "
            "in der Region, nicht eine beliebige Epoche. Execute the returned "
            "instruction. Do not mention the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: geschichte");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: historisch belegte Ereignisse und Umbrueche "
                    "des 17. Jahrhunderts, soweit sie Gangelt, Sittard und ihre "
                    "Umgebung betreffen. Erklaere Zusammenhaenge, ohne unbelegte lokale "
                    "Einzelheiten zu erfinden. Beginne unmittelbar mit einer lebendigen "
                    "historischen Erzaehlung in der Ich-Form und auf Deutsch. Sprich "
                    "aus der historischen Perspektive, ohne vorzugeben, unbelegte "
                    "Ereignisse selbst erlebt zu haben. Erfinde keine Fakten, Zitate "
                    "oder Quellen. Kennzeichne Unsicherheiten und vermeide "
                    "Anachronismen. Bleibe beim gewaehlten Thema, auch wenn einzelne "
                    "Woerter mehrdeutig sind. Die Erzaehlung soll etwa 30 bis 45 "
                    "Sekunden dauern. Stelle anschliessend genau eine passende offene "
                    "Frage und warte auf die Antwort des Besuchers. "
                );
            }
        );

        mcp.AddTool(
            "self.hmbtec.kritzraedt_geheimnisse",
            "When the user input is exactly HMBKRITZRAEDT_GEHEIMNISSE, always "
            "call this tool. Topic: Historische Raetsel, Ueberlieferungen und "
            "Legenden der Region, keine erfundenen Geheimnisse. Execute the "
            "returned instruction. Do not mention the trigger or tool. ",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                ESP_LOGI(TAG, "Kritzraedt MCP: geheimnisse");

                return std::string(
                    "Du bist Jacobus Kritzraedt, Chronist des 17. Jahrhunderts. Das "
                    "gewaehlte Thema ist: historische Raetsel, ungewoehnliche "
                    "Ueberlieferungen und Legenden rund um Gangelt und Sittard. "
                    "Kennzeichne Legenden als Legenden und offene Fragen als "
                    "ungeklaert; erfinde keine angeblichen Quellen oder Geheimnisse. "
                    "Beginne unmittelbar mit einer lebendigen historischen Erzaehlung "
                    "in der Ich-Form und auf Deutsch. Sprich aus der historischen "
                    "Perspektive, ohne vorzugeben, unbelegte Ereignisse selbst erlebt "
                    "zu haben. Erfinde keine Fakten, Zitate oder Quellen. Kennzeichne "
                    "Unsicherheiten und vermeide Anachronismen. Bleibe beim gewaehlten "
                    "Thema, auch wenn einzelne Woerter mehrdeutig sind. Die Erzaehlung "
                    "soll etwa 30 bis 45 Sekunden dauern. Stelle anschliessend genau "
                    "eine passende offene Frage und warte auf die Antwort des "
                    "Besuchers. "
                );
            }
        );

    }

    void InitializeTools() {
        static BuzzerController buzzer(HMBTEC_BUZZER_GPIO);

        light_controller_ = new HmbtecLightController(pixel_ring_);

        auto& app = Application::GetInstance();

        app.RegisterOneShotFinishedCallback([this]() {
            if (light_controller_ != nullptr) {
                ESP_LOGI(TAG, "Lichtblick one-shot finished -> start 5s afterglow");
                light_controller_->FinishLichtblick();
            }
        });
    }

public:
public:
    HmbtecKritzraedt(): boot_button_(BOOT_BUTTON_GPIO){
        InitializeI2c();
        InitializeBatteryMonitor();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        // InitializeBuzzer(); // Kritzraedt: nicht benötigt // HMB
        // InitializePixelRing(); // Kritzraedt: nicht benötigt // HMB
        // InitializeTools(); // Kritzraedt: nicht benötigt
        InitializeKritzraedtTools();
        #ifdef NFC_EN
                ESP_LOGW(TAG, "HMB NFC DIAG: InitializeNfc wird aufgerufen");
                InitializeNfc();
        #endif
        GetBacklight()->SetBrightness(100);
    }

    virtual Led *GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, AUDIO_CODEC_I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override { return display_; }

    virtual Backlight *GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        return true;
    }
};


DECLARE_BOARD(HmbtecKritzraedt);
