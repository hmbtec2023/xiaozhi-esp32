/*
 * HMB | TEC Build Information
 * PROJECT_NAME: HMBTEC_MemoryStand_C3
 * PROJECT_VERSION: 0.6.0
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
#include "ir_remote_controller.h"
#include "mcp_server.h"
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <inttypes.h>

#define TAG "HMBMemoryStandC3"

static constexpr uint16_t IR_NEC_ADDRESS  = 0x0000; // 0xEF00; // Extended NEC, 16-Bit-Adresse
static constexpr uint8_t IR_NEC_RED       = 0x19;
static constexpr uint8_t IR_NEC_RED_1     = 0x17;
static constexpr uint8_t IR_NEC_RED_2     = 0x0A;

static constexpr uint8_t IR_NEC_BLUE      = 0x11;
static constexpr uint8_t IR_NEC_BLUE_1    = 0x16;
static constexpr uint8_t IR_NEC_BLUE_2    = 0x04;
static constexpr uint8_t IR_NEC_BLUE_3    = 0x0E;

static constexpr uint8_t IR_NEC_GREEN     = 0x1B;
static constexpr uint8_t IR_NEC_GREEN_1   = 0x12;
static constexpr uint8_t IR_NEC_GREEN_2   = 0x4C;
static constexpr uint8_t IR_NEC_GREEN_3   = 0x1E;

static constexpr uint8_t IR_NEC_CYAN    = 0x14;
static constexpr uint8_t IR_NEC_PURPLE  = 0x0F;
static constexpr uint8_t IR_NEC_ORANGE  = 0x40;

static constexpr uint8_t IR_NEC_YELLOW  = 0x1C;
static constexpr uint8_t IR_NEC_WEISS   = 0x15;

static constexpr uint8_t IR_NEC_OFF     = 0x1F;
static constexpr uint8_t IR_NEC_ON      = 0x0D;
static constexpr uint8_t IR_NEC_FLASH   = 0x4D;
static constexpr uint8_t IR_NEC_STROBE  = 0x00;
static constexpr uint8_t IR_NEC_FADE    = 0x1A;
static constexpr uint8_t IR_NEC_SMOOTH  = 0x0C;

static rmt_channel_handle_t ir_tx_channel = nullptr;
static rmt_encoder_handle_t ir_copy_encoder = nullptr;

// Zuletzt per IR-Fernbedienung gewählte Farbe
static uint8_t ir_selected_color = IR_NEC_WEISS;

static void InitializeIrTransmitter() {
    #if HMB_IR_ENABLED
        rmt_tx_channel_config_t tx_config = {};
        tx_config.gpio_num = IR_TX_GPIO;
        tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
        tx_config.resolution_hz = 1000000; // 1 Tick = 1 us
        tx_config.mem_block_symbols = 64;
        tx_config.trans_queue_depth = 4;
        esp_err_t err = rmt_new_tx_channel(&tx_config, &ir_tx_channel);
        if (err != ESP_OK) { ESP_LOGE(TAG, "IR RMT TX: %s", esp_err_to_name(err)); return; }

        rmt_carrier_config_t carrier = {};
        carrier.frequency_hz = 38000;
        carrier.duty_cycle = 0.33f;
        carrier.flags.polarity_active_low = false;
        err = rmt_apply_carrier(ir_tx_channel, &carrier);
        if (err != ESP_OK) { ESP_LOGE(TAG, "IR carrier: %s", esp_err_to_name(err)); return; }

        rmt_copy_encoder_config_t encoder_config = {};
        err = rmt_new_copy_encoder(&encoder_config, &ir_copy_encoder);
        if (err != ESP_OK) { ESP_LOGE(TAG, "IR encoder: %s", esp_err_to_name(err)); return; }
        err = rmt_enable(ir_tx_channel);
        if (err != ESP_OK) { ESP_LOGE(TAG, "IR enable: %s", esp_err_to_name(err)); return; }
        ESP_LOGI(TAG, "IR Extended NEC GPIO10 / 38kHz bereit");
    #endif
}

// ============================================================
// Universeller IR-NEC-Sender
// GPIO10 / 38 kHz / Adresse 0x0000
// ============================================================

static void SendIrCommand(uint8_t command) {
#if HMB_IR_ENABLED
    if (!ir_tx_channel || !ir_copy_encoder) return;

    const uint32_t nec_data =
        static_cast<uint32_t>(IR_NEC_ADDRESS) |
        (static_cast<uint32_t>(command) << 16) |
        (static_cast<uint32_t>(command ^ 0xFFU) << 24);

    rmt_symbol_word_t symbols[34] = {};

    symbols[0] = {
        .duration0 = 9000,
        .level0 = 1,
        .duration1 = 4500,
        .level1 = 0
    };

    for (int bit = 0; bit < 32; ++bit) {
        const bool one = ((nec_data >> bit) & 0x01U) != 0;

        symbols[bit + 1] = {
            .duration0 = 560,
            .level0 = 1,
            .duration1 = static_cast<uint16_t>(one ? 1690 : 560),
            .level1 = 0
        };
    }

    symbols[33] = {
        .duration0 = 560,
        .level0 = 1,
        .duration1 = 560,
        .level1 = 0
    };

    rmt_transmit_config_t config = {};
    config.loop_count = 0;

    esp_err_t err = rmt_transmit(
        ir_tx_channel,
        ir_copy_encoder,
        symbols,
        sizeof(symbols),
        &config
    );

    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(
            ir_tx_channel,
            pdMS_TO_TICKS(1000)
        );
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IR TX Fehler: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG,
                 "IR TX: Address=0x%04X Command=0x%02X Raw=0x%08" PRIX32,
                 IR_NEC_ADDRESS,
                 command,
                 nec_data);
    }
#else
    (void)command;
#endif
}


static void RegisterLightMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.light.set_color",
        "Setzt die Farbe der MemoryStand-Lampe.",
        PropertyList({
            Property("color", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            const std::string color =
                properties["color"].value<std::string>();

            uint8_t command;

            if      (color == "red"    || color == "rot")     command = IR_NEC_RED;
            else if (color == "green"  || color == "grün" ||
                    color == "gruen")                        command = IR_NEC_GREEN;
            else if (color == "blue"   || color == "blau")    command = IR_NEC_BLUE;
            else if (color == "cyan"   || color == "türkis" ||
                    color == "tuerkis")                      command = IR_NEC_CYAN;
            else if (color == "purple" || color == "violett" ||
                    color == "lila")                         command = IR_NEC_PURPLE;
            else if (color == "orange")                       command = IR_NEC_ORANGE;
            else if (color == "yellow" || color == "gelb")    command = IR_NEC_YELLOW;
            else if (color == "white" || color == "weiß" || color == "weiss") command = IR_NEC_WEISS;
            else return std::string("Unbekannte Farbe: ") + color;

            ir_selected_color = command;
            SendIrCommand(command);

            return std::string("Lichtfarbe gesetzt");
        }
    );
    mcp.AddTool(
        "self.hmbtec.get_affirmation",
        "When the user input is exactly HMBPROMPT, "
        "always call this tool. Execute the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {

            std::string color;

            switch (ir_selected_color) {
                case IR_NEC_RED:
                case IR_NEC_RED_1:
                case IR_NEC_RED_2:
                    color = "Rot";
                    break;

                case IR_NEC_GREEN:
                case IR_NEC_GREEN_1:
                case IR_NEC_GREEN_2:
                case IR_NEC_GREEN_3:
                    color = "Gruen";
                    break;

                case IR_NEC_BLUE:
                case IR_NEC_BLUE_1:
                case IR_NEC_BLUE_2:
                case IR_NEC_BLUE_3:
                    color = "Blau";
                    break;

                case IR_NEC_CYAN:   color = "Tuerkis"; break;
                case IR_NEC_PURPLE: color = "Violett"; break;
                case IR_NEC_ORANGE: color = "Orange"; break;
                case IR_NEC_YELLOW: color = "Gelb"; break;
                default:            color = "Weiss"; break;
            }

            return std::string(
                "Erzeuge eine kurze, persoenliche Affirmation "
                "auf Deutsch zur bewusst gewaehlten Farbe "
            ) + color +
            ". Sprich die Affirmation direkt aus. "
            "Keine Rueckfragen, keine Erklaerungen.";
        }
    );
    mcp.AddTool(
        "self.hmbtec.start_seelsorge",
        "When the user input is exactly HMBSEELSORGE, "
        "always call this tool. Execute the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {

            std::string color;

            switch (ir_selected_color) {
                case IR_NEC_RED:
                case IR_NEC_RED_1:
                case IR_NEC_RED_2:
                    color = "Rot";
                    break;

                case IR_NEC_GREEN:
                case IR_NEC_GREEN_1:
                case IR_NEC_GREEN_2:
                case IR_NEC_GREEN_3:
                    color = "Gruen";
                    break;

                case IR_NEC_BLUE:
                case IR_NEC_BLUE_1:
                case IR_NEC_BLUE_2:
                case IR_NEC_BLUE_3:
                    color = "Blau";
                    break;

                case IR_NEC_CYAN:   color = "Tuerkis"; break;
                case IR_NEC_PURPLE: color = "Violett"; break;
                case IR_NEC_ORANGE: color = "Orange"; break;
                case IR_NEC_YELLOW: color = "Gelb"; break;
                default:            color = "Weiss"; break;
            }

            return std::string(
                "Du bist ein ruhiger, empathischer Gespraechsbegleiter "
                "im HMBTEC MemoryStand. "
                "Der Nutzer hat bewusst die Lichtfarbe "
            ) + color +
            " gewaehlt. Betrachte diese Farbe als moeglichen "
            "Stimmungskontext, nicht als sichere Emotion. "
            "Beginne jetzt mit einer kurzen, persoenlichen "
            "und offenen Frage auf Deutsch. "
            "Hoere danach aufmerksam zu und fuehre "
            "das Gespraech behutsam weiter. "
            "Keine Diagnosen, keine ungefragten Ratschlaege. "
            "Erwaehne weder MCP noch HMBSEELSORGE.";
        }
    );
}


class HmbtecMemoryStandC3Board : public WifiBoard {

private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    Button boot_button_;
    HmbtecIrRemoteController ir_receiver_{IR_RX_GPIO};

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

        ESP_LOGI(TAG, "HMBTEC MemoryStand C3 V0.6.0");

        InitializeI2C();
        InitializeDisplay();
        InitializeButtons();
        InitializeIrTransmitter();
        RegisterLightMcpTools();
            ir_receiver_.SetCommandCallback([](uint8_t command) {
                ESP_LOGI(TAG, "IR RX=0x%02X", command);
                switch (command) {
                    case IR_NEC_RED:
                    case IR_NEC_RED_1:
                    case IR_NEC_RED_2:
                    case IR_NEC_GREEN:
                    case IR_NEC_GREEN_1:
                    case IR_NEC_GREEN_2:
                    case IR_NEC_GREEN_3:
                    case IR_NEC_BLUE:
                    case IR_NEC_BLUE_1:
                    case IR_NEC_BLUE_2:
                    case IR_NEC_BLUE_3:
                    case IR_NEC_CYAN:
                    case IR_NEC_PURPLE:
                    case IR_NEC_ORANGE:
                    case IR_NEC_YELLOW:
                    case IR_NEC_WEISS:
                        ir_selected_color = command;
                        ESP_LOGI(TAG, "IR FARBE gespeichert: 0x%02X", command);
                        break;
                    case IR_NEC_FLASH:
                        ESP_LOGI(TAG,
                                "AFFIRMATION: FLASH erkannt, Farbe=0x%02X",
                                ir_selected_color);

                        // FLASH-Modus der Lampe beenden
                        // und zuletzt gewählte Farbe wiederherstellen
                        vTaskDelay(pdMS_TO_TICKS(500));
                        SendIrCommand(ir_selected_color);
                        ESP_LOGI(TAG, "AFFIRMATION: Farbe wiederhergestellt");
                        Application::GetInstance().WakeWordInvoke("HMBPROMPT", true);
                        ESP_LOGI(TAG, "AFFIRMATION: XiaoZhi-Dialog angefordert");
                        break;

                    case IR_NEC_STROBE:
                        ESP_LOGI(TAG,
                                 "SEELSORGE: STROBE erkannt, Farbe=0x%02X",
                                 ir_selected_color);

                        // STROBE beenden und vorherige Farbe wiederherstellen
                        vTaskDelay(pdMS_TO_TICKS(500));
                        SendIrCommand(ir_selected_color);

                        ESP_LOGI(TAG, "SEELSORGE: Farbe wiederhergestellt");

                        // XiaoZhi-Seelsorge über MCP auslösen
                        Application::GetInstance().WakeWordInvoke(
                            "HMBSEELSORGE", false
                        );

                        ESP_LOGI(TAG, "SEELSORGE: XiaoZhi-Dialog angefordert");
                        break;
    
                        default:
                        break;
                }
            });
            if (!ir_receiver_.Initialize()) {
                ESP_LOGE(TAG, "IR RX INITIALISIERUNG FEHLGESCHLAGEN!");
            } else {
                ESP_LOGI(TAG, "IR RX BEREIT - GPIO%d", static_cast<int>(IR_RX_GPIO));
            }

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