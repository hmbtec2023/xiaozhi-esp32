#ifndef __BUZZER_CONTROLLER_H__
#define __BUZZER_CONTROLLER_H__

#include "mcp_server.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


class BuzzerController {
private:
    gpio_num_t gpio_num_;

    void Beep(uint8_t count) {
        for (uint8_t i = 0; i < count; ++i) {
            gpio_set_level(gpio_num_, 1);
            vTaskDelay(pdMS_TO_TICKS(100));

            gpio_set_level(gpio_num_, 0);

            if (i + 1 < count) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }

public:
    explicit BuzzerController(gpio_num_t gpio_num) : gpio_num_(gpio_num) {
        if (gpio_num_ == GPIO_NUM_NC) {
            return;
        }

        gpio_config_t config = {};
        config.pin_bit_mask = (1ULL << gpio_num_);
        config.mode = GPIO_MODE_OUTPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;

        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(gpio_num_, 0);

        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.buzzer.beep",
            "Emit short audible beeps. count specifies the number of beeps from 1 to 5.",
            PropertyList({
                Property("count", kPropertyTypeInteger, 1, 1, 5)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int count = properties["count"].value<int>();
                Beep(static_cast<uint8_t>(count));
                return true;
            }
        );
    }
};


#endif // __BUZZER_CONTROLLER_H__
