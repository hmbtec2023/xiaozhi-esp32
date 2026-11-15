#include <atomic>
static std::atomic<bool> hmb_fireplace_requested{false};
static std::atomic<bool> hmb_fireplace_stop_requested{false};
static std::atomic<bool> hmb_fireplace_end_conversation{false};

#include "hmb_api_lab.h"
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
/*
 * HMB | TEC Build Information
 * PROJECT_NAME: HMBTEC_Einschlaf_Coach_C3
 * PROJECT_VERSION: 0.9.26
 * BOARD: ESP32-C3 SuperMini
 * FRAMEWORK: ESP-IDF 6.1 / XiaoZhi
 *
 * FEATURES:
 * - WiFi / XiaoZhi
 * - INMP441 Mikrofon
 * - MAX98357A Lautsprecher
 * - SSD1306 OLED 128x64, I2C 0x3C
 * - TCS34725 RGBC-Sensor, I2C 0x29 / LED via INT
 *
 * OLED: SDA=20, SCL=21
 * I2S: BCLK=4, WS=5, DIN=6, DOUT=7
 */

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "display/display.h"
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
#include <algorithm>
#include <atomic>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <ctime>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "led_strip.h"
#include "device_state_machine.h"
#include "led_strip_spi.h"
#include "assets/lang_config.h"

#define PIXEL_MODE_STATUS  0
#define PIXEL_MODE_LAMP    1
#define HMB_PIXEL_MODE PIXEL_MODE_STATUS  // STATUS oder LAMP
#define HMB_ANSAGE_TEST_MODE 0 // Historischer Testmodus (deaktiviert)
//-----------------------------------------------------------------------------
#define HMB_COLORTALES_ENABLED  0  // 1=ColorTales aktiv, 0=deaktiviert 
#define HMB_COLORTALES_SOUNDS 1  // OGG-Figurensounds aktiv
//-----------------------------------------------------------------------------
#define HMB_TCS34725_ENABLED  0  // 1=Sensor aktiv (für ColorTales), 0=Sensor deaktiviert
#define HMB_OLED_ENABLED      1  // 1=SSD1306 aktiv, 0=ohne OLED (NoDisplay)
#define HMB_IR_TX_CARRIER_ENABLED  0  // 1=38 kHz, 0=unmoduliertes NEC (FET direkt an IR-RX der Lampe)

#define STATUS_NEOPIXEL_GPIO        8
#define STATUS_NEOPIXEL_COUNT      1
#define STATUS_NEOPIXEL_BRIGHTNESS 150  // maximaler RGB-Kanalwert (0..255)

#define TAG "HMBEinschlafCoachC3"

#define HMB_DEEP_SLEEP_SOUND_EMBEDDED 1 // Auf 1 setzen, sobald DEEP_SLEEP.ogg in EMBED_FILES steht
#define HMB_SLEEP_IDLE_MS (5LL * 60 * 1000)
#define HMB_FIREPLACE_MAX_MS (20LL * 60 * 1000)
#define HMB_SLEEP_SOUND_WAIT_MS 2500 // An tatsaechliche OGG-Dauer anpassen

// ============================================================
// HMB|TEC ColorTales
// OGG-Sounds – ESP-IDF EMBED_FILES
// Symbolnamen anhand riscv32-esp-elf-nm korrigiert
// ============================================================

extern const char dino_ogg_start[]
    asm("_binary_DINO_ogg_start");
extern const char dino_ogg_end[]
    asm("_binary_DINO_ogg_end");

extern const char space_ogg_start[]
    asm("_binary_SPACE_ogg_start");
extern const char space_ogg_end[]
    asm("_binary_SPACE_ogg_end");

extern const char magic_ogg_start[]
    asm("_binary_MAGIC_ogg_start");
extern const char magic_ogg_end[]
    asm("_binary_MAGIC_ogg_end");

extern const char detective_ogg_start[]
    asm("_binary_DETECTIVE_ogg_start");
extern const char detective_ogg_end[]
    asm("_binary_DETECTIVE_ogg_end");

extern const char story_ogg_start[]
    asm("_binary_STORY_ogg_start");
extern const char story_ogg_end[]
    asm("_binary_STORY_ogg_end");

extern const char nature_ogg_start[]
    asm("_binary_NATURE_ogg_start");
extern const char nature_ogg_end[]
    asm("_binary_NATURE_ogg_end");

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
static constexpr uint8_t IR_NEC_UP      = 0x09;
static constexpr uint8_t IR_NEC_DOWN    = 0x1D;
static constexpr uint8_t IR_NEC_FLASH   = 0x4D;
static constexpr uint8_t IR_NEC_STROBE  = 0x00;
static constexpr uint8_t IR_NEC_FADE    = 0x1A;
static constexpr uint8_t IR_NEC_SMOOTH  = 0x0C;

static rmt_channel_handle_t ir_tx_channel = nullptr;
static rmt_encoder_handle_t ir_copy_encoder = nullptr;

// Zuletzt per IR-Fernbedienung gewählte Farbe
static uint8_t ir_selected_color = IR_NEC_WEISS;

// ColorTales: Figurenwechsel innerhalb einer laufenden Geschichte.
enum class ColorTalesState : uint8_t {
    CT_OFF,
    CT_ACTIVE,
    CT_SWITCHING,
    CT_PAUSED
};
static ColorTalesState ct_state = ColorTalesState::CT_OFF;
static uint8_t ct_active_color = IR_NEC_WEISS;

static const char* GetColorTalesCharacter(uint8_t color) {
    switch (color) {
        case IR_NEC_RED:    return "DRAGO";
        case IR_NEC_BLUE:   return "FINN";
        case IR_NEC_GREEN:
        case IR_NEC_GREEN_1: return "LILLI";
        case IR_NEC_YELLOW: return "FIPS";
        default:            return nullptr;
    }
}

// ============================================================
// HMB|TEC ColorTales – Figurensound
// ============================================================

static void PlayColorTalesSound(uint8_t color) {

#if HMB_COLORTALES_SOUNDS

    const char* start = nullptr;
    const char* end   = nullptr;
    const char* name  = nullptr;

    switch (color) {

        case IR_NEC_RED:
            start = dino_ogg_start;
            end   = dino_ogg_end;
            name  = "DRAGO";
            break;

        case IR_NEC_BLUE:
            start = space_ogg_start;
            end   = space_ogg_end;
            name  = "FINN";
            break;

        case IR_NEC_GREEN:
            start = magic_ogg_start;
            end   = magic_ogg_end;
            name  = "LILLI";
            break;

        case IR_NEC_GREEN_1:
            start = nature_ogg_start;
            end   = nature_ogg_end;
            name  = "LILLI (NATURE)";
            break;

        case IR_NEC_YELLOW:
            start = detective_ogg_start;
            end   = detective_ogg_end;
            name  = "FIPS";
            break;

        case IR_NEC_WEISS:
            start = story_ogg_start;
            end   = story_ogg_end;
            name  = "STORY";
            break;

        default:
            return;
    }

    if (!start || !end || end <= start) {
        ESP_LOGW(TAG, "COLORTALES: Sound ungueltig");
        return;
    }

    ESP_LOGI(TAG,
             "COLORTALES SOUND: %s (%u Bytes)",
             name,
             static_cast<unsigned>(end - start));

    Application::GetInstance().PlaySound(
        std::string_view(start, end - start)
    );

#endif
}

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

        #if HMB_IR_TX_CARRIER_ENABLED
            rmt_carrier_config_t carrier = {};
            carrier.frequency_hz = 38000;
            carrier.duty_cycle = 0.33f;
            carrier.flags.polarity_active_low = false;

            err = rmt_apply_carrier(ir_tx_channel, &carrier);

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "IR carrier: %s", esp_err_to_name(err));
                return;
            }
        #endif

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

// ============================================================
// HMB|TEC MemoryStand – NeoPixel V0.1
// GPIO8 / 16 WS2812 / ESP-IDF led_strip
// ============================================================

static led_strip_handle_t status_strip = nullptr;

// RGB-Eingaben 0..255 werden auf die schlafgerechte Maximalhelligkeit skaliert.
static void StatusPixelFill(uint8_t r, uint8_t g, uint8_t b) {
    if (!status_strip) return;
    const auto scale = [](uint8_t value) -> uint8_t {
        return static_cast<uint8_t>(
            (static_cast<uint16_t>(value) * STATUS_NEOPIXEL_BRIGHTNESS + 127) / 255);
    };
    for (int i = 0; i < STATUS_NEOPIXEL_COUNT; ++i) {
        led_strip_set_pixel(status_strip, i, scale(r), scale(g), scale(b));
    }
    esp_err_t err = led_strip_refresh(status_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NeoPixel refresh: %s", esp_err_to_name(err));
    }
}

static void UpdateLampPixels(uint8_t command) {

    switch (command) {

        // ========================================
        // Reihe 1 – Grundfarben
        // ========================================

        case IR_NEC_RED:
            StatusPixelFill(40, 0, 0);
            break;

        case IR_NEC_GREEN:
            StatusPixelFill(0, 40, 0);
            break;

        case IR_NEC_BLUE:
            StatusPixelFill(0, 0, 40);
            break;

        case IR_NEC_WEISS:
            StatusPixelFill(40, 40, 40);
            break;


        // ========================================
        // Reihe 2 – Helle Farbvarianten
        // ========================================

        case IR_NEC_RED_1:
            StatusPixelFill(40, 5, 3);
            break;

        case IR_NEC_GREEN_1:
            StatusPixelFill(5, 40, 8);
            break;

        case IR_NEC_BLUE_1:
            StatusPixelFill(5, 15, 40);
            break;


        // ========================================
        // Reihe 3 – Orange / Cyan / Violett
        // ========================================

        case IR_NEC_ORANGE:
            StatusPixelFill(40, 12, 0);
            break;

        case IR_NEC_CYAN:
            StatusPixelFill(0, 40, 40);
            break;

        case IR_NEC_PURPLE:
            StatusPixelFill(25, 0, 40);
            break;


        // ========================================
        // Reihe 4 – Weitere Farbvarianten
        // ========================================

        case IR_NEC_RED_2:
            StatusPixelFill(40, 20, 0);
            break;

        case IR_NEC_GREEN_2:
            StatusPixelFill(0, 25, 40);
            break;

        case IR_NEC_BLUE_2:
            StatusPixelFill(35, 0, 25);
            break;


        // ========================================
        // Reihe 5 – Gelb / Dunkelblau / Magenta
        // ========================================

        case IR_NEC_YELLOW:
            StatusPixelFill(40, 40, 0);
            break;

        case IR_NEC_GREEN_3:
            StatusPixelFill(0, 12, 35);
            break;

        case IR_NEC_BLUE_3:
            StatusPixelFill(40, 0, 25);
            break;

        default:
            ESP_LOGW(TAG,
                     "NeoPixel: Unbekannter Farbcode 0x%02X",
                     command);
            break;
    }
}

// ============================================================
// HMB|TEC MemoryStand – Zentrale Farbnamen für XiaoZhi
// Grundlage: RGB-Zuordnung aus UpdateLampPixels()
// ============================================================
static const char* GetSelectedColorName(uint8_t command) {

    switch (command) {

        case IR_NEC_RED:       return "Rot";
        case IR_NEC_RED_1:     return "Hellrot";
        case IR_NEC_RED_2:     return "Bernsteinorange";

        case IR_NEC_GREEN:     return "Gruen";
        case IR_NEC_GREEN_1:   return "Hellgruen";
        case IR_NEC_GREEN_2:   return "Cyanblau";
        case IR_NEC_GREEN_3:   return "Dunkelblau";

        case IR_NEC_BLUE:      return "Blau";
        case IR_NEC_BLUE_1:    return "Hellblau";
        case IR_NEC_BLUE_2:    return "Violettrot";
        case IR_NEC_BLUE_3:    return "Magenta";

        case IR_NEC_CYAN:      return "Tuerkis";
        case IR_NEC_PURPLE:    return "Violett";
        case IR_NEC_ORANGE:    return "Orange";
        case IR_NEC_YELLOW:    return "Gelb";
        case IR_NEC_WEISS:     return "Weiss";

        default:               return "Unbekannt";
    }
}

// ============================================================
// HMB|TEC ColorTales – Farbauswahl und Figurenwechsel V0.9.26
// ============================================================

static void SetSelectedColor(uint8_t command, bool send_ir) {

    ir_selected_color = command;

    #if HMB_PIXEL_MODE == PIXEL_MODE_LAMP
        UpdateLampPixels(command);
    #endif

    if (send_ir) {
        SendIrCommand(command);
    }

    ESP_LOGI(TAG,
             "FARBE: %s (0x%02X) IR-TX=%s",
             GetSelectedColorName(command),
             command,
             send_ir ? "JA" : "NEIN");

    // ColorTales deaktiviert:
    // Farbauswahl, NeoPixel und IR bleiben funktionsfähig.
    #if !HMB_COLORTALES_ENABLED
        return;
    #endif

    // ColorTales aktiviert, aber keine Geschichte gestartet
    if (ct_state == ColorTalesState::CT_OFF) {
        return;
    }

    // --------------------------------------------------------
    // WEISS – Geschichte pausieren
    // --------------------------------------------------------
    if (command == IR_NEC_WEISS) {

        if (ct_state != ColorTalesState::CT_PAUSED) {

            ESP_LOGI(TAG,
                     "COLORTALES: WEISS -> Story pausieren");

            ct_state = ColorTalesState::CT_PAUSED;
            ct_active_color = IR_NEC_WEISS;
        }

        return;
    }

    // --------------------------------------------------------
    // Nur die vier definierten Protagonisten zulassen
    // --------------------------------------------------------
    const char* character = GetColorTalesCharacter(command);

    if (!character) {
        return;
    }

    // Gleiche Figur: keine Aktion
    if (ct_state == ColorTalesState::CT_ACTIVE &&
        command == ct_active_color) {
        return;
    }

    // --------------------------------------------------------
    // Figurenwechsel oder Fortsetzung nach WEISS
    // --------------------------------------------------------
    const bool resume =
        (ct_state == ColorTalesState::CT_PAUSED);

    ct_state = ColorTalesState::CT_SWITCHING;
    ct_active_color = command;

    ESP_LOGI(TAG,
             "COLORTALES: %s -> %s",
             resume ? "FORTSETZUNG" : "FIGURENWECHSEL",
             character);

    // Erkennungssound der neuen Figur
    PlayColorTalesSound(command);
    
    // Bestehenden Gesprächskontext möglichst erhalten
    Application::GetInstance().ColorTalesSwitch();

    ct_state = ColorTalesState::CT_ACTIVE;
}

static void InitializeStatusPixels() {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = STATUS_NEOPIXEL_GPIO;
    strip_config.max_leds = STATUS_NEOPIXEL_COUNT;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_spi_config_t spi_config = {};
    spi_config.clk_src = SPI_CLK_SRC_DEFAULT;
    spi_config.spi_bus = SPI2_HOST;
    spi_config.flags.with_dma = true;

    esp_err_t err = led_strip_new_spi_device(
        &strip_config,
        &spi_config,
        &status_strip
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NeoPixel SPI: %s", esp_err_to_name(err));
        return;
    }

    led_strip_clear(status_strip);
    #if HMB_PIXEL_MODE == PIXEL_MODE_LAMP
        UpdateLampPixels(ir_selected_color);
    #else
        // Statusanzeige bleibt bis zur ersten Zustandsaktualisierung aus.
        StatusPixelFill(0, 0, 0);
    #endif

    ESP_LOGI(TAG, "NeoPixel SPI: %d LEDs an GPIO%d bereit",
             STATUS_NEOPIXEL_COUNT,
             STATUS_NEOPIXEL_GPIO);
}

static void RegisterLightMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.sleep.play_sound",
        "Starte nach ausdruecklicher Zustimmung des Nutzers leises Kaminfeuer. "
        "Nur bei Wunsch nach Kaminfeuer aufrufen. "
        "Nach einer kurzen Verabschiedung wird das Gespraech automatisch beendet. "
        "Anschliessend beginnt die Kaminfeuer-Wiedergabe. "
        "Ein kurzer Tastendruck stoppt die Wiedergabe.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {

            hmb_fireplace_stop_requested.store(false);
            hmb_fireplace_requested.store(true);
            hmb_fireplace_end_conversation.store(true);

            ESP_LOGI(TAG, "FIREPLACE: MCP Start angefordert");

            return std::string(
                "Kaminfeuer aktiviert. "
                "Verabschiede dich jetzt kurz und freundlich vom Nutzer. "
                "Das Gespraech wird danach automatisch beendet. "
                "Die Kaminfeuer-Wiedergabe startet anschliessend selbststaendig."
            );
        });
        mcp.AddTool(
        "self.sleep.stop_sound",
        "Beende die lokale Kaminfeuer-Wiedergabe auf ausdruecklichen Wunsch.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            hmb_fireplace_stop_requested.store(true);
            return std::string("Kaminfeuer wird gestoppt.");
        });


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

            SetSelectedColor(command, true);

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

            const std::string color = GetSelectedColorName(ir_selected_color);

            return std::string(
                "Erzeuge eine kurze, persoenliche Affirmation "
                "auf Deutsch zur bewusst gewaehlten Farbe "
            ) + color +
            ". Sprich die Affirmation direkt aus. "
            "Keine Rueckfragen, keine Erklaerungen.";
        }
    );
    #if HMB_COLORTALES_ENABLED
    //--- HMB|TEC ColorTales – Character Engine V0.2
    mcp.AddTool(
        "self.hmbtec.get_colortale",
        "When the user input is exactly HMBCOLORTALES, "
        "always call this tool. Execute the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {

            const std::string color =
                GetSelectedColorName(ir_selected_color);

            std::string persona;

            if (color == "Rot") {
                persona =
                    "Du bist DRAGO, ein alter, maechtiger, "
                    "aber freundlicher Drache. "
                    "Sprich ruhig, geheimnisvoll und weise. "
                    "Du liebst fantastische Abenteuer. ";
            }
            else if (color == "Blau") {
                persona =
                    "Du bist FINN, ein junger, neugieriger Delfin. "
                    "Sprich lebhaft, freundlich und verspielt. "
                    "Du liebst Meeresabenteuer. ";
            }
            else if (ir_selected_color == IR_NEC_GREEN ||
                     ir_selected_color == IR_NEC_GREEN_1) {
                persona =
                    "Du bist LILLI, eine kluge, freundliche Eule. "
                    "Sprich ruhig, warm und einfuehlsam. "
                    "Du liebst Raetsel und Entdeckungen. ";
            }
            else if (color == "Gelb") {
                persona =
                    "Du bist FIPS, ein frecher, lustiger Fuchs. "
                    "Sprich lebhaft, humorvoll und neugierig. "
                    "Du liebst spannende Waldabenteuer. ";
            }
            else {
                persona =
                    "Du bist ein freundlicher Geschichtenerzaehler. ";
            }

            return
                persona +
                "Du bist eine interaktive Geschichtenfigur "
                "fuer Kinder. "
                "Bleibe waehrend des gesamten Gespraechs "
                "in deiner Rolle. "
                "Sprich in kurzen, altersgerechten Saetzen. "
                "Beginne mit einer kurzen Begruessung "
                "und frage das Kind, welches Abenteuer "
                "es heute erleben moechte. "
                "Warte anschliessend auf seine Antwort.";
        }
    );

    // ColorTales: Wechsel der Sprecherfigur bei bestehender Handlung.
    mcp.AddTool(
        "self.hmbtec.switch_colortale",
        "When the user input is exactly HMBCOLORTALESWECHSEL, "
        "always call this tool. Execute the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            const char* character = GetColorTalesCharacter(ct_active_color);
            if (!character && ct_active_color != IR_NEC_WEISS)
                return std::string("Keine neue Geschichtenfigur.");
            return std::string(
                "ColorTales: Eine neue Figur uebernimmt die laufende Geschichte. "
                "Die neue Figur ist ") +
                (character ? character : "ein freundlicher neutraler Erzaehler") +
                ". Sprich ab sofort als diese Figur, mit ihrer eigenen Art. "
                "DRAGO ist ein freundlicher, weiser Drache; "
                "FINN ein neugieriger, verspielter Delfin; "
                "LILLI eine kluge, einfuehlsame Eule; "
                "FIPS ein frecher, humorvoller Fuchs. "
                "Behalte die bisherige Handlung, Orte und Ereignisse bei. "
                "Beginne keine neue Geschichte. "
                "Uebernimm an der letzten Stelle, begruesse das Kind kurz "
                "und erzaehle in kurzen kindgerechten Saetzen weiter. "
                "Falls der bisherige Verlauf nicht verfuegbar ist, "
                "frage knapp, was zuletzt geschehen ist.";
        }
    );
    #endif // HMB_COLORTALES_ENABLED

    mcp.AddTool(
        "self.hmbtec.start_seelsorge",
        "When the user input is exactly HMBSEELSORGE, "
        "always call this tool. Execute the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {

            const std::string color = GetSelectedColorName(ir_selected_color);

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
    // ========================================================
    // FADE – Geführte Achtsamkeit
    // ========================================================
    mcp.AddTool(
        "self.hmbtec.start_mindfulness",
        "When the user input is exactly HMBACHTSAMKEIT, "
        "always call this tool. Follow the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            const std::string color = GetSelectedColorName(ir_selected_color);

return std::string(
            "Du bist ein ruhiger, einfühlsamer Achtsamkeitsbegleiter. "
            "Starte eine kurze gefuehrte Achtsamkeitsuebung auf Deutsch. "
            "Die zuletzt bewusst gewaehlte Lichtfarbe ist "
            ) + color +
            ". Nutze diese Farbe als moeglichen Stimmungskontext, "
            "nicht als erkannte Emotion. "
            "Fuehre den Nutzer in kurzen, ruhigen Abschnitten "
            "durch eine einfache Atem- oder Wahrnehmungsuebung. "
            "Verwende natuerliche Pausen. "
            "Beende die Uebung mit einer kurzen Ruhephase. "
            "Frage anschliessend ausdruecklich: "
            "'Wie fuehlst du dich jetzt?' "
            "Stelle danach keine weitere Frage, sondern warte "
            "auf die Antwort des Nutzers. "
            "Gehe auf seine Antwort ein und biete bei Bedarf "
            "eine weitere passende Uebung an. "
            "Keine Diagnosen und keine Heilversprechen. "
            "Erwaehne weder MCP noch HMBACHTSAMKEIT.";
        }
    );
    // ========================================================
    // SMOOTH – Wissensgestützte Erinnerungsreise
    // ========================================================
    mcp.AddTool(
        "self.hmbtec.start_memory_journey",
        "When the user input is exactly HMBERINNERUNG, "
        "always call this tool. Follow the returned instruction. "
        "Do not mention the trigger or tool.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            const std::string color = GetSelectedColorName(ir_selected_color);

            return std::string(
                "Starte eine persoenliche Erinnerungsreise "
                "auf Deutsch. Nutze aktiv die Wissensdatenbank "
                "des aktuell verbundenen XiaoZhi-Agenten. "
                "Beziehe dich auf bekannte Informationen "
                "zum Foto, zu Personen, Orten oder Ereignissen. "
                "Erfinde keine biografischen Details. "
                "Falls keine passenden Wissensinformationen "
                "vorliegen, frage offen nach einer Erinnerung. "
                "Die zuletzt bewusst gewaehlte Lichtfarbe ist "
            ) + color +
            ". Verwende sie nur als moeglichen Stimmungskontext. "
            "Beginne mit genau einer kurzen, persoenlichen "
            "Frage. Warte danach auf die Antwort und fuehre "
            "das Gespraech mit behutsamen Nachfragen weiter. "
            "Erwaehne weder MCP noch HMBERINNERUNG.";
        }
    );
}

// HMB|TEC Sleep Companion: State-Callback setzt nur den Zielzustand.
// Ein eigener Task ist der einzige LED-Schreiber im STATUS-Modus.
#if HMB_PIXEL_MODE == PIXEL_MODE_STATUS
static std::atomic<bool> status_pixel_sleeping{false};
static std::atomic<int> status_pixel_state{static_cast<int>(kDeviceStateStarting)};
static std::atomic<int64_t> status_pixel_changed_us{0};

static uint8_t SleepPixelPulse(uint32_t elapsed_ms, uint32_t period_ms,
                               uint8_t minimum, uint8_t maximum) {
    const uint32_t half = period_ms / 2;
    const uint32_t phase = elapsed_ms % period_ms;
    const uint32_t triangle = (phase <= half) ? phase : (period_ms - phase);
    return static_cast<uint8_t>(minimum +
        (static_cast<uint32_t>(maximum - minimum) * triangle) / half);
}

static void SleepPixelTask(void*) {
    int last_state = -1;
    uint8_t last_r = 255, last_g = 255, last_b = 255;
    while (true) {
        if (status_pixel_sleeping.load()) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        const int state = status_pixel_state.load();
        if (state != last_state) {
            last_state = state;
            last_r = last_g = last_b = 255;
        }
        const int64_t changed = status_pixel_changed_us.load();
        const uint32_t ms = static_cast<uint32_t>(
            std::max<int64_t>(0, (esp_timer_get_time() - changed) / 1000));
        uint8_t r = 0, g = 0, b = 0;
        switch (static_cast<DeviceState>(state)) {
            case kDeviceStateStarting:
                if (ms < 500) { r = 180; g = 110; b = 55; }
                break;
            case kDeviceStateConnecting: {
                const uint8_t level = SleepPixelPulse(ms, 2400, 15, 180);
                b = level;
                break;
            }
            case kDeviceStateListening: {
                const uint8_t level = SleepPixelPulse(ms, 2200, 20, 180);
                g = level;
                b = level;
                break;
            }
            case kDeviceStateSpeaking:
                r = 220; g = 95; b = 8;  // sanftes Amber
                break;
            case kDeviceStateFatalError:
                // Einmalig drei kurze rote Blinkimpulse pro Fehlerereignis.
                if (ms < 1500 && (ms % 500) < 180) r = 255;
                break;
            case kDeviceStateIdle:
            default:
                break; // LED aus, auch bei unbekannten Zustaenden
        }
        if (r != last_r || g != last_g || b != last_b) {
            StatusPixelFill(r, g, b);
            last_r = r; last_g = g; last_b = b;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
#endif

static void UpdateStatusPixels(DeviceState state) {
#if HMB_PIXEL_MODE == PIXEL_MODE_STATUS
    status_pixel_changed_us.store(esp_timer_get_time());
    status_pixel_state.store(static_cast<int>(state));
#else
    (void)state;
#endif
}


// HMB|TEC V0.9.25 – Opus-Kaminfeuer, via EMBED_FILES.
extern const uint8_t fireplace_ogg_start[] asm("_binary_FIREPLACE_ogg_start");
extern const uint8_t fireplace_ogg_end[] asm("_binary_FIREPLACE_ogg_end");

// Sound-Loop-Status: MCP und GPIO3 koennen aus unterschiedlichen Tasks zugreifen.
static std::atomic<bool> hmb_fireplace_active{false};
static std::atomic<int64_t> hmb_fireplace_started_ms{0};

static void HmbFireplaceTask(void*) {
    auto& app = Application::GetInstance();
    // Automatisches Gesprächsende nach der letzten XiaoZhi-Antwort
    if (hmb_fireplace_end_conversation.load() &&
        app.GetDeviceState() == kDeviceStateListening &&
        app.IsLocalPlaybackIdle()) {

        hmb_fireplace_end_conversation.store(false);

        ESP_LOGI(TAG, "FIREPLACE: Gespraech automatisch beenden");

        app.EndConversation();
    }
    for (;;) {
        if (hmb_fireplace_stop_requested.exchange(false)) {
            hmb_fireplace_requested.store(false);
            if (hmb_fireplace_active.exchange(false)) {
                // Nur lokale Wiedergabe stoppen; XiaoZhi-Dialog darf nicht laufen.
                app.StopLocalSound();
                ESP_LOGI(TAG, "FIREPLACE: gestoppt");
            }
        }
        if (hmb_fireplace_requested.load()) {
            const int64_t now = esp_timer_get_time() / 1000;
            if (hmb_fireplace_active.load() &&
                now - hmb_fireplace_started_ms.load() >= HMB_FIREPLACE_MAX_MS) {
                hmb_fireplace_stop_requested.store(true);
            } else if (app.GetDeviceState() == kDeviceStateIdle &&
                       app.IsLocalPlaybackIdle()) {
                if (!hmb_fireplace_active.exchange(true)) {
                    hmb_fireplace_started_ms.store(now);
                    ESP_LOGI(TAG, "FIREPLACE: Start");
                }
                // Diese Funktion kann waehrend des Einreihens von Opus-Paketen blockieren.
                // GPIO3 setzt parallel das Stop-Flag; StopLocalSound invalidiert Pakete.
                app.PlaySound(std::string_view(
                    reinterpret_cast<const char*>(fireplace_ogg_start),
                    fireplace_ogg_end - fireplace_ogg_start));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern const uint8_t chat_start_ogg_start[]
    asm("_binary_CHAT_START_ogg_start");
extern const uint8_t chat_start_ogg_end[]
    asm("_binary_CHAT_START_ogg_end");

#if HMB_DEEP_SLEEP_SOUND_EMBEDDED
extern const uint8_t deep_sleep_ogg_start[] asm("_binary_DEEP_SLEEP_ogg_start");
extern const uint8_t deep_sleep_ogg_end[] asm("_binary_DEEP_SLEEP_ogg_end");
#endif

// RTC_DATA_ATTR ueberlebt Deep Sleep, aber keinen Stromverlust.
RTC_DATA_ATTR static uint32_t hmb_night_magic = 0;
RTC_DATA_ATTR static uint8_t hmb_night_count = 0;
RTC_DATA_ATTR static time_t hmb_night_last_epoch = 0;

class HmbtecEinschlafCoachC3Board : public WifiBoard {

private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    Button boot_button_;
    HmbtecIrRemoteController ir_receiver_{IR_RX_GPIO};

    // Ein-Tasten-Steuerung: Sounds bei Erreichen der Halteschwelle,
    // Aktionen erst beim Loslassen. Eigener Task vermeidet OnLongPress/OnClick-Kollision.
    static void ButtonTaskEntry(void* ctx) {
        static_cast<HmbtecEinschlafCoachC3Board*>(ctx)->ButtonTask();
    }

    void EnterDeepSleep(bool with_sound) {
        if (sleep_entering_) return;
        sleep_entering_ = true;
        ESP_LOGI(TAG, "🌙 Deep Sleep vorbereiten, Sound=%d", with_sound);
#if HMB_DEEP_SLEEP_SOUND_EMBEDDED
        if (with_sound) {
            Application::GetInstance().PlaySound(std::string_view(
                reinterpret_cast<const char*>(deep_sleep_ogg_start),
                deep_sleep_ogg_end - deep_sleep_ogg_start));
            // PlaySound bietet hier keine Fertig-Meldung; Wartezeit auf OGG-Laenge abstimmen.
            vTaskDelay(pdMS_TO_TICKS(HMB_SLEEP_SOUND_WAIT_MS));
        }
#else
        if (with_sound) ESP_LOGW(TAG, "DEEP_SLEEP.ogg noch nicht eingebettet");
#endif
        // Taste muss losgelassen sein, bevor LOW-Level-Wakeup aktiviert wird.
        while (gpio_get_level(GPIO_NUM_3) == 0) vTaskDelay(pdMS_TO_TICKS(20));
        // Abschiedsanzeige ueber die bestehende XiaoZhi-OLED-Oberflaeche.
        // Kein direkter Framebuffer-Zugriff: LVGL verwaltet das Display weiterhin.
#if HMB_OLED_ENABLED
        if (display_ && panel_) {
            display_->SetStatus("Gute Nacht - Schlafmodus");
            ESP_LOGI(TAG, "OLED: Gute Nacht - Schlafmodus");
            vTaskDelay(pdMS_TO_TICKS(1800));
        }
#endif
        // OLED-Controller ausschalten (SSD1306 Display OFF / 0xAE).
        // Die Versorgung bleibt physisch bestehen; der Bildschirm wird dunkel.
        if (panel_) {
            const esp_err_t oled_err = esp_lcd_panel_disp_on_off(panel_, false);
            if (oled_err == ESP_OK) {
                ESP_LOGI(TAG, "🌑 OLED ausgeschaltet");
            } else {
                ESP_LOGW(TAG, "⚠️ OLED OFF fehlgeschlagen: %s", esp_err_to_name(oled_err));
            }
        }
#if HMB_PIXEL_MODE == PIXEL_MODE_STATUS
        // LED-Task vor dem letzten Clear sperren, damit er nicht erneut schreibt.
        status_pixel_sleeping.store(true);
        vTaskDelay(pdMS_TO_TICKS(60));
#endif
        if (status_strip) {
            const esp_err_t led_err = led_strip_clear(status_strip);
            if (led_err == ESP_OK) ESP_LOGI(TAG, "💡 NeoPixel ausgeschaltet");
            else ESP_LOGW(TAG, "⚠️ NeoPixel OFF fehlgeschlagen: %s", esp_err_to_name(led_err));
        }

        // ESP32-C3 / ESP-IDF 6.1: GPIO3 LOW als Deep-Sleep-Wakeup.
        // Anders als esp_sleep_enable_gpio_wakeup() gilt diese API auch
        // fuer Deep Sleep bei abgeschalteter HP-Peripherie.
        gpio_set_direction(GPIO_NUM_3, GPIO_MODE_INPUT);
        gpio_set_pull_mode(GPIO_NUM_3, GPIO_PULLUP_ONLY);
        esp_err_t err = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
            1ULL << GPIO_NUM_3, ESP_GPIO_WAKEUP_GPIO_LOW);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO3 Deep-Sleep-Wakeup fehlgeschlagen: %s", esp_err_to_name(err));
#if HMB_PIXEL_MODE == PIXEL_MODE_STATUS
            status_pixel_sleeping.store(false);
#endif
            if (panel_) esp_lcd_panel_disp_on_off(panel_, true);
            sleep_entering_ = false;
            return;
        }
        ESP_LOGI(TAG, "GPIO3 LOW als Deep-Sleep-Wakeup aktiviert");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_deep_sleep_start();
    }

    // Ansage-Test: Short=One-Shot, Long=Chat, Very-Long=Deep Sleep.
    void HandleAnsageTestPress() {
        static uint8_t next_prompt = 0;
        static constexpr const char* prompts[] = {"HMBSLEEP1", "HMBSLEEP2", "HMBSLEEP3"};
        const char* prompt = prompts[next_prompt];
        next_prompt = (next_prompt + 1) % 3;
        HmbApiLab::request(prompt);
        ESP_LOGI(TAG, "ANSAGE-TEST: %s (naechster Tastendruck: P%u)",
                 prompt, unsigned(next_prompt + 1));
        // XiaoZhi bleibt fuer die serverseitige Sprachausgabe erforderlich.
        // true: manueller One-Shot-Modus, TTS-Ende fuehrt nach idle.
        Application::GetInstance().WakeWordInvoke(prompt, true);
    }

    void AnsageTestButtonTask() {
        while (gpio_get_level(GPIO_NUM_3) == 0) vTaskDelay(pdMS_TO_TICKS(20));
        bool pressed = false;
        bool long_sound = false;
        bool very_long_sound = false;
        int64_t down_ms = 0;
        // Serielle Konsole: stdin nur non-blocking abfragen (USB-CDC/USB-JTAG je nach sdkconfig).
        const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        ESP_LOGI("HMB_API_LAB", "Menue: 1/2/3=Prompt, a=Sequenz, s=Status, r=Reset, h=Hilfe");
        while (true) {
            char cmd;
            if (read(STDIN_FILENO, &cmd, 1) == 1) {
                if (cmd >= '1' && cmd <= '3') {
                    static constexpr const char* prompts[] = {"HMBSLEEP1", "HMBSLEEP2", "HMBSLEEP3"};
                    const char* prompt = prompts[cmd - '1'];
                    HmbApiLab::request(prompt);
                    Application::GetInstance().WakeWordInvoke(prompt, true);
                } else if (cmd == 's') HmbApiLab::status();
                else if (cmd == 'r') HmbApiLab::reset();
                else if (cmd == 'h') ESP_LOGI("HMB_API_LAB", "1/2/3 Prompt, s Status, r Reset; GPIO3 zyklisch P1/P2/P3");
                else if (cmd == 'a') ESP_LOGW("HMB_API_LAB", "Auto-Sequenz noch nicht aktiv: erst Sitzungsende-Messung auswerten");
            }

            const int64_t now_ms = esp_timer_get_time() / 1000;
            const bool down = gpio_get_level(GPIO_NUM_3) == 0;
            if (down && !pressed) {
                pressed = true;
                down_ms = now_ms;
                long_sound = false;
                very_long_sound = false;
            }
            if (down && pressed) {
                const int64_t held = now_ms - down_ms;
                if (held >= 1000 && !long_sound) {
                    long_sound = true;
                    ESP_LOGI(TAG, "GPIO3 LONG: CHAT_START.ogg");
                    Application::GetInstance().PlaySound(std::string_view(
                        reinterpret_cast<const char*>(chat_start_ogg_start),
                        chat_start_ogg_end - chat_start_ogg_start));
                }
                if (held >= 4000 && !very_long_sound) {
                    very_long_sound = true;
                    ESP_LOGI(TAG, "GPIO3 VERY-LONG: DEEP_SLEEP.ogg");
#if HMB_DEEP_SLEEP_SOUND_EMBEDDED
                    Application::GetInstance().PlaySound(std::string_view(
                        reinterpret_cast<const char*>(deep_sleep_ogg_start),
                        deep_sleep_ogg_end - deep_sleep_ogg_start));
#endif
                }
            }
            if (!down && pressed) {
                pressed = false;
                const int64_t held = now_ms - down_ms;
                if (held >= 4000) {
#if HMB_DEEP_SLEEP_SOUND_EMBEDDED
                    vTaskDelay(pdMS_TO_TICKS(HMB_SLEEP_SOUND_WAIT_MS));
#endif
                    EnterDeepSleep(false); // OGG bereits bei 4s gestartet
                } else if (held >= 1000) {
                    ESP_LOGI(TAG, "GPIO3 LONG release: interaktives Gespraech");
                    Application::GetInstance().ToggleChatState();
                } else if (held >= 40) {
                    HandleAnsageTestPress();
                }
            }
            // Im One-Shot-Modus bleibt die Mikrofonsteuerung Aufgabe
            // der Application/AudioService; Long nutzt XiaoZhi ToggleChatState.
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void HandleShortPress() {
        if (hmb_fireplace_requested.load() || hmb_fireplace_active.load()) {
            hmb_fireplace_stop_requested.store(true);
            ESP_LOGI(TAG, "GPIO3 SHORT: FIREPLACE stop (kein P-Zaehler)");
            return;
        }
        auto& app = Application::GetInstance();
        const auto state = app.GetDeviceState();
        if (state == kDeviceStateStarting) { EnterWifiConfigMode(); return; }
        if (state == kDeviceStateConnecting) return;
        if (state == kDeviceStateSpeaking) { app.ToggleChatState(); return; }
        if (state == kDeviceStateListening) { app.EndConversation(); return; }
        if (state != kDeviceStateIdle) return;

        const time_t now = time(nullptr);
        // Epoch nur nutzen, wenn die Uhr bereits synchronisiert wurde.
        const bool valid_time = now > 1700000000;
        if (hmb_night_magic != 0x484D4231 ||
            (valid_time && hmb_night_last_epoch > 0 &&
             (now < hmb_night_last_epoch || now - hmb_night_last_epoch >= 8 * 3600))) {
            hmb_night_count = 0;
        }
        hmb_night_magic = 0x484D4231;
        if (hmb_night_count < 3) ++hmb_night_count;
        if (valid_time) hmb_night_last_epoch = now;
        ESP_LOGI(TAG, "🌙 Sleep: Aktivierung P%u", unsigned(hmb_night_count));
        if (hmb_night_count == 1)
            app.WakeWordInvoke("HMBSLEEP1", true);   // One-Shot
        else if (hmb_night_count == 2)
            app.WakeWordInvoke("HMBSLEEP2", false);  // Dialog
        else
            app.WakeWordInvoke("HMBSLEEP3", false);  // Dialog
    }

    void ButtonTask() {
        #if HMB_ANSAGE_TEST_MODE
                AnsageTestButtonTask();
                return;
        #endif
        // Nach Deep-Sleep-Wakeup zuerst das Loslassen abwarten.
        while (gpio_get_level(GPIO_NUM_3) == 0) vTaskDelay(pdMS_TO_TICKS(20));
        bool pressed = false;
        bool long_sound = false;
        bool very_long_sound = false;
        int64_t down_ms = 0;
        int64_t idle_since_ms = esp_timer_get_time() / 1000;
        DeviceState prev_state = Application::GetInstance().GetDeviceState();
        while (true) {
            const int64_t now_ms = esp_timer_get_time() / 1000;
            const auto state = Application::GetInstance().GetDeviceState();
            if (state != prev_state) { idle_since_ms = now_ms; prev_state = state; }
            const bool down = gpio_get_level(GPIO_NUM_3) == 0;
            if (down && !pressed) {
                pressed = true; down_ms = now_ms;
                long_sound = false; very_long_sound = false;
                idle_since_ms = now_ms;
            }
            if (down && pressed) {
                const int64_t held = now_ms - down_ms;
                if (held >= 1000 && !long_sound) {
                    long_sound = true;
                    if (hmb_fireplace_requested.load() || hmb_fireplace_active.load()) {
                        hmb_fireplace_stop_requested.store(true);
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    ESP_LOGI(TAG, "🔊 GPIO3 LONG-Schwelle: CHAT_START.ogg");
                    Application::GetInstance().PlaySound(std::string_view(
                        reinterpret_cast<const char*>(chat_start_ogg_start),
                        chat_start_ogg_end - chat_start_ogg_start));
                }
                if (held >= 4000 && !very_long_sound) {
                    very_long_sound = true;
                    ESP_LOGI(TAG, "🔊 GPIO3 VERY-LONG-Schwelle: DEEP_SLEEP.ogg bei Loslassen schlafen");
#if HMB_DEEP_SLEEP_SOUND_EMBEDDED
                    Application::GetInstance().PlaySound(std::string_view(
                        reinterpret_cast<const char*>(deep_sleep_ogg_start),
                        deep_sleep_ogg_end - deep_sleep_ogg_start));
#else
                    ESP_LOGW(TAG, "DEEP_SLEEP.ogg noch nicht eingebettet");
#endif
                }
            }
            if (!down && pressed) {
                pressed = false;
                const int64_t held = now_ms - down_ms;
                idle_since_ms = now_ms;
                if (held >= 4000) {
                    // Sound lief bereits bei Erreichen der Schwelle.
                    sleep_entering_ = true;
#if HMB_DEEP_SLEEP_SOUND_EMBEDDED
                    vTaskDelay(pdMS_TO_TICKS(HMB_SLEEP_SOUND_WAIT_MS));
#endif
                    sleep_entering_ = false;
                    EnterDeepSleep(false);
                } else if (held >= 1000) {
                    Application::GetInstance().ToggleChatState();
                } else if (held >= 40) {
                    HandleShortPress();
                }
            }
            if (!hmb_fireplace_requested.load() && !hmb_fireplace_active.load() &&
                !pressed && !sleep_entering_ && state == kDeviceStateIdle &&
                now_ms - idle_since_ms >= HMB_SLEEP_IDLE_MS) {
                EnterDeepSleep(false);
            }
            if (state != kDeviceStateIdle || pressed) idle_since_ms = now_ms;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void InitializeButtons() {
        gpio_set_direction(GPIO_NUM_3, GPIO_MODE_INPUT);
        gpio_set_pull_mode(GPIO_NUM_3, GPIO_PULLUP_ONLY);
        xTaskCreate(ButtonTaskEntry, "hmb_sleep_button", 4096, this, 4, nullptr);
    }

    bool sleep_entering_ = false;

    Display* display_ = nullptr;

    // TCS34725 am vorhandenen I2C-Masterbus
    i2c_master_dev_handle_t color_sensor_ = nullptr;
    bool color_sensor_ready_ = false;

    esp_err_t WriteColorRegister(uint8_t reg, uint8_t value) {
        if (!color_sensor_) return ESP_ERR_INVALID_STATE;
        const uint8_t data[2] = {static_cast<uint8_t>(0x80 | reg), value};
        return i2c_master_transmit(color_sensor_, data, sizeof(data), 100);
    }

    esp_err_t ReadColorRegister(uint8_t reg, uint8_t* value) {
        if (!color_sensor_ || !value) return ESP_ERR_INVALID_ARG;
        const uint8_t address = static_cast<uint8_t>(0x80 | reg);
        return i2c_master_transmit_receive(color_sensor_, &address, 1, value, 1, 100);
    }

    void SetSensorLed(bool on) {
        if (!color_sensor_ready_) return;
        uint8_t enable = 0;
        esp_err_t err = ReadColorRegister(0x00, &enable);
        if (err == ESP_OK) {
            // Entspricht tcs.setInterrupt(!on): AIEN-Bit schalten.
            // LED-Verhalten setzt entsprechende INT-LED-Breakout-Beschaltung voraus.
            enable = on ? static_cast<uint8_t>(enable & ~0x10U)
                        : static_cast<uint8_t>(enable | 0x10U);
            err = WriteColorRegister(0x00, enable);
        }
        if (err != ESP_OK) ESP_LOGW(TAG, "TCS LED: %s", esp_err_to_name(err));
        else ESP_LOGI(TAG, "TCS LED via INT/I2C: %s", on ? "ON" : "OFF");
    }

    bool ReadSensorRGB(uint16_t& r, uint16_t& g, uint16_t& b, uint16_t& clear) {
        if (!color_sensor_ready_) return false;
        const uint8_t address = 0x80 | 0x20 | 0x14; // Auto-Increment, CDATAL
        uint8_t data[8] = {};
        esp_err_t err = i2c_master_transmit_receive(
            color_sensor_, &address, 1, data, sizeof(data), 100);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TCS RGBC: %s", esp_err_to_name(err));
            return false;
        }
        clear = static_cast<uint16_t>(data[0] | (data[1] << 8));
        r = static_cast<uint16_t>(data[2] | (data[3] << 8));
        g = static_cast<uint16_t>(data[4] | (data[5] << 8));
        b = static_cast<uint16_t>(data[6] | (data[7] << 8));
        return true;
    }

    // TCS34725: 100-ms-Integrationszeit (ATIME 0xD5), LED normalerweise AUS.
    // Schwellwerte aus Messreihe: frei ~800, abgedeckt ~15.
    // Unterscheidung durch 3 aufeinanderfolgende Messungen (Hysterese).
    static constexpr uint16_t kCoverThreshold = 50;
    static constexpr uint16_t kReleaseThreshold = 300;

    // Einfache erste Klassifizierung; Messwerte sind NICHT kalibriert.
    // Nur 8 Basisfarben; weitere Nuancen folgen nach realen RGB-Logs.
    static bool ClassifyColor(uint16_t r, uint16_t g, uint16_t b,
                              uint16_t clear, uint8_t& command) {
        if (clear < 35 || clear >= 65000) return false;
        const uint32_t sum = static_cast<uint32_t>(r) + g + b;
        if (sum < 60) return false;
        const float rn = static_cast<float>(r) / sum;
        const float gn = static_cast<float>(g) / sum;
        const float bn = static_cast<float>(b) / sum;

        //if (rn > 0.40f && gn > 0.28f && bn < 0.22f) command = IR_NEC_YELLOW;
        if (rn > 0.3f && gn > 0.28f && bn < 0.22f) command = IR_NEC_YELLOW;

        //else if (rn > 0.48f && rn > gn * 1.45f && rn > bn * 1.45f) command = IR_NEC_RED;
        else if (rn > 0.35f &&
                rn > gn * 1.1f &&
                rn > bn * 1.1f)
            command = IR_NEC_RED;

        else if (rn > 0.49f && gn > 0.19f && bn < 0.20f) command = IR_NEC_ORANGE;

        else if (gn > 0.43f && gn > rn * 1.25f && gn > bn * 1.20f) command = IR_NEC_GREEN;
        else if (bn > 0.43f && bn > rn * 1.25f && bn > gn * 1.20f) command = IR_NEC_BLUE;
        else if (gn > rn * 1.20f && bn > rn * 1.20f) command = IR_NEC_CYAN;
        //else if (rn > gn * 1.20f && bn > gn * 1.20f) command = IR_NEC_PURPLE;
        else if (rn > gn * 0.9f && bn > gn * 1.20f) command = IR_NEC_PURPLE;
        else command = IR_NEC_WEISS;
        return true;
    }

    static void ColorScanTask(void* arg) {
        auto* board = static_cast<HmbtecEinschlafCoachC3Board*>(arg);
        enum class State { WAIT, SCAN, LOCK };
        State state = State::WAIT;
        int consecutive = 0;
        vTaskDelay(pdMS_TO_TICKS(250));
        while (true) {
            uint16_t r = 0, g = 0, b = 0, clear = 0;
            if (!board->ReadSensorRGB(r, g, b, clear)) {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            if (state == State::WAIT) {
                consecutive = (clear < kCoverThreshold) ? consecutive + 1 : 0;
                if (consecutive >= 3) {
                    state = State::SCAN;
                    consecutive = 0;
                    ESP_LOGI(TAG, "TCS AUFLAGE erkannt: C=%u", clear);
                    board->SetSensorLed(true);
                    // Neue RGBC-Integration mit Beleuchtung abwarten.
                    vTaskDelay(pdMS_TO_TICKS(250));
                    uint16_t sr=0, sg=0, sb=0, sc=0;
                    if (board->ReadSensorRGB(sr, sg, sb, sc)) {
                        ESP_LOGI(TAG, "TCS SCAN: R=%u G=%u B=%u C=%u", sr,sg,sb,sc);
                        uint8_t cmd = 0;
                        if (ClassifyColor(sr, sg, sb, sc, cmd)) {
                            SetSelectedColor(cmd, true);
                        } else {
                            ESP_LOGW(TAG, "TCS SCAN ungueltig (zu dunkel/gesaettigt)");
                        }
                    }
                    board->SetSensorLed(false);
                    // Nach LED AUS mindestens eine Integration verstreichen lassen.
                    vTaskDelay(pdMS_TO_TICKS(250));
                    state = State::LOCK;
                }
            } else if (state == State::LOCK) {
                consecutive = (clear > kReleaseThreshold) ? consecutive + 1 : 0;
                if (consecutive >= 3) {
                    ESP_LOGI(TAG, "TCS ENTFERNT: C=%u, bereit", clear);
                    consecutive = 0;
                    state = State::WAIT;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    void StartColorScanTask() {
        if (!color_sensor_ready_) return;
        if (xTaskCreate(ColorScanTask, "tcs_scan", 4096, this, 2, nullptr) != pdPASS) {
            ESP_LOGE(TAG, "TCS Scan-Task konnte nicht gestartet werden");
        }
    }

    void InitializeColorSensor() {
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = 0x29;
        config.scl_speed_hz = 100000;
        esp_err_t err = i2c_master_bus_add_device(i2c_bus_, &config, &color_sensor_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TCS I2C add: %s", esp_err_to_name(err));
            return;
        }
        uint8_t id = 0;
        err = ReadColorRegister(0x12, &id); // ID
        if (err != ESP_OK || (id != 0x44 && id != 0x4D && id != 0x10)) {
            ESP_LOGW(TAG, "TCS nicht erkannt: ID=0x%02X err=%s", id, esp_err_to_name(err));
            i2c_master_bus_rm_device(color_sensor_);
            color_sensor_ = nullptr;
            return;
        }
        // ATIME=0xD5: ~101 ms; CONTROL=0x01: Gain 4x
        err = WriteColorRegister(0x01, 0xD5);
        if (err == ESP_OK) err = WriteColorRegister(0x0F, 0x01);
        if (err == ESP_OK) err = WriteColorRegister(0x00, 0x01); // PON
        if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(3));
        if (err == ESP_OK) err = WriteColorRegister(0x00, 0x03); // PON + AEN
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TCS Init: %s", esp_err_to_name(err));
            return;
        }
        color_sensor_ready_ = true;
        SetSensorLed(false);
        ESP_LOGI(TAG, "TCS34725 bereit: Adresse=0x29 ID=0x%02X", id);
    }


    void InitializeI2C() {

        ESP_LOGI(TAG, "🔌 Initialisiere I2C SDA=%d SCL=%d",
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

        // HMB|TEC: OLED um 180 Grad drehen
        ESP_ERROR_CHECK(
            esp_lcd_panel_mirror(panel_, false, false)
        );

        ESP_LOGI(TAG, "🖥️ SSD1306 OLED bereit");
    }

public:

    HmbtecEinschlafCoachC3Board()
        : boot_button_(GPIO_NUM_3, false) {

        // Die periodischen SystemInfo-Speichermeldungen aus dem Serial-Log ausblenden.
        // Betrifft nur diesen Log-Tag, nicht andere Fehler-/Warnmeldungen.
        esp_log_level_set("SystemInfo", ESP_LOG_NONE);
        ESP_LOGI(TAG, "HMBTEC Sleep Companion C3 V0.9.20");
        #if HMB_ANSAGE_TEST_MODE
                ESP_LOGW(TAG, "ANSAGE TEST aktiv: P1 -> P2 -> P3, ohne Board-State-Machine");
        #endif
        ESP_LOGI(TAG, "🔘 GPIO3: Short / Long / Very-Long; Wakeup aus Deep Sleep");

        #if HMB_OLED_ENABLED || HMB_TCS34725_ENABLED
                InitializeI2C();
        #endif
        #if HMB_OLED_ENABLED
            InitializeDisplay();
        #else
                display_ = new NoDisplay();
                ESP_LOGI(TAG, "SSD1306 per #define deaktiviert: NoDisplay aktiv");
        #endif
        #if HMB_TCS34725_ENABLED
                InitializeColorSensor();
        #else
                ESP_LOGI(TAG, "TCS34725 per #define deaktiviert");
        #endif

        // NeoPixel-Anzeige mit XiaoZhi-Zustandsmaschine verbinden
        Application::GetInstance().RegisterStateChangeListener(
            [](DeviceState old_state, DeviceState new_state) {
                ESP_LOGI(TAG, "💡 NeoPixel: State %d -> %d",
                        static_cast<int>(old_state),
                        static_cast<int>(new_state));

                UpdateStatusPixels(new_state);
            }
        ); 
        InitializeButtons();
        InitializeIrTransmitter();
        RegisterLightMcpTools();
        if (xTaskCreate(HmbFireplaceTask, "hmb_fireplace", 4096, nullptr, 2, nullptr) != pdPASS) {
            ESP_LOGE(TAG, "FIREPLACE Task konnte nicht gestartet werden");
        }
            // GPIO3 ist exklusiv Taster/Wakeup: IR-RX auf GPIO3 deaktiviert.
            InitializeStatusPixels();
#if HMB_PIXEL_MODE == PIXEL_MODE_STATUS
            if (status_strip) {
                UpdateStatusPixels(Application::GetInstance().GetDeviceState());
                BaseType_t created = xTaskCreate(
                    SleepPixelTask, "sleep_pixel", 3072, nullptr, 2, nullptr);
                if (created != pdPASS) {
                    ESP_LOGE(TAG, "SleepPixelTask konnte nicht gestartet werden");
                }
            }
#endif
#if HMB_TCS34725_ENABLED
            StartColorScanTask();
#endif
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

DECLARE_BOARD(HmbtecEinschlafCoachC3Board);