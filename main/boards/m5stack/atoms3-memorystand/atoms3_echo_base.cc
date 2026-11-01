#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "assets/lang_config.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>
#include <inttypes.h>

#include <driver/uart.h> // for LD2410 Radar
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include "ir_remote_controller.h"
#include <cstring>  // memcmp(), memmove()
#include <lvgl.h>
#include "mcp_server.h"

#define TAG "HMBTEC MemoryStand"
//-------------------------------------------------------------
#define HMB_RADAR_DEFAULT_ENABLED  0  // 1 = EIN, 0 = AUS
//-------------------------------------------------------------

// HMB|TEC Firmware-Version (bei jeder Aenderung hochzaehlen)
#define HMB_FIRMWARE_VERSION "0.5.4"

#include <led_strip.h>

// HMB|TEC MemoryStand – LD2410 + WS2812 an GPIO1
// 0 ABSENT, 1 PRESENT, 2 NEAR, 3 DIALOG, 4 LEFT
extern "C" {
    volatile uint8_t radar_display_state = 0;
}

static constexpr gpio_num_t STATUS_PIXEL_GPIO = GPIO_NUM_1;
static constexpr uint32_t PRESENCE_CONFIRM_MS = 3000;
static constexpr uint32_t ABSENCE_CONFIRM_MS = 3000;
static constexpr uint32_t NEAR_CONFIRM_MS = 10000;
static constexpr uint32_t FAR_CONFIRM_MS = 10000;
static constexpr uint16_t NEAR_DISTANCE_CM = 100;

// HMB|TEC IR-Statusleuchte (Extended NEC, 38 kHz).
// Adresse und Kommandos gemaess Vorgabe; Kompatibilitaet mit Leuchte pruefen.
#define HMB_IR_ENABLED 1
#define HMB_IR_RX_ENABLED 1
static constexpr gpio_num_t IR_RX_GPIO    = GPIO_NUM_2; // Externer IR-Demodulator, Pinbelegung pruefen
static constexpr gpio_num_t IR_TX_GPIO    = GPIO_NUM_4; // M5Stack ATOM S3 integrierte IR-LED
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
        ESP_LOGI(TAG, "IR Extended NEC GPIO4 / 38kHz bereit");
    #endif
}

// ============================================================
// Universeller IR-NEC-Sender
// GPIO4 / 38 kHz / Adresse 0x0000
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

// ============================================================
// Radar-Status -> IR-Farbkommando
// ============================================================
// true: Sprachsteuerung hat Vorrang vor Radar
// false: Radar steuert die Lichtfarbe automatisch
static bool light_manual_override = false;
// HMB|TEC MemoryStand – Betriebsmodus
// true  = Radar steuert Licht und Dialog
// false = IR-Fernbedienung steuert Licht und Dialog
static bool radar_enabled = (HMB_RADAR_DEFAULT_ENABLED != 0);

static void SendIrStatus(uint8_t state) {
    uint8_t command;

    switch (state) {
        case 0: command = IR_NEC_BLUE;   break;
        case 1: command = IR_NEC_GREEN;  break;
        case 2: command = IR_NEC_CYAN;   break;
        case 3: command = IR_NEC_PURPLE; break;
        case 4: command = IR_NEC_ORANGE; break;
        default: return;
    }
    SendIrCommand(command);
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

            light_manual_override = true;
            SendIrCommand(command);

            return std::string("Lichtfarbe gesetzt");
        }
    );
    mcp.AddTool(
        "self.radar.set_enabled",
        "Aktiviert oder deaktiviert die Radarautomatik des MemoryStand. "
        "Bei deaktiviertem Radar übernimmt die IR-Fernbedienung die Steuerung.",
        PropertyList({
            Property("enabled", kPropertyTypeBoolean)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            bool enabled = properties["enabled"].value<bool>();

            radar_enabled = enabled;

            ESP_LOGI(TAG, "HMB RADAR: %s",
                    enabled ? "ENABLED" : "DISABLED");

            return enabled
                ? std::string("Radarautomatik aktiviert")
                : std::string("Radarautomatik deaktiviert");
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
}

static led_strip_handle_t status_pixel = nullptr;

static void SetStatusPixel(uint8_t state) {
    if (!status_pixel) return;
    uint8_t r = 0, g = 0, b = 0;
    switch (state) {
        case 0: b = 32; break;                   // ABSENT: Blau
        case 1: g = 32; break;                   // PRESENT: Gruen
        case 2: g = 25; b = 25; break;           // NEAR: Tuerkis
        case 3: r = 25; b = 25; break;           // DIALOG: Violett
        case 4: r = 32; g = 10; break;           // LEFT: Orange
    }
    led_strip_set_pixel(status_pixel, 0, r, g, b);
    led_strip_refresh(status_pixel);
}

static void InitializeStatusPixel() {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = STATUS_PIXEL_GPIO;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &status_pixel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NeoPixel GPIO1: %s", esp_err_to_name(err));
        return;
    }
    SetStatusPixel(0);
}

static void Ld2410Task(void* arg) {
    constexpr uint8_t HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1};
    constexpr uint8_t FOOTER[] = {0xF8, 0xF7, 0xF6, 0xF5};
    uint8_t buffer[128] = {};
    uint8_t rx[64];
    size_t used = 0;
    int last_presence = -1;
    bool confirmed_presence = false;
    bool near_confirmed = false;
    // Dialog nur beenden, wenn er durch den Radar gestartet wurde.
    bool radar_dialog_owned = false;
    uint32_t last_wake_attempt_ms = 0;
    bool wake_attempted = false;
    uint32_t left_until = 0;
    uint32_t presence_since = 0;
    uint32_t absence_since = 0;
    uint32_t near_since = 0;
    uint32_t far_since = 0;
    uint8_t previous_display_state = 255;

    while (true) {
        if (!radar_enabled) {
            if (radar_dialog_owned) {
                radar_dialog_owned = false;
                Application::GetInstance().EndConversation();
                ESP_LOGI(TAG, "RADAR DISABLED: Radardialog beendet");
            }
            used = 0;
            confirmed_presence = false;
            near_confirmed = false;
            presence_since = 0;
            absence_since = 0;
            near_since = 0;
            far_since = 0;
            wake_attempted = false;
            last_wake_attempt_ms = 0;
            left_until = 0;
            last_presence = -1;
            previous_display_state = 255;

            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int len = uart_read_bytes(UART_NUM_1, rx, sizeof(rx), pdMS_TO_TICKS(200));
        for (int i = 0; i < len; ++i) {
            if (used >= sizeof(buffer)) memmove(buffer, buffer + 1, --used);
            buffer[used++] = rx[i];
            while (used >= 4) {
                if (memcmp(buffer, HEADER, 4) != 0) {
                    memmove(buffer, buffer + 1, --used);
                    continue;
                }
                if (used < 6) break;
                uint16_t n = buffer[4] | (buffer[5] << 8);
                if (n < 13 || n > 96) {
                    memmove(buffer, buffer + 1, --used);
                    continue;
                }
                size_t total = 6 + n + 4;
                if (used < total) break;
                if (memcmp(buffer + 6 + n, FOOTER, 4) == 0 &&
                    buffer[6] == 0x02 && buffer[7] == 0xAA &&
                    buffer[6 + n - 2] == 0x55 && buffer[6 + n - 1] == 0x00 &&
                    buffer[8] <= 3) {
                    uint8_t raw_state = buffer[8];
                    bool detected = raw_state != 0;
                    uint16_t moving_cm = buffer[9] | (buffer[10] << 8);
                    uint8_t moving_energy = buffer[11];
                    uint16_t still_cm = buffer[12] | (buffer[13] << 8);
                    uint8_t still_energy = buffer[14];
                    uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
                    // Nur gueltige Ziele im Nahbereich; Distanz 0 gilt nicht als Ziel.
                    bool near_raw = detected &&
                        (((raw_state & 1) && moving_cm > 0 && moving_cm <= NEAR_DISTANCE_CM && moving_energy > 0) ||
                         ((raw_state & 2) && still_cm > 0 && still_cm <= NEAR_DISTANCE_CM && still_energy > 0));

                    if (detected) {
                        absence_since = 0;
                        if (!confirmed_presence) {
                            if (presence_since == 0) presence_since = now;
                            if (now - presence_since >= PRESENCE_CONFIRM_MS) {
                                confirmed_presence = true;
                                ESP_LOGI(TAG, "PRESENCE bestaetigt (3s)");
                            }
                        }
                    } else {
                        presence_since = 0;
                        if (confirmed_presence) {
                            if (absence_since == 0) absence_since = now;
                            if (now - absence_since >= ABSENCE_CONFIRM_MS) {
                                confirmed_presence = false;
                                ESP_LOGI(TAG, "ABSENCE bestaetigt (3s)");
                            }
                        }
                    }

                    if (confirmed_presence && near_raw) {
                        far_since = 0;
                        if (!near_confirmed) {
                            if (near_since == 0) near_since = now;
                            if (now - near_since >= NEAR_CONFIRM_MS) {
                                near_confirmed = true;
                                ESP_LOGI(TAG, "NEAR bestaetigt (10s)");
                            }
                        }
                    } else {
                        near_since = 0;
                        if (near_confirmed) {
                            if (far_since == 0) far_since = now;
                            if (now - far_since >= FAR_CONFIRM_MS) {
                                near_confirmed = false;
                                wake_attempted = false;
                                last_wake_attempt_ms = 0;
                                ESP_LOGI(TAG, "LEFT bestaetigt (10s)");
                                if (radar_dialog_owned) {
                                    radar_dialog_owned = false;
                                    Application::GetInstance().EndConversation();
                                    ESP_LOGI(TAG, "RADAR: KI-Dialogende angefordert");
                                }
                                left_until = now + 1500;
                            }
                        }
                    }

                    // Wakeword erst im IDLE-Zustand ausloesen. IDLE allein garantiert
                    // keine Serververbindung: Diese wird durch XiaoZhi erst aufgebaut.
                    // Erst bei LISTENING ist der Start bestaetigt; bei Rueckkehr zu
                    // IDLE nach fehlgeschlagenem Verbindungsaufbau erneut versuchen.
                    if (near_confirmed && !radar_dialog_owned) {
                        auto& app = Application::GetInstance();
                        const auto state = app.GetDeviceState();
                        if (wake_attempted && state == kDeviceStateListening) {
                            radar_dialog_owned = true;
                            ESP_LOGI(TAG, "RADAR: KI-Dialog aktiv (LISTENING)");
                        } else if (state == kDeviceStateIdle &&
                                   (!wake_attempted || now - last_wake_attempt_ms >= 5000)) {
                            wake_attempted = true;
                            last_wake_attempt_ms = now;
                            Application::GetInstance().WakeWordInvoke("Hallo");
                            ESP_LOGI(TAG, "RADAR: Wakeword Hallo angefordert (NEAR)");
                        }
                    }

                    // LEFT kurz signalisieren, danach PRESENT oder ABSENT.
                    uint8_t display_state = (left_until != 0 && static_cast<int32_t>(left_until - now) > 0) ? 4 :
                                            !confirmed_presence ? 0 :
                                            near_confirmed ? (radar_dialog_owned ? 3 : 2) : 1;
                    // Nach Dialogende manuelle Lichtsteuerung aufheben
                    if (light_manual_override &&
                        radar_dialog_owned &&
                        Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {

                        light_manual_override = false;
                        previous_display_state = 255;  // IR-Status erneut senden

                        ESP_LOGI(TAG, "LIGHT: Dialog beendet -> RADAR AUTO");
                    }
                    if (display_state != previous_display_state) {
                        previous_display_state = display_state;
                        radar_display_state = display_state;
                        SetStatusPixel(display_state);
                        if (!light_manual_override) {
                            SendIrStatus(display_state);
                        }
                        ESP_LOGI(TAG, "MemoryStand Display-State=%u", display_state);
                    }
                    if (static_cast<int>(detected) != last_presence) {
                        last_presence = detected;
                        ESP_LOGI(TAG, "LD2410 PRESENCE=%s STATE=%u MOVE=%ucm/%u STATIC=%ucm/%u",
                                 detected ? "YES" : "NO", raw_state,
                                 moving_cm, moving_energy, still_cm, still_energy);
                    }
                }
                memmove(buffer, buffer + total, used - total);
                used -= total;
            }
        }
    }
}

static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb2, (uint8_t[]){0x2f}, 1, 0},
    {0xb3, (uint8_t[]){0x03}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x01}, 1, 0},
    {0xac, (uint8_t[]){0xcb}, 1, 0},
    {0xab, (uint8_t[]){0x0e}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x19}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xe8, (uint8_t[]){0x24}, 1, 0},
    {0xe9, (uint8_t[]){0x48}, 1, 0},
    {0xea, (uint8_t[]){0x22}, 1, 0},
    {0xc6, (uint8_t[]){0x30}, 1, 0},
    {0xc7, (uint8_t[]){0x18}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1f, 0x28, 0x04, 0x3e, 0x2a, 0x2e, 0x20, 0x00, 0x0c, 0x06,
                0x00, 0x1c, 0x1f, 0x0f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x00, 0x2d, 0x2f, 0x3c, 0x6f, 0x1c, 0x0b, 0x00, 0x00, 0x00,
                0x07, 0x0d, 0x11, 0x0f},
    14, 0},
};

class AtomS3MemoryStandBoard : public WifiBoard {
private:
    enum class LcdPanelType {
        kGc9107,
        kSt7735,
    };

    struct LcdPanelConfig {
        LcdPanelType type;
        const char* name;
        int gap_x;
        int gap_y;
        bool mirror_x;
        bool mirror_y;
        bool swap_xy;
        bool invert_color;
    };

    Display* display_;
    Button boot_button_;

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize SPI bus");
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = LCD_MOSI_GPIO;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = LCD_SCLK_GPIO;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void ResetLcdPanel() {
        gpio_config_t reset_config = {};
        reset_config.pin_bit_mask = BIT64(LCD_RST_GPIO);
        reset_config.mode = GPIO_MODE_OUTPUT;
        gpio_config(&reset_config);

        gpio_set_level(LCD_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(LCD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    uint32_t ReadLcdPanelId() {
        ResetLcdPanel();

        gpio_config_t cs_config = {};
        cs_config.pin_bit_mask = BIT64(LCD_CS_GPIO);
        cs_config.mode = GPIO_MODE_OUTPUT;
        gpio_config(&cs_config);
        gpio_set_level(LCD_CS_GPIO, 1);

        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = 16 * 1000 * 1000;
        devcfg.mode = 0;
        devcfg.spics_io_num = GPIO_NUM_NC;
        devcfg.queue_size = 1;
        devcfg.command_bits = 8;
        devcfg.dummy_bits = 1;
        devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE;

        spi_device_handle_t spi = nullptr;
        esp_err_t ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add SPI device for panel ID read: %s", esp_err_to_name(ret));
            return 0;
        }

        spi_transaction_t wake = {};
        wake.length = 8;
        wake.flags = SPI_TRANS_USE_TXDATA;
        wake.tx_data[0] = 0x00;
        ret = spi_device_polling_transmit(spi, &wake);

        spi_transaction_t trans = {};
        trans.cmd = 0x04; // RDDID
        trans.rxlength = 32;
        trans.flags = SPI_TRANS_USE_RXDATA;
        gpio_set_level(LCD_CS_GPIO, 0);
        if (ret == ESP_OK) {
            ret = spi_device_polling_transmit(spi, &trans);
        }
        gpio_set_level(LCD_CS_GPIO, 1);
        spi_bus_remove_device(spi);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read LCD panel ID: %s", esp_err_to_name(ret));
            return 0;
        }

        uint32_t id = static_cast<uint32_t>(trans.rx_data[0]) |
                      (static_cast<uint32_t>(trans.rx_data[1]) << 8) |
                      (static_cast<uint32_t>(trans.rx_data[2]) << 16) |
                      (static_cast<uint32_t>(trans.rx_data[3]) << 24);
        ESP_LOGI(TAG, "LCD panel ID: 0x%08" PRIx32, id);
        return id;
    }

    LcdPanelConfig GetLcdPanelConfig(LcdPanelType type) {
        if (type == LcdPanelType::kSt7735) {
            return {
                .type = type,
                .name = "ST7735",
                .gap_x = 2,
                .gap_y = 3,
                .mirror_x = true,
                .mirror_y = true,
                .swap_xy = false,
                .invert_color = true,
            };
        }

        return {
            .type = LcdPanelType::kGc9107,
            .name = "GC9107",
            .gap_x = DISPLAY_OFFSET_X,
            .gap_y = DISPLAY_OFFSET_Y,
            .mirror_x = DISPLAY_MIRROR_X,
            .mirror_y = DISPLAY_MIRROR_Y,
            .swap_xy = DISPLAY_SWAP_XY,
            .invert_color = false,
        };
    }

    LcdPanelConfig DetectLcdPanel() {
        uint32_t id = ReadLcdPanelId();
        if ((id & 0xFFFF) == 0x7683 || (id & 0xFFFF) == 0x897C) {
            ESP_LOGI(TAG, "Detected ST7735 LCD panel");
            return GetLcdPanelConfig(LcdPanelType::kSt7735);
        }
        if ((id & 0xFFFFFF) == 0x079100) {
            ESP_LOGI(TAG, "Detected GC9107 LCD panel");
            return GetLcdPanelConfig(LcdPanelType::kGc9107);
        }

        ESP_LOGW(TAG, "Unknown LCD panel ID 0x%08" PRIx32 ", fallback to GC9107", id);
        return GetLcdPanelConfig(LcdPanelType::kGc9107);
    }

    void InitializeLcdDisplay(const LcdPanelConfig& config) {
        ESP_LOGI(TAG, "Init %s display", config.name);

        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_handle_t io_handle = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_CS_GPIO;
        io_config.dc_gpio_num = LCD_DC_GPIO;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));
    
        ESP_LOGI(TAG, "Install %s panel driver", config.name);
        esp_lcd_panel_handle_t panel_handle = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = LCD_RST_GPIO; // Set to -1 if not use
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16; // Implemented by LCD command `3Ah` (16/18)

        if (config.type == LcdPanelType::kGc9107) {
            gc9a01_vendor_config_t gc9107_vendor_config = {
                .init_cmds = gc9107_lcd_init_cmds,
                .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
            };
            panel_config.vendor_config = &gc9107_vendor_config;
            ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        } else {
            ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
        }

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, config.invert_color));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, config.swap_xy));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, config.mirror_x, config.mirror_y));
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, config.gap_x, config.gap_y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

        display_ = new SpiLcdDisplay(io_handle, panel_handle,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, config.mirror_x, config.mirror_y, config.swap_xy);
        }

    HmbtecIrRemoteController ir_receiver_{IR_RX_GPIO};

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    AtomS3MemoryStandBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "MemoryStand Firmware V%s gestartet", HMB_FIRMWARE_VERSION);
        InitializeSpi();
        InitializeLcdDisplay(DetectLcdPanel());
        ESP_LOGI(TAG, "LCD-Initialisierung abgeschlossen");

        //CreateRadarStatus();

        InitializeButtons();
        InitializeStatusPixel();
        InitializeIrTransmitter();
        
        RegisterLightMcpTools();  
        ESP_LOGI(TAG, "HMB MCP: self.light.set_color registriert");

        #if HMB_IR_RX_ENABLED
            ir_receiver_.SetCommandCallback([](uint8_t command) {
                if (radar_enabled) {
                    ESP_LOGI(TAG, "IR RX=0x%02X (RADAR AUTO, nur Diagnose)", command);
                    return;
                }

                ESP_LOGI(TAG, "IR RX=0x%02X (IR MANUAL)", command);
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

                    default:
                        break;
                }
            });
                if (!ir_receiver_.Initialize()) {
                    ESP_LOGW(TAG, "IR RX konnte nicht initialisiert werden (GPIO%d)", static_cast<int>(IR_RX_GPIO));
                }
        #endif
        // HMB|TEC MemoryStand – LD2410 UART
        uart_config_t radar_uart = {};
        radar_uart.baud_rate = LD2410_UART_BAUD;
        radar_uart.data_bits = UART_DATA_8_BITS;
        radar_uart.parity = UART_PARITY_DISABLE;
        radar_uart.stop_bits = UART_STOP_BITS_1;
        radar_uart.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

        ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &radar_uart));

        ESP_ERROR_CHECK(uart_set_pin(
            UART_NUM_1,
            LD2410_UART_TX,
            LD2410_UART_RX,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        ));

        ESP_ERROR_CHECK(uart_driver_install(
            UART_NUM_1, 2048, 0, 0, nullptr, 0
        ));

        // LD2410-Empfangstask starten
        BaseType_t task_ok = xTaskCreate(
            Ld2410Task,
            "LD2410",
            3072,
            nullptr,
            5,
            nullptr
        );
        ESP_LOGI(TAG, "LD2410 Task: %s",
                task_ok == pdPASS ? "STARTED" : "FAILED");
                
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT, 256);
        return &backlight;
    }
};

DECLARE_BOARD(AtomS3MemoryStandBoard);
