#ifndef HMBTEC_IR_REMOTE_CONTROLLER_H
#define HMBTEC_IR_REMOTE_CONTROLLER_H

#include <driver/gpio.h>
#include <driver/rmt_rx.h>

#include <esp_err.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>
#include <functional>

class HmbtecIrRemoteController {
private:
    static constexpr const char* TAG = "HmbtecIR";

    static constexpr uint32_t RMT_RESOLUTION_HZ = 1000000;  // 1 Tick = 1 us
    static constexpr size_t RMT_SYMBOL_COUNT = 128;

    gpio_num_t gpio_;
    bool initialized_ = false;
    std::function<void(uint8_t)> command_callback_;

    rmt_channel_handle_t rx_channel_ = nullptr;
    rmt_symbol_word_t raw_symbols_[RMT_SYMBOL_COUNT];

    TaskHandle_t worker_task_ = nullptr;

    static bool OnRxDone(
        rmt_channel_handle_t channel,
        const rmt_rx_done_event_data_t* event_data,
        void* user_data
    ) {
        auto* self = static_cast<HmbtecIrRemoteController*>(user_data);

        if (self == nullptr || event_data == nullptr || self->worker_task_ == nullptr) {
            return false;
        }

        BaseType_t high_priority_task_woken = pdFALSE;

        xTaskNotifyFromISR(
            self->worker_task_,
            static_cast<uint32_t>(event_data->num_symbols),
            eSetValueWithOverwrite,
            &high_priority_task_woken
        );

        return high_priority_task_woken == pdTRUE;
    }

    static void WorkerTask(void* parameter) {
        auto* self = static_cast<HmbtecIrRemoteController*>(parameter);

        if (self == nullptr) {
            vTaskDelete(nullptr);
            return;
        }

        while (true) {
            uint32_t symbol_count = 0;

            xTaskNotifyWait(
                0,
                UINT32_MAX,
                &symbol_count,
                portMAX_DELAY
            );

            self->ProcessFrame(symbol_count);

            if (!self->StartReceive()) {
                ESP_LOGE(TAG, "Unable to restart IR reception");
            }
        }
    }

    void ProcessFrame(uint32_t symbol_count) {
    if (symbol_count < 34) {
        return;
    }

    // NEC Leader prüfen:
    // ca. 9 ms LOW + 4.5 ms HIGH
    const auto& leader = raw_symbols_[0];

    if (leader.duration0 < 8000 || leader.duration0 > 10000 ||
        leader.duration1 < 3500 || leader.duration1 > 5500) {
        return;
    }

    uint32_t data = 0;

    // NEC überträgt jedes Byte LSB first.
    for (uint32_t i = 0; i < 32; ++i) {
        const auto& symbol = raw_symbols_[i + 1];

        // LOW-Puls liegt ungefähr bei 560 us.
        if (symbol.duration0 < 350 || symbol.duration0 > 800) {
            ESP_LOGW(TAG, "Invalid NEC pulse at bit %lu",
                     static_cast<unsigned long>(i));
            return;
        }

        // HIGH:
        // ca. 560 us  -> 0
        // ca. 1690 us -> 1
        bool bit = symbol.duration1 > 1000;

        if (bit) {
            data |= (1UL << i);
        }
    }

    uint8_t address     =  data        & 0xFF;
    uint8_t address_inv = (data >> 8)  & 0xFF;
    uint8_t command     = (data >> 16) & 0xFF;
    uint8_t command_inv = (data >> 24) & 0xFF;

    bool address_ok =
        static_cast<uint8_t>(address ^ address_inv) == 0xFF;

    bool command_ok =
        static_cast<uint8_t>(command ^ command_inv) == 0xFF;

    ESP_LOGI(
        TAG,
        "NEC: raw=0x%08lX addr=0x%02X addrInv=0x%02X cmd=0x%02X cmdInv=0x%02X valid=%s",
        static_cast<unsigned long>(data),
        address,
        address_inv,
        command,
        command_inv,
        (address_ok && command_ok) ? "YES" : "NO"
    );

    if (address_ok && command_ok && command_callback_) {
        command_callback_(command);
    }

}
    
    bool StartReceive() {
        if (!initialized_ || rx_channel_ == nullptr) {
            return false;
        }

        rmt_receive_config_t receive_config = {};
        receive_config.signal_range_min_ns = 1000;       // 1 us
        receive_config.signal_range_max_ns = 15000000;   // 15 ms

        esp_err_t err = rmt_receive(
            rx_channel_,
            raw_symbols_,
            sizeof(raw_symbols_),
            &receive_config
        );

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "rmt_receive failed: %s",
                esp_err_to_name(err)
            );
            return false;
        }

        return true;
    }

public:
    explicit HmbtecIrRemoteController(gpio_num_t gpio)
        : gpio_(gpio) {
    }

    void SetCommandCallback(std::function<void(uint8_t)> callback) {
        command_callback_ = std::move(callback);
    }

    ~HmbtecIrRemoteController() {
        initialized_ = false;

        if (rx_channel_ != nullptr) {
            rmt_disable(rx_channel_);
            rmt_del_channel(rx_channel_);
            rx_channel_ = nullptr;
        }

        if (worker_task_ != nullptr) {
            vTaskDelete(worker_task_);
            worker_task_ = nullptr;
        }
    }

    bool Initialize() {
        if (gpio_ == GPIO_NUM_NC) {
            ESP_LOGI(TAG, "IR receiver disabled (GPIO_NUM_NC)");
            return false;
        }

        ESP_LOGI(
            TAG,
            "Initializing IR receiver on GPIO%d",
            static_cast<int>(gpio_)
        );

        rmt_rx_channel_config_t rx_config = {};
        rx_config.gpio_num = gpio_;
        rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
        rx_config.resolution_hz = RMT_RESOLUTION_HZ;
        rx_config.mem_block_symbols = 64;
        rx_config.intr_priority = 0;
        rx_config.flags.invert_in = false;
        rx_config.flags.with_dma = false;

        esp_err_t err = rmt_new_rx_channel(
            &rx_config,
            &rx_channel_
        );

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "rmt_new_rx_channel failed: %s",
                esp_err_to_name(err)
            );
            rx_channel_ = nullptr;
            return false;
        }

        BaseType_t task_result = xTaskCreate(
            WorkerTask,
            "hmb_ir_rx",
            3072,
            this,
            5,
            &worker_task_
        );

        if (task_result != pdPASS) {
            ESP_LOGE(TAG, "Unable to create IR worker task");

            rmt_del_channel(rx_channel_);
            rx_channel_ = nullptr;
            return false;
        }

        rmt_rx_event_callbacks_t callbacks = {};
        callbacks.on_recv_done = OnRxDone;

        err = rmt_rx_register_event_callbacks(
            rx_channel_,
            &callbacks,
            this
        );

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "rmt_rx_register_event_callbacks failed: %s",
                esp_err_to_name(err)
            );

            vTaskDelete(worker_task_);
            worker_task_ = nullptr;

            rmt_del_channel(rx_channel_);
            rx_channel_ = nullptr;
            return false;
        }

        err = rmt_enable(rx_channel_);

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "rmt_enable failed: %s",
                esp_err_to_name(err)
            );

            vTaskDelete(worker_task_);
            worker_task_ = nullptr;

            rmt_del_channel(rx_channel_);
            rx_channel_ = nullptr;
            return false;
        }

        initialized_ = true;

        ESP_LOGI(
            TAG,
            "IR receiver ready on GPIO%d",
            static_cast<int>(gpio_)
        );

        if (!StartReceive()) {
            ESP_LOGE(TAG, "Unable to start IR reception");

            initialized_ = false;

            rmt_disable(rx_channel_);
            rmt_del_channel(rx_channel_);
            rx_channel_ = nullptr;

            vTaskDelete(worker_task_);
            worker_task_ = nullptr;

            return false;
        }

        return true;
    }

    bool IsInitialized() const {
        return initialized_;
    }
};

#endif // HMBTEC_IR_REMOTE_CONTROLLER_H