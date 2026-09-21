#ifndef __HMBTEC_LIGHT_CONTROLLER_H__
#define __HMBTEC_LIGHT_CONTROLLER_H__

#include "led/circular_strip.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>

class HmbtecLightController {
private:
    CircularStrip* led_strip_;
    int brightness_level_ = 4;
    std::function<void()> afterglow_finished_callback_;

    int LevelToBrightness(int level) const {
        if (level < 0) level = 0;
        if (level > 8) level = 8;

        return (1 << level) - 1;
    }

    StripColor RGBToColor(int red, int green, int blue) const {
        return {
            static_cast<uint8_t>(red),
            static_cast<uint8_t>(green),
            static_cast<uint8_t>(blue)
        };
    }

public:
    void SetAfterglowFinishedCallback(std::function<void()> callback) {
        afterglow_finished_callback_ = std::move(callback);
    }

    void FinishLichtblick() {
        if (led_strip_ == nullptr) {
            return;
        }

        ESP_LOGI(
            "HmbtecLight",
            "Lichtblick finished -> keep breathing for 30 seconds"
        );

        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<HmbtecLightController*>(arg);

                // Keep the AI-selected breathing animation running.
                vTaskDelay(pdMS_TO_TICKS(30000));

                ESP_LOGI(
                    "HmbtecLight",
                    "Lichtblick afterglow finished"
                );

                if (self->afterglow_finished_callback_) {
                    self->afterglow_finished_callback_();
                } else {
                    self->led_strip_->SetAllColor({0, 0, 0});
                }

                vTaskDelete(nullptr);
            },
            "lichtblick_afterglow",
            2048,
            this,
            2,
            nullptr
        );
    }

    explicit HmbtecLightController(CircularStrip* led_strip)
        : led_strip_(led_strip) {

        if (led_strip_ == nullptr) {
            return;
        }

        // Moderate default brightness
        led_strip_->SetBrightness(LevelToBrightness(brightness_level_), 4);

        auto& mcp_server = McpServer::GetInstance();

        /*
        * HMB|TEC firmware prompt.
        *
        * HMBPROMPT is only a short semantic trigger.
        * The actual instruction is stored locally in the firmware and
        * returned to the AI through MCP.
        */
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
                    "Erzeuge eine kurze, ruhige und positive Affirmation. "
                    "Maximal zwei kurze Saetze. "
                    "Keine Rueckfrage. "
                    "Sprich die Affirmation direkt aus."
                );
            }
        );

        /*
         * Set all 8 LEDs to one RGB color.
         *
         * The AI can choose the color according to the conversational
         * context, for example warm colors for comfort or cool colors
         * for a calm atmosphere.
         */
        mcp_server.AddTool(
            "self.light.set_color",
            "Set the Lichtblick light ring to an RGB color. "
            "Choose a suitable color according to the user's request or conversational context.",
            PropertyList({
                Property("red", kPropertyTypeInteger, 0, 255),
                Property("green", kPropertyTypeInteger, 0, 255),
                Property("blue", kPropertyTypeInteger, 0, 255)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int red = properties["red"].value<int>();
                int green = properties["green"].value<int>();
                int blue = properties["blue"].value<int>();

                ESP_LOGI(
                    "HmbtecLight",
                    "Set color R=%d G=%d B=%d",
                    red,
                    green,
                    blue
                );

                led_strip_->SetAllColor(RGBToColor(red, green, blue));

                return true;
            }
        );

        /*
         * Brightness is deliberately expressed as level 0..8,
         * matching the existing XiaoZhi CircularStrip implementation.
         */
        mcp_server.AddTool(
            "self.light.set_brightness",
            "Set the Lichtblick light ring brightness. "
            "0 is off/dark and 8 is maximum brightness.",
            PropertyList({
                Property("level", kPropertyTypeInteger, 0, 8)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int level = properties["level"].value<int>();

                ESP_LOGI(
                    "HmbtecLight",
                    "Set brightness level=%d",
                    level
                );

                brightness_level_ = level;

                led_strip_->SetBrightness(
                    LevelToBrightness(brightness_level_),
                    4
                );

                return true;
            }
        );

        /*
         * Explicit OFF command.
         */
        mcp_server.AddTool(
            "self.light.off",
            "Turn the Lichtblick light ring off.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI("HmbtecLight", "Light off");

                led_strip_->SetAllColor(RGBToColor(0, 0, 0));

                return true;
            }
        );

        /*
         * Soft breathing animation.
         */
        mcp_server.AddTool(
            "self.light.breathe",
            "Create a soft breathing light effect on the Lichtblick light ring "
            "using the specified RGB color. Use this for calm, relaxing or supportive light feedback.",
            PropertyList({
                Property("red", kPropertyTypeInteger, 0, 255),
                Property("green", kPropertyTypeInteger, 0, 255),
                Property("blue", kPropertyTypeInteger, 0, 255)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int red = properties["red"].value<int>();
                int green = properties["green"].value<int>();
                int blue = properties["blue"].value<int>();

                ESP_LOGI(
                    "HmbtecLight",
                    "Breathe R=%d G=%d B=%d",
                    red,
                    green,
                    blue
                );

                StripColor low = RGBToColor(
                    red / 20,
                    green / 20,
                    blue / 20
                );

                StripColor high = RGBToColor(
                    red,
                    green,
                    blue
                );

                led_strip_->Breathe(low, high, 40);

                return true;
            }
        );
    }
};

#endif