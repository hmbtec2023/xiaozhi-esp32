/*
 * HMB | TEC Build Information
 * PROJECT_NAME: HMBTEC_MemoryStand_C3
 * PROJECT_VERSION: 0.9.8 (OLED optional)
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
#include "led_strip.h"
#include "device_state_machine.h"
#include "led_strip_spi.h"
#include "assets/lang_config.h"

#define PIXEL_MODE_STATUS  0
#define PIXEL_MODE_LAMP    1
#define HMB_PIXEL_MODE PIXEL_MODE_LAMP  // STATUS oder LAMP
#define HMB_COLORTALES_SOUNDS 1  // OGG-Figurensounds aktiv
#define HMB_TCS34725_ENABLED  0  // 1=Sensor aktiv, 0=Sensor deaktiviert
#define HMB_OLED_ENABLED      0  // 1=SSD1306 aktiv, 0=ohne OLED (NoDisplay)
#define HMB_IR_TX_CARRIER_ENABLED  0  // 1=38 kHz, 0=unmoduliertes NEC (FET direkt an IR-RX der Lampe)

#define STATUS_NEOPIXEL_GPIO        8
#define STATUS_NEOPIXEL_COUNT      16
#define STATUS_NEOPIXEL_BRIGHTNESS 40

#define TAG "HMBMemoryStandC3"
#define HMB_COLORTALES_SOUNDS 1

// ============================================================
// HMB|TEC ColorTales V0.9.10
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

static void StatusPixelFill(uint8_t r, uint8_t g, uint8_t b) {
    if (!status_strip) return;

    for (int i = 0; i < STATUS_NEOPIXEL_COUNT; i++) {
        led_strip_set_pixel(status_strip, i, r, g, b);
    }

    led_strip_refresh(status_strip);
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
// HMB|TEC ColorTales – Farbauswahl und Figurenwechsel V0.9.4
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

    // Außerhalb von ColorTales nur die Lichtfarbe ändern
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
        StatusPixelFill(10, 10, 10);
    #endif

    ESP_LOGI(TAG, "NeoPixel SPI: %d LEDs an GPIO%d bereit",
             STATUS_NEOPIXEL_COUNT,
             STATUS_NEOPIXEL_GPIO);
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

static void UpdateStatusPixels(DeviceState state) {

#if HMB_PIXEL_MODE == PIXEL_MODE_STATUS

    switch (state) {
        case kDeviceStateIdle:
            StatusPixelFill(10, 10, 10);
            break;

        case kDeviceStateListening:
            StatusPixelFill(0, 0, 40);
            break;

        case kDeviceStateSpeaking:
            StatusPixelFill(0, 40, 0);
            break;

        case kDeviceStateConnecting:
            StatusPixelFill(40, 20, 0);
            break;

        case kDeviceStateFatalError:
            StatusPixelFill(40, 0, 0);
            break;

        default:
            break;
    }

#else
    (void)state;
#endif
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
        auto* board = static_cast<HmbtecMemoryStandC3Board*>(arg);
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

        ESP_LOGI(TAG, "HMBTEC MemoryStand C3 V0.9.7");

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
                ESP_LOGI(TAG, "NeoPixel: State %d -> %d",
                        static_cast<int>(old_state),
                        static_cast<int>(new_state));

                UpdateStatusPixels(new_state);
            }
        ); 
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
                        SetSelectedColor(command, false);
                        break;
                    // ==========================================
                    // OFF – KI-Dialog vollständig beenden
                    // ==========================================
                    case IR_NEC_OFF:
                        #if HMB_PIXEL_MODE == PIXEL_MODE_LAMP
                            StatusPixelFill(5, 5, 5);
                        #endif

                        ESP_LOGI(TAG, "IR OFF: KI-Dialog beenden -> IDLE");

                        ct_state = ColorTalesState::CT_OFF;
                        Application::GetInstance().EndConversation();

                        break;
                    // ==========================================
                    // UP / DOWN – XiaoZhi-Audiolautstärke
                    // ==========================================
                    case IR_NEC_UP:
                    case IR_NEC_DOWN: {
                        auto* codec = Board::GetInstance().GetAudioCodec();

                        int volume = codec->output_volume();

                        volume += (command == IR_NEC_UP) ? 10 : -10;
                        volume = std::clamp(volume, 0, 100);

                        codec->SetOutputVolume(volume);

                        ESP_LOGI(TAG, "AUDIO: Lautstaerke = %d%%", volume);
                        break;
                    }
                    //--------------------------------------------------------------
                    // HMB|TEC ColorTales – Aktivierung über ON-Taste
                    //--------------------------------------------------------------
                    case IR_NEC_ON: {
                        const bool white = (ir_selected_color == IR_NEC_WEISS);
                        const bool resume = (ct_state == ColorTalesState::CT_PAUSED);
                        const char* character = GetColorTalesCharacter(ir_selected_color);

                        // WEISS ist der neutrale Story-Start und darf ON nicht blockieren.
                        if (!white && !character) {
                            ESP_LOGW(TAG, "COLORTALES: Keine Figur fuer %s",
                                     GetSelectedColorName(ir_selected_color));
                            break;
                        }

                        #if HMB_PIXEL_MODE == PIXEL_MODE_LAMP
                            UpdateLampPixels(ir_selected_color);
                        #endif

                        ct_active_color = ir_selected_color;
                        ct_state = ColorTalesState::CT_ACTIVE;

                        ESP_LOGI(TAG, "COLORTALES: %s Figur=%s",
                                 resume ? "FORTSETZUNG" : "START",
                                 white ? "ERZAEHLER (WEISS)" : character);

                        // Nach WEISS-Pause die bisherige Geschichte fortsetzen.
                        // Beim ersten ON nach Reset mit WEISS eine neue Geschichte starten.
                        if (resume) {
                            PlayColorTalesSound(ir_selected_color);
                            //! Application::GetInstance().WakeWordInvoke("HMBCOLORTALESWECHSEL", false);
                        } else {
                            PlayColorTalesSound(ir_selected_color);
                            Application::GetInstance().WakeWordInvoke(
                                "HMBCOLORTALES", false);
                        }
                        break;
                    }
                    //--------------------------------------------------------------  
                    case IR_NEC_FLASH:
                    //-------------------------------------------------------------
                        #if HMB_PIXEL_MODE == PIXEL_MODE_LAMP
                            UpdateLampPixels(ir_selected_color);
                        #endif

                        ESP_LOGI(TAG,
                                "AFFIRMATION: FLASH erkannt, Farbe=0x%02X",
                                ir_selected_color);

                        // FLASH-Modus der Lampe beenden
                        // und zuletzt gewählte Farbe wiederherstellen
                        vTaskDelay(pdMS_TO_TICKS(500));
                        SendIrCommand(ir_selected_color);
                        ESP_LOGI(TAG, "AFFIRMATION: Farbe wiederhergestellt");

                        // XiaoZhi-Dialog NICHT starten
                        Application::GetInstance().WakeWordInvoke("HMBPROMPT", true);

                        ESP_LOGI(TAG, "AFFIRMATION: XiaoZhi-Dialog angefordert");
                        break;
                    //-------------------------------------------------------------        
                    case IR_NEC_STROBE:
                    //-------------------------------------------------------------
                        #if HMB_PIXEL_MODE == PIXEL_MODE_LAMP
                            UpdateLampPixels(ir_selected_color);
                        #endif

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
    
                    // ==========================================
                    // FADE – Wissensgestützte Erinnerungsreise
                    // Originalen FADE-Lichteffekt beibehalten
                    // ==========================================
                    case IR_NEC_FADE:
                        ESP_LOGI(TAG,
                                 "ERINNERUNG: FADE erkannt, Farbkontext=0x%02X",
                                 ir_selected_color);

                        // XiaoZhi-Dialog starten
                        Application::GetInstance().WakeWordInvoke(
                            "HMBERINNERUNG", false
                        );

                        ESP_LOGI(TAG, "ERINNERUNG: XiaoZhi-Dialog aktiviert");
                        break;

                    // ==========================================
                    // SMOOTH – Geführte Achtsamkeit
                    // Originalen SMOOTH-Lichteffekt beibehalten
                    // ==========================================
                    case IR_NEC_SMOOTH:
                        ESP_LOGI(TAG,
                                 "ACHTSAMKEIT: SMOOTH erkannt, Farbkontext=0x%02X",
                                 ir_selected_color);

                        // XiaoZhi-Dialog starten
                        Application::GetInstance().WakeWordInvoke(
                            "HMBACHTSAMKEIT", false
                        );

                        ESP_LOGI(TAG, "ACHTSAMKEIT: XiaoZhi aktiviert");
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
            InitializeStatusPixels();
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

DECLARE_BOARD(HmbtecMemoryStandC3Board);