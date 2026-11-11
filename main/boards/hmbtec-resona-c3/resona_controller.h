#pragma once

/*
 * HMB | TEC Build Information
 * PROJECT_NAME: HMBTEC_RESONA_AI
 * PROJECT_VERSION: 0.1.1
 * BOARD: ESP32-C3 SuperMini
 * FRAMEWORK: ESP-IDF 5/6 / XiaoZhi
 *
 * HARDWARE:
 * - LD2410 UART1 RX=GPIO1, TX=GPIO2, 256000 Baud
 * - Flammen-Flackerprozessor PWM=GPIO6
 * - Maximale Flammenhelligkeit: 85 %
 *
 * FUNKTIONEN:
 * - LD2410-Präsenzerkennung
 * - CALM-Erkennung mit Hysterese 92/88
 * - Automatische Begrüßungsanforderung nach 3 s CALM
 * - Einmaliger Begrüßungs-Callback pro Anwesenheitszyklus
 * - 60-s-Wartefenster nach abgeschlossener Begrüßung
 * - Einmalige Folgefrage bei ausbleibender Spracheingabe
 * - Rücksetzung nach 10 s Abwesenheit
 *
 * HINWEIS:
 * Der Callback wird aus der RESONA-FreeRTOS-Task aufgerufen.
 * Die Boardintegration muss den XiaoZhi-Aufruf entsprechend
 * threadsicher ausführen.
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <algorithm>

#include <driver/uart.h>
#include <driver/ledc.h>

#include <esp_timer.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define RESONA_FLAME_PWM_GPIO GPIO_NUM_6

namespace resona {

struct Snapshot {
    bool presence = false;
    bool moving = false;
    bool still = false;
    bool calm = false;

    uint8_t level = 0;
    uint8_t pwm = 0;

    bool greeting_pending = false;
    bool followup_pending = false;
};

class Controller {

private:

    // =========================================================
    // Hardware
    // =========================================================

    uart_port_t port_ = UART_NUM_1;

    static constexpr uint8_t max_pwm_ = 217;   // ca. 85 %
    static constexpr uint8_t base_pwm_ = 120;

    // =========================================================
    // Zustandsvariablen
    // =========================================================

    float level_ = 0.0f;

    bool presence_ = false;
    bool moving_ = false;
    bool still_ = false;
    bool calm_ = false;

    bool greeting_sent_ = false;
    bool followup_sent_ = false;

    bool greeting_pending_ = false;
    bool followup_pending_ = false;

    uint8_t pwm_ = 0;

    // =========================================================
    // Zeitverwaltung (Mikrosekunden)
    // =========================================================

    int64_t last_frame_ = 0;
    int64_t last_update_ = 0;

    int64_t absence_since_ = 0;
    int64_t calm_since_ = 0;
    int64_t listen_since_ = 0;

    // =========================================================
    // UART-Frame
    // =========================================================

    uint8_t frame_[64] = {};
    size_t used_ = 0;

    // =========================================================
    // XiaoZhi-Callback
    // =========================================================

    using GreetingCallback = void (*)();

    GreetingCallback greeting_callback_ = nullptr;

    // =========================================================
    // FreeRTOS-Worker
    // =========================================================

    static void worker(void* arg) {

        auto* controller =
            static_cast<Controller*>(arg);

        for (;;) {

            controller->tick();

            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    // =========================================================
    // LD2410 UART-Frame auswerten
    // =========================================================

    void parse(uint8_t b, int64_t now) {

        static constexpr uint8_t header[] = {
            0xF4, 0xF3, 0xF2, 0xF1
        };

        if (used_ < 4) {

            if (b == header[used_]) {
                frame_[used_++] = b;
            } else {
                used_ = (b == header[0]) ? 1 : 0;
            }

            return;
        }

        if (used_ >= sizeof(frame_)) {
            used_ = 0;
            return;
        }

        frame_[used_++] = b;

        if (used_ == 6) {

            uint16_t len =
                frame_[4] |
                (frame_[5] << 8);

            if (len < 5 || len > 48) {
                used_ = 0;
            }

            return;
        }

        if (used_ < 6) {
            return;
        }

        size_t expected =
            6 +
            (frame_[4] | (frame_[5] << 8)) +
            4;

        if (used_ != expected) {
            return;
        }

        bool valid_footer =
            frame_[expected - 4] == 0xF8 &&
            frame_[expected - 3] == 0xF7 &&
            frame_[expected - 2] == 0xF6 &&
            frame_[expected - 1] == 0xF5;

        bool valid_target =
            frame_[6] == 0x02 &&
            frame_[7] == 0xAA;

        if (valid_footer && valid_target) {

            uint8_t state = frame_[8];

            presence_ =
                (state == 1 ||
                 state == 2 ||
                 state == 3);

            moving_ =
                (state == 1 ||
                 state == 3);

            still_ =
                (state == 2 ||
                 state == 3);

            last_frame_ = now;
        }

        used_ = 0;
    }

    // =========================================================
    // RESONA-Zustandsmaschine
    // =========================================================

    void tick() {

        uint8_t rx[128];

        int n = uart_read_bytes(
            port_,
            rx,
            sizeof(rx),
            0
        );

        int64_t now = esp_timer_get_time();

        for (int i = 0; i < n; i++) {
            parse(rx[i], now);
        }

        // -----------------------------------------------------
        // LD2410 Timeout
        // -----------------------------------------------------

        if (last_frame_ == 0 ||
            now - last_frame_ > 1500000) {

            presence_ = false;
            moving_ = false;
            still_ = false;
        }

        // -----------------------------------------------------
        // Zeitdifferenz
        // -----------------------------------------------------

        float dt = last_update_
            ? std::min(
                0.25f,
                (now - last_update_) / 1000000.0f
              )
            : 0.0f;

        last_update_ = now;

        // -----------------------------------------------------
        // Ruhelevel berechnen
        // -----------------------------------------------------

        float rate =
            presence_ && still_ && !moving_
                ? 28.0f
                : !presence_
                    ? -35.0f
                    : moving_ && !still_
                        ? -22.0f
                        : -8.0f;

        level_ = std::clamp(
            level_ + rate * dt,
            0.0f,
            100.0f
        );

        // -----------------------------------------------------
        // Abwesenheit / Reset nach 10 Sekunden
        // -----------------------------------------------------

        if (!presence_) {

            if (!absence_since_) {
                absence_since_ = now;
            }

            if (now - absence_since_ >= 10000000) {

                greeting_sent_ = false;
                followup_sent_ = false;

                greeting_pending_ = false;
                followup_pending_ = false;

                listen_since_ = 0;
            }

        } else {

            absence_since_ = 0;
        }

        // -----------------------------------------------------
        // CALM-Hysterese
        // -----------------------------------------------------

        if (!calm_ &&
            presence_ &&
            level_ >= 92.0f) {

            calm_ = true;
            calm_since_ = now;
        }

        if (calm_ &&
            (!presence_ || level_ <= 88.0f)) {

            calm_ = false;
            calm_since_ = 0;
        }

        // -----------------------------------------------------
        // Automatische KI-Begrüßung
        // -----------------------------------------------------

        if (calm_ &&
            calm_since_ &&
            now - calm_since_ >= 3000000 &&
            !greeting_sent_) {

            greeting_pending_ = true;
            greeting_sent_ = true;

            ESP_LOGI(
                "RESONA",
                "CALM: Begruessung angefordert"
            );

            // Callback an Boardintegration
            if (greeting_callback_) {

                ESP_LOGI(
                    "RESONA",
                    "KI-Begruessungs-Callback"
                );

                greeting_callback_();
            }
        }

        // -----------------------------------------------------
        // 60 Sekunden auf Spracheingabe warten
        // -----------------------------------------------------

        if (listen_since_ &&
            !followup_sent_ &&
            now - listen_since_ >= 60000000) {

            followup_pending_ = true;
            followup_sent_ = true;

            listen_since_ = 0;

            ESP_LOGI(
                "RESONA",
                "60s ohne Sprache: Folgefrage angefordert"
            );
        }

        // -----------------------------------------------------
        // Flammenhelligkeit
        // -----------------------------------------------------

        uint8_t target = 0;

        if (presence_) {

            target = base_pwm_;

            if (calm_) {

                float t = std::clamp(
                    (level_ - 92.0f) / 8.0f,
                    0.0f,
                    1.0f
                );

                target = static_cast<uint8_t>(
                    base_pwm_ +
                    t * (max_pwm_ - base_pwm_) +
                    0.5f
                );
            }
        }

        // -----------------------------------------------------
        // Sanftes PWM-Fading
        // -----------------------------------------------------

        if (pwm_ < target) {

            pwm_++;

        } else if (pwm_ > target) {

            pwm_ = static_cast<uint8_t>(
                std::max(
                    0,
                    static_cast<int>(pwm_) - 2
                )
            );
        }

        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            pwm_
        );

        ledc_update_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0
        );
    }

public:

    // =========================================================
    // Begrüßungs-Callback registrieren
    // =========================================================

    void set_greeting_callback(
        GreetingCallback callback
    ) {
        greeting_callback_ = callback;
    }

    // =========================================================
    // Hardware initialisieren
    // =========================================================

    bool begin() {

        uart_config_t uc = {};

        uc.baud_rate = 256000;
        uc.data_bits = UART_DATA_8_BITS;
        uc.parity = UART_PARITY_DISABLE;
        uc.stop_bits = UART_STOP_BITS_1;
        uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uc.source_clk = UART_SCLK_DEFAULT;

        if (uart_driver_install(
                port_,
                2048,
                0,
                0,
                nullptr,
                0
            ) != ESP_OK) {

            return false;
        }

        if (uart_param_config(
                port_,
                &uc
            ) != ESP_OK) {

            return false;
        }

        // LD2410 RX=GPIO1, TX=GPIO2
        if (uart_set_pin(
                port_,
                2,
                1,
                UART_PIN_NO_CHANGE,
                UART_PIN_NO_CHANGE
            ) != ESP_OK) {

            return false;
        }

        // -----------------------------------------------------
        // LEDC PWM
        // -----------------------------------------------------

        ledc_timer_config_t timer = {};

        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.timer_num = LEDC_TIMER_0;
        timer.duty_resolution = LEDC_TIMER_8_BIT;
        timer.freq_hz = 1000;
        timer.clk_cfg = LEDC_AUTO_CLK;

        if (ledc_timer_config(
                &timer
            ) != ESP_OK) {

            return false;
        }

        ledc_channel_config_t channel = {};

        channel.gpio_num =
            RESONA_FLAME_PWM_GPIO;

        channel.speed_mode =
            LEDC_LOW_SPEED_MODE;

        channel.channel =
            LEDC_CHANNEL_0;

        channel.timer_sel =
            LEDC_TIMER_0;

        channel.duty = 0;
        channel.hpoint = 0;

        if (ledc_channel_config(
                &channel
            ) != ESP_OK) {

            return false;
        }

        // -----------------------------------------------------
        // RESONA-Worker starten
        // -----------------------------------------------------

        return xTaskCreate(
            worker,
            "resona",
            4096,
            this,
            5,
            nullptr
        ) == pdPASS;
    }

    // =========================================================
    // Aktuellen Zustand lesen
    // =========================================================

    Snapshot snapshot() const {

        return {
            presence_,
            moving_,
            still_,
            calm_,
            static_cast<uint8_t>(level_ + 0.5f),
            pwm_,
            greeting_pending_,
            followup_pending_
        };
    }

    // =========================================================
    // XiaoZhi-Dialogereignisse
    // =========================================================

    // Erst nach tatsächlich abgeschlossener Begrüßungs-TTS!
    void greeting_finished() {

        greeting_pending_ = false;

        listen_since_ = esp_timer_get_time();

        ESP_LOGI(
            "RESONA",
            "Begruessung beendet: 60s-Timer gestartet"
        );
    }

    // Nutzer hat gesprochen
    void user_spoke() {

        listen_since_ = 0;

        followup_pending_ = false;
        followup_sent_ = true;
    }

    // Folgefrage verarbeitet
    void followup_handled() {

        followup_pending_ = false;
    }
};

} // namespace resona