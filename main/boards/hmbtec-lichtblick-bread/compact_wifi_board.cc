#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "buzzer_controller.h"
#include "led/circular_strip.h"
#include "light_controller.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <ctime>
#include "ir_remote_controller.h"
#include <esp_timer.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button lichtblick_button_;
    CircularStrip* pixel_ring_ = nullptr;
    HmbtecLightController* light_controller_ = nullptr;
    HmbtecIrRemoteController ir_remote_;
    bool lichtblick_ptt_active_ = false;
    bool lichtblick_effect_active_ = false;
    bool ptt_interaction_active_ = false;
    // ------------------------------------------------------------------------
    // HMB|TEC IR light control
    // ------------------------------------------------------------------------
    StripColor ir_light_color_ = {255, 80, 20};  // warmes Standardlicht
    int ir_brightness_level_ = 4;
    bool ir_light_on_ = false;
    uint8_t ir_last_command_ = 0;
    int64_t ir_last_command_time_ = 0;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    bool DetectOled() {
        constexpr uint8_t OLED_ADDRESS = 0x3C;

        esp_err_t err = i2c_master_probe(
            display_i2c_bus_,
            OLED_ADDRESS,
            100
        );

        if (err == ESP_OK) {
            ESP_LOGI(
                TAG,
                "OLED detected at I2C address 0x%02X",
                OLED_ADDRESS
            );
            return true;
        }

        ESP_LOGI(
            TAG,
            "No OLED detected at I2C address 0x%02X -> headless mode",
            OLED_ADDRESS
        );

        return false;
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        #ifdef SH1106
                ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
        #else
                ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        #endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();

            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }

            app.ToggleChatState();
        });

        // ------------------------------------------------------------------------
        // HMB|TEC Lichtblick Button
        //
        // Short Press:
        //   Lichtblick One-Shot
        //
        // Long Press >= 700 ms:
        //   Push-to-Talk starten
        //
        // Release nach Long Press:
        //   Push-to-Talk beenden
        // ------------------------------------------------------------------------
        lichtblick_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Lichtblick button -> trigger");
            TriggerLichtblick();
        });

        lichtblick_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Lichtblick long press -> PTT start");

            lichtblick_ptt_active_ = true;
            ptt_interaction_active_ = true;

            // Clock display must not remain visible during the voice interaction.
            if (pixel_ring_ != nullptr) {
                pixel_ring_->SetAllColor({0, 0, 0});
            }

            auto& app = Application::GetInstance();
            app.StartListening();
        });

        lichtblick_button_.OnPressUp([this]() {
            if (!lichtblick_ptt_active_) {
                return;
            }

            ESP_LOGI(TAG, "Lichtblick PTT released -> stop listening");

            lichtblick_ptt_active_ = false;

            auto& app = Application::GetInstance();
            app.StopListening();
        });

        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void ShowClockHour() {
        if (pixel_ring_ == nullptr) {
            return;
        }

    time_t now = time(nullptr);

    // Vor einer gültigen Serversynchronisation keine falsche Uhrzeit anzeigen.
    if (now < 1700000000) {
        ESP_LOGW(TAG, "Clock: system time not synchronized yet");
        pixel_ring_->SetAllColor({0, 0, 0});
        return;
    }

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    uint8_t hour = timeinfo.tm_hour % 12;

    // Physischer Ring:
    // Pixel 0 = 6 Uhr
    // Zählrichtung = clockwise
    // Daher liegt 12 Uhr auf Pixel 6.
    uint8_t hour_pixel = (hour + 6) % 12; // org=12 

    ESP_LOGI(
        TAG,
        "Clock: %02d:%02d -> hour pixel %d",
        timeinfo.tm_hour,
        timeinfo.tm_min,
        hour_pixel
    );

    pixel_ring_->SetAllColor({0, 0, 0});

    // Zunächst bewusst dezent.
    pixel_ring_->SetSingleColor(hour_pixel, {20*3, 12*3, 4*3});
}

    void InitializePixelRing() {
        ESP_LOGI(
            TAG,
            "Initializing HMBTEC NeoPixel ring: GPIO%d, %d pixels",
            HMBTEC_PIXEL_RING_GPIO,
            HMBTEC_PIXEL_RING_COUNT
        );

        pixel_ring_ = new CircularStrip(
            HMBTEC_PIXEL_RING_GPIO,
            HMBTEC_PIXEL_RING_COUNT
        );

        // HMB|TEC startup ring animation
        pixel_ring_->SetAllColor({0, 0, 0});

        for (uint8_t i = 0; i < HMBTEC_PIXEL_RING_COUNT; i++) {
            pixel_ring_->SetAllColor({0, 0, 0});
            pixel_ring_->SetSingleColor(i, {0, 100, 0}); // grün

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        pixel_ring_->SetAllColor({0, 0, 0});
    }

    void TriggerLichtblick() {
        ESP_LOGI(TAG, "Lichtblick triggered");

        auto& app = Application::GetInstance();
        app.WakeWordInvoke("HMBPROMPT", true);
    }

    void ApplyIrLight() {
        if (pixel_ring_ == nullptr) {
            return;
        }

        if (!ir_light_on_ || ir_brightness_level_ == 0) {
            pixel_ring_->SetAllColor({0, 0, 0});
            return;
        }

        // Level 1...8 -> ca. 12,5...100 %
        const uint16_t scale = ir_brightness_level_;

        StripColor color = {
            static_cast<uint8_t>(
                (static_cast<uint16_t>(ir_light_color_.red) * scale) / 8
            ),
            static_cast<uint8_t>(
                (static_cast<uint16_t>(ir_light_color_.green) * scale) / 8
            ),
            static_cast<uint8_t>(
                (static_cast<uint16_t>(ir_light_color_.blue) * scale) / 8
            )
        };

        pixel_ring_->SetAllColor(color);

        ESP_LOGI(
            TAG,
            "IR light: level=%d RGB=%d,%d,%d",
            ir_brightness_level_,
            color.red,
            color.green,
            color.blue
        );
    }

    void HandleIrCommand(uint8_t command) {
        const int64_t now = esp_timer_get_time();

        // Gleichen IR-Befehl innerhalb von 300 ms nur einmal ausführen.
        if (command == ir_last_command_ &&
            (now - ir_last_command_time_) < 300000) {
            ESP_LOGI(
                TAG,
                "IR duplicate ignored: 0x%02X",
                command
            );
            return;
        }

        ir_last_command_ = command;
        ir_last_command_time_ = now;

        ESP_LOGI(TAG, "Handle IR command: 0x%02X", command);

        if (pixel_ring_ == nullptr) {
            // RESET muss auch ohne Pixelring funktionieren.
            if (command == 0x42) {
                ESP_LOGI(TAG, "IR RESET -> Lichtblick");
                TriggerLichtblick();
            } else {
                ESP_LOGW(TAG, "IR light command ignored: pixel ring unavailable");
            }
            return;
        }

        switch (command) {
            case 0x47:  // OFF
                ESP_LOGI(TAG, "IR OFF");
                ir_light_on_ = false;
                ApplyIrLight();
                break;

            case 0x45:  // ON
                ESP_LOGI(TAG, "IR ON");

                if (ir_brightness_level_ == 0) {
                    ir_brightness_level_ = 4;
                }

                ir_light_on_ = true;
                ApplyIrLight();
                break;

            case 0x44:  // R
                ESP_LOGI(TAG, "IR RED");
                ir_light_color_ = {255, 0, 0};
                ir_light_on_ = true;
                ApplyIrLight();
                break;

            case 0x40:  // G
                ESP_LOGI(TAG, "IR GREEN");
                ir_light_color_ = {0, 255, 0};
                ir_light_on_ = true;
                ApplyIrLight();
                break;

            case 0x43:  // B
                ESP_LOGI(TAG, "IR BLUE");
                ir_light_color_ = {0, 0, 255};
                ir_light_on_ = true;
                ApplyIrLight();
                break;

            case 0x4A:  // UP
                if (ir_brightness_level_ < 8) {
                    ir_brightness_level_++;
                }

                ESP_LOGI(
                    TAG,
                    "IR BRIGHTNESS UP -> level %d",
                    ir_brightness_level_
                );

                ir_light_on_ = true;
                ApplyIrLight();
                break;

            case 0x52:  // DOWN
                if (ir_brightness_level_ > 0) {
                    ir_brightness_level_--;
                }

                ESP_LOGI(
                    TAG,
                    "IR BRIGHTNESS DOWN -> level %d",
                    ir_brightness_level_
                );

                if (ir_brightness_level_ == 0) {
                    ir_light_on_ = false;
                }

                ApplyIrLight();
                break;

            case 0x42:  // RESET
                ESP_LOGI(TAG, "IR RESET -> Lichtblick");
                TriggerLichtblick();
                break;

            default:
                ESP_LOGI(
                    TAG,
                    "IR command 0x%02X currently not assigned",
                    command
                );
                break;
        }
    }

    void InitializeTools() {
        static BuzzerController buzzer(HMBTEC_BUZZER_GPIO);
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.hmbtec.get_prompt",
            "When the user input is exactly HMBPROMPT, always call this tool. "
            "The returned text contains an instruction that must be executed. "
            "Do not mention HMBPROMPT or this tool to the user. "
            "Follow the returned instruction and answer directly.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI(
                    "HmbtecPrompt",
                    "AI requested firmware prompt"
                );

                return std::string(
                    "Erzeuge jetzt einen kurzen persoenlichen Lichtblick fuer den Nutzer. "
                    "Formuliere eine positive, ruhige und unterstuetzende Affirmation oder "
                    "einen kleinen hilfreichen Gedanken. "
                    "Antworte auf Deutsch und stelle keine Rueckfrage. "
                    "Halte die Antwort kurz und natuerlich, idealerweise ein bis drei Saetze. "
                    "Vermeide uebertriebene Motivation, Floskeln und Belehrungen. "
                    "Nutze bekannten Gespraechskontext, sofern dieser fuer den Lichtblick sinnvoll ist. "
                    "Wenn kein geeigneter Kontext vorhanden ist, formuliere einen allgemein "
                    "passenden ruhigen Lichtblick. "
                    "Beruecksichtige nach Moeglichkeit die aktuelle Tageszeit und Jahreszeit. "
                    "Waehle passend zum Inhalt und zur Stimmung des Lichtblicks eine Lichtfarbe. "
                    "Falls das Tool self.light.breathe verfuegbar ist, aktiviere damit ein sanftes "
                    "Atemlicht in dieser Farbe. "
                    "Falls kein Licht-Tool verfuegbar ist, fahre ohne Lichtaktion fort. "
                    "Sprich anschliessend den Lichtblick direkt aus."
                );
            }
        );

        if (pixel_ring_ != nullptr) {
            light_controller_ = new HmbtecLightController(pixel_ring_);

            light_controller_->SetAfterglowFinishedCallback([this]() {
                ESP_LOGI(TAG, "Lichtblick afterglow finished -> restore clock");
                lichtblick_effect_active_ = false;
                ShowClockHour();
            });
        } else {
            ESP_LOGW(TAG, "HMBTEC light controller disabled: no pixel ring available");
        }

        auto& app = Application::GetInstance();
        app.RegisterStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
            ESP_LOGI(
                TAG,
                "HMBTEC state change: %d -> %d",
                static_cast<int>(old_state),
                static_cast<int>(new_state)
            );

            if (new_state == kDeviceStateIdle && !lichtblick_effect_active_) {
                if (ptt_interaction_active_) {
                    ESP_LOGI(TAG, "PTT interaction finished -> restore clock");
                    ptt_interaction_active_ = false;
                } else {
                    ESP_LOGI(TAG, "Idle -> show clock");
                }

                ShowClockHour();
            }
        });

        app.RegisterOneShotFinishedCallback([this]() {
            if (light_controller_ != nullptr) {
                ESP_LOGI(TAG, "Lichtblick one-shot finished -> start 30s afterglow");
                light_controller_->FinishLichtblick();
            }
        });
    }

public:
CompactWifiBoard() :
    boot_button_(BOOT_BUTTON_GPIO),
    touch_button_(TOUCH_BUTTON_GPIO),
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
    lichtblick_button_(HMBTEC_BUTTON_GPIO, false, 700),
    ir_remote_(HMBTEC_IR_RX_GPIO) {

        InitializeDisplayI2c();

    if (DetectOled()) {
        InitializeSsd1306Display();
    } else {
        display_ = new NoDisplay();
    }

    InitializeButtons();
    ir_remote_.SetCommandCallback([this](uint8_t command) {
        ESP_LOGI(TAG, "IR command received: 0x%02X", command);
        HandleIrCommand(command);
    });
    ir_remote_.Initialize();
    if (HMBTEC_IR_RX_GPIO != HMBTEC_PIXEL_RING_GPIO) {
        InitializePixelRing();
    } else {
        ESP_LOGW(
            TAG,
            "NeoPixel ring disabled: GPIO%d is currently used for IR RX",
            HMBTEC_IR_RX_GPIO
        );
    }
    InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
