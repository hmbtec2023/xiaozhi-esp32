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
#include <driver/gpio.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <ctime>
#if HMB_PWA_STATS_EN
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#endif
#include "ir_remote_controller.h"
#include <utility>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_=nullptr;
    esp_lcd_panel_handle_t panel_=nullptr;
    Display* display_=nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button lichtblick_button_;
    CircularStrip* pixel_ring_=nullptr;
    HmbtecLightController* light_controller_=nullptr;
    HmbtecIrRemoteController ir_remote_;
    bool lichtblick_ptt_active_=false;
    bool lichtblick_effect_active_=false;
    bool ptt_interaction_active_=false;
    bool lichtblick_ptt_speech_detected_=false;
    bool lichtblick_empty_ptt_pending_=false;
    esp_timer_handle_t lichtblick_empty_ptt_timer_=nullptr;

    // ------------------------------------------------------------------------
    // HMB|TEC IR light control
    // ------------------------------------------------------------------------
    StripColor ir_light_color_={255,80,20};
    int ir_brightness_level_=4;
    bool ir_light_on_=false;
    uint8_t ir_last_command_=0;
    int64_t ir_last_command_time_=0;

    void InitializeDisplayI2c(){
        i2c_master_bus_config_t bus_config={
            .i2c_port=(i2c_port_t)0,
            .sda_io_num=DISPLAY_SDA_PIN,
            .scl_io_num=DISPLAY_SCL_PIN,
            .clk_source=I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt=7,
            .intr_priority=0,
            .trans_queue_depth=0,
            .flags={
                .enable_internal_pullup=1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config,&display_i2c_bus_));
    }

    bool DetectOled(){
        constexpr uint8_t OLED_ADDRESS=0x3C;
        esp_err_t err=i2c_master_probe(display_i2c_bus_,OLED_ADDRESS,100);

        if(err==ESP_OK){
            ESP_LOGI(TAG,"OLED detected at I2C address 0x%02X",OLED_ADDRESS);
            return true;
        }

        ESP_LOGI(TAG,"No OLED detected at I2C address 0x%02X -> headless mode",OLED_ADDRESS);
        return false;
    }

    void InitializeSsd1306Display(){
        esp_lcd_panel_io_i2c_config_t io_config={
            .dev_addr=0x3C,
            .scl_speed_hz=400*1000,
            .control_phase_bytes=1,
            .dc_bit_offset=6,
            .lcd_cmd_bits=8,
            .lcd_param_bits=8,
            .on_color_trans_done=nullptr,
            .user_ctx=nullptr,
            .flags={
                .dc_low_on_data=0,
                .disable_control_phase=0,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_,&io_config,&panel_io_));

        ESP_LOGI(TAG,"Install SSD1306 driver");

        esp_lcd_panel_dev_config_t panel_config={};
        panel_config.reset_gpio_num=GPIO_NUM_NC;
        panel_config.bits_per_pixel=1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config={
            .height=static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config=&ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_,&panel_config,&panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_,&panel_config,&panel_));
#endif

        ESP_LOGI(TAG,"SSD1306 driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if(esp_lcd_panel_init(panel_)!=ESP_OK){
            ESP_LOGE(TAG,"Failed to initialize display");
            display_=new NoDisplay();
            return;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_,false));

        ESP_LOGI(TAG,"Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_,true));

        display_=new OledDisplay(
            panel_io_,
            panel_,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y
        );
    }

    void InitializeButtons(){
        boot_button_.OnClick([this](){
            auto& app=Application::GetInstance();

            if(app.GetDeviceState()==kDeviceStateStarting){
                EnterWifiConfigMode();
                return;
            }

            app.ToggleChatState();
        });

        // ------------------------------------------------------------------------
        // HMB|TEC Main Button
        //
        // SEELSORGE_EN = 0:
        //   Short Press: Lichtblick One-Shot
        //   Long Press:  PTT starten
        //   Release:     PTT beenden
        //
        // SEELSORGE_EN = 1:
        //   Short Press: Seelsorge starten
        //   Long Press:  PTT starten
        //   Release:     PTT beenden
        // ------------------------------------------------------------------------
        lichtblick_button_.OnClick([this](){
        #if SEELSORGE_EN
            ESP_LOGI(TAG,"Lichtblick button -> Seelsorge");
            TriggerSeelsorge();
        #else
            ESP_LOGI(TAG,"Lichtblick button -> Lichtblick");
            TriggerLichtblick();
        #endif
        });

        lichtblick_button_.OnLongPress([this](){
        #if SEELSORGE_EN
            ESP_LOGI(TAG,"Lichtblick long press -> Seelsorge PTT start");
        #else
            ESP_LOGI(TAG,"Lichtblick long press -> PTT start");
            lichtblick_ptt_speech_detected_=false;
            lichtblick_empty_ptt_pending_=false;
            if(lichtblick_empty_ptt_timer_!=nullptr){
                esp_timer_stop(lichtblick_empty_ptt_timer_);
            }
        #endif
            lichtblick_ptt_active_=true;
            ptt_interaction_active_=true;
        #if HMB_PWA_STATS_EN && SEELSORGE_EN
            SendPwaEvent("seelsorger_start");
        #endif
            if(pixel_ring_!=nullptr){
                pixel_ring_->SetAllColor({0,0,0});
            }
            auto& app=Application::GetInstance();
            app.StartListening();
        });

        lichtblick_button_.OnPressUp([this](){
            if(!lichtblick_ptt_active_) return;
        #if SEELSORGE_EN
            ESP_LOGI(TAG,"Lichtblick Seelsorge PTT released -> stop listening");
        #else
            ESP_LOGI(TAG,"Lichtblick PTT released -> stop listening");
            lichtblick_empty_ptt_pending_=true;
        #endif
            lichtblick_ptt_active_=false;
        #if HMB_PWA_STATS_EN && SEELSORGE_EN
            SendPwaEvent("seelsorger_end");
        #endif
            auto& app=Application::GetInstance();
            app.StopListening();

        #if !SEELSORGE_EN
            if(lichtblick_empty_ptt_timer_!=nullptr){
                esp_timer_stop(lichtblick_empty_ptt_timer_);
                esp_err_t err=esp_timer_start_once(lichtblick_empty_ptt_timer_,1200*1000);
                if(err==ESP_OK){
                    ESP_LOGI(TAG,"Empty PTT check armed for 1200 ms");
                }else{
                    ESP_LOGW(TAG,"Empty PTT timer start failed: %s",esp_err_to_name(err));
                    lichtblick_empty_ptt_pending_=false;
                }
            }
        #endif
        });

        volume_up_button_.OnClick([this](){
            auto codec=GetAudioCodec();
            auto volume=codec->output_volume()+10;

            if(volume>100){
                volume=100;
            }

            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME+std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this](){
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this](){
            auto codec=GetAudioCodec();
            auto volume=codec->output_volume()-10;

            if(volume<0){
                volume=0;
            }

            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME+std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this](){
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void ShowClockHour(){
        if(pixel_ring_==nullptr){
            return;
        }

        time_t now=time(nullptr);

        // Vor einer gültigen Serversynchronisation keine falsche Uhrzeit anzeigen.
        if(now<1700000000){
            ESP_LOGW(TAG,"Clock: system time not synchronized yet");
            pixel_ring_->SetAllColor({0,0,0});
            return;
        }

        struct tm timeinfo;
        localtime_r(&now,&timeinfo);

        uint8_t hour=timeinfo.tm_hour%12;

        // Physischer Ring:
        // Pixel 0 = 6 Uhr
        // Zählrichtung = clockwise
        // Daher liegt 12 Uhr auf Pixel 6.
        uint8_t hour_pixel=(hour+6)%12;

        ESP_LOGI(
            TAG,
            "Clock: %02d:%02d -> hour pixel %d",
            timeinfo.tm_hour,
            timeinfo.tm_min,
            hour_pixel
        );

        pixel_ring_->SetAllColor({0,0,0});
        pixel_ring_->SetSingleColor(hour_pixel,{20*3,12*3,4*3});
    }

    void InitializePixelRing(){
        ESP_LOGI(
            TAG,
            "Initializing HMBTEC NeoPixel ring: GPIO%d, %d pixels",
            HMBTEC_PIXEL_RING_GPIO,
            HMBTEC_PIXEL_RING_COUNT
        );

        pixel_ring_=new CircularStrip(
            HMBTEC_PIXEL_RING_GPIO,
            HMBTEC_PIXEL_RING_COUNT
        );

        // HMB|TEC startup ring animation
        pixel_ring_->SetAllColor({0,0,0});

        for(uint8_t i=0;i<HMBTEC_PIXEL_RING_COUNT;i++){
            pixel_ring_->SetAllColor({0,0,0});
            pixel_ring_->SetSingleColor(i,{0,100,0});
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        pixel_ring_->SetAllColor({0,0,0});
    }

#if HMB_PWA_STATS_EN
    static void PwaEventTask(void* parameter){
        char* event=static_cast<char*>(parameter);
        uint8_t mac[6]={0};
        esp_read_mac(mac,ESP_MAC_WIFI_STA);
        char device_id[32];
        snprintf(device_id,sizeof(device_id),"HMB-%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        char json[192];
        snprintf(json,sizeof(json),"{\"device_id\":\"%s\",\"event\":\"%s\"}",device_id,event);
        esp_http_client_config_t config={};
        config.url=HMB_PWA_STATS_URL;
        config.method=HTTP_METHOD_POST;
        config.timeout_ms=5000;
        config.crt_bundle_attach=esp_crt_bundle_attach;
        esp_http_client_handle_t client=esp_http_client_init(&config);
        if(client!=nullptr){
            esp_http_client_set_header(client,"Content-Type","application/json");
            esp_http_client_set_post_field(client,json,strlen(json));
            esp_err_t err=esp_http_client_perform(client);
            if(err==ESP_OK){
                ESP_LOGI(TAG,"PWA event '%s' -> HTTP %d",event,esp_http_client_get_status_code(client));
            }else{
                ESP_LOGW(TAG,"PWA event '%s' failed: %s",event,esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        }
        free(event);
        vTaskDelete(nullptr);
    }

    void SendPwaEvent(const char* event){
        if(event==nullptr || event[0]=='\0') return;
        char* event_copy=strdup(event);
        if(event_copy==nullptr){
            ESP_LOGW(TAG,"PWA event allocation failed");
            return;
        }
        BaseType_t result=xTaskCreate(PwaEventTask,"hmb_pwa_event",6144,event_copy,2,nullptr);
        if(result!=pdPASS){
            ESP_LOGW(TAG,"PWA event task creation failed");
            free(event_copy);
        }
    }
#endif

    void TriggerLichtblick(){
        ESP_LOGI(TAG,"Lichtblick triggered");
#if HMB_PWA_STATS_EN
        SendPwaEvent("lichtblick");
#endif
        auto& app=Application::GetInstance();
        app.WakeWordInvoke("HMBPROMPT",true);
    }

    void TriggerExtendedLichtblick(){
        ESP_LOGI(TAG,"Extended Lichtblick triggered");
#if HMB_PWA_STATS_EN
        SendPwaEvent("lichtblick");
#endif
        auto& app=Application::GetInstance();
        app.WakeWordInvoke("HMBPROMPT_LONG",true);
    }

    void TriggerSeelsorge(){
        ESP_LOGI(TAG,"Seelsorge triggered");

        auto& app=Application::GetInstance();

        // Kein One-Shot:
        // Nach der ersten KI-Antwort bleibt XiaoZhi im Dialogmodus.
        app.WakeWordInvoke("HMBSEELSORGE",false);
    }

    void ApplyIrLight(){
        if(pixel_ring_==nullptr){
            return;
        }

        if(!ir_light_on_ || ir_brightness_level_==0){
            pixel_ring_->SetAllColor({0,0,0});
            return;
        }

        // Level 1...8 -> ca. 12,5...100 %
        const uint16_t scale=ir_brightness_level_;

        StripColor color={
            static_cast<uint8_t>(
                (static_cast<uint16_t>(ir_light_color_.red)*scale)/8
            ),
            static_cast<uint8_t>(
                (static_cast<uint16_t>(ir_light_color_.green)*scale)/8
            ),
            static_cast<uint8_t>(
                (static_cast<uint16_t>(ir_light_color_.blue)*scale)/8
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

    void HandleIrCommand(uint8_t command){
        const int64_t now=esp_timer_get_time();

        // Gleichen IR-Befehl innerhalb von 300 ms nur einmal ausführen.
        if(command==ir_last_command_ && (now-ir_last_command_time_)<300000){
            ESP_LOGI(TAG,"IR duplicate ignored: 0x%02X",command);
            return;
        }

        ir_last_command_=command;
        ir_last_command_time_=now;

        ESP_LOGI(TAG,"Handle IR command: 0x%02X",command);

        if(pixel_ring_==nullptr){
            // RESET muss auch ohne Pixelring funktionieren.
            if(command==0x42){
                ESP_LOGI(TAG,"IR RESET -> Lichtblick");
                TriggerLichtblick();
            }else{
                ESP_LOGW(TAG,"IR light command ignored: pixel ring unavailable");
            }
            return;
        }

        switch(command){
            case 0x47: // OFF
                ESP_LOGI(TAG,"IR OFF");
                ir_light_on_=false;
                ApplyIrLight();
                break;

            case 0x45: // ON
                ESP_LOGI(TAG,"IR ON");

                if(ir_brightness_level_==0){
                    ir_brightness_level_=4;
                }

                ir_light_on_=true;
                ApplyIrLight();
                break;

            case 0x44: // R
                ESP_LOGI(TAG,"IR RED");
                ir_light_color_={255,0,0};
                ir_light_on_=true;
                ApplyIrLight();
                break;

            case 0x40: // G
                ESP_LOGI(TAG,"IR GREEN");
                ir_light_color_={0,255,0};
                ir_light_on_=true;
                ApplyIrLight();
                break;

            case 0x43: // B
                ESP_LOGI(TAG,"IR BLUE");
                ir_light_color_={0,0,255};
                ir_light_on_=true;
                ApplyIrLight();
                break;

            case 0x4A: // UP
                if(ir_brightness_level_<8){
                    ir_brightness_level_++;
                }

                ESP_LOGI(TAG,"IR BRIGHTNESS UP -> level %d",ir_brightness_level_);

                ir_light_on_=true;
                ApplyIrLight();
                break;

            case 0x52: // DOWN
                if(ir_brightness_level_>0){
                    ir_brightness_level_--;
                }

                ESP_LOGI(TAG,"IR BRIGHTNESS DOWN -> level %d",ir_brightness_level_);

                if(ir_brightness_level_==0){
                    ir_light_on_=false;
                }

                ApplyIrLight();
                break;

            case 0x42: // RESET
                ESP_LOGI(TAG,"IR RESET -> Lichtblick");
                TriggerLichtblick();
                break;

            default:
                ESP_LOGI(TAG,"IR command 0x%02X currently not assigned",command);
                break;
        }
    }

#if HMB_FLAME_EN
    // ------------------------------------------------------------------------
    // HMB|TEC Flame control / Flammenmoment
    // ------------------------------------------------------------------------
    void SetAllFlames(bool on){
#ifdef SINGLE_FLAME_EN
        gpio_set_level((gpio_num_t)HMB_FLAME_1,on ? 1 : 0);
        ESP_LOGI(TAG,"SINGLE FLAME -> %s",on ? "ON" : "OFF");
#else
        gpio_set_level((gpio_num_t)HMB_FLAME_1,on ? 1 : 0);
        gpio_set_level((gpio_num_t)HMB_FLAME_2,on ? 1 : 0);
        gpio_set_level((gpio_num_t)HMB_FLAME_3,on ? 1 : 0);
        gpio_set_level((gpio_num_t)HMB_FLAME_4,on ? 1 : 0);
        ESP_LOGI(TAG,"FLAME ALL -> %s",on ? "ON" : "OFF");
#endif
    }
    void SetFlame(uint8_t flame,bool on){
#ifdef SINGLE_FLAME_EN
        if(flame!=1){
            ESP_LOGW(TAG,"SINGLE FLAME invalid number: %d",flame);
            return;
        }
        gpio_set_level((gpio_num_t)HMB_FLAME_1,on ? 1 : 0);
        ESP_LOGI(TAG,"SINGLE FLAME -> %s",on ? "ON" : "OFF");
#else
        gpio_num_t gpio;
        switch(flame){
            case 1: gpio=(gpio_num_t)HMB_FLAME_1; break;
            case 2: gpio=(gpio_num_t)HMB_FLAME_2; break;
            case 3: gpio=(gpio_num_t)HMB_FLAME_3; break;
            case 4: gpio=(gpio_num_t)HMB_FLAME_4; break;
            default:
                ESP_LOGW(TAG,"Invalid FLAME number: %d",flame);
                return;
        }
        gpio_set_level(gpio,on ? 1 : 0);
        ESP_LOGI(TAG,"FLAME %d -> %s",flame,on ? "ON" : "OFF");
#endif
    }
#ifndef SINGLE_FLAME_EN
    void SetFlameMoment(uint8_t count){
        if(count<1 || count>4){
            ESP_LOGW(TAG,"Flammenmoment invalid count: %d",count);
            return;
        }
        SetAllFlames(false);
        uint8_t mask=0;
        while(__builtin_popcount((unsigned int)mask)<count){
            mask|=(1U<<(esp_random()%4));
        }
        for(uint8_t i=0;i<4;i++){
            if(mask&(1U<<i)){
                SetFlame(i+1,true);
            }
        }
        ESP_LOGI(TAG,"Flammenmoment -> count=%d mask=0x%02X",count,mask);
    }
    uint8_t SetRandomFlameMoment(){
        uint8_t count=(uint8_t)((esp_random()%4)+1);
        ESP_LOGI(TAG,"Flammenmoment random selection -> %d",count);
        SetFlameMoment(count);
        return count;
    }
#endif
    void InitializeFlames(){
#ifdef SINGLE_FLAME_EN
        ESP_LOGI(TAG,"Initialize SINGLE FLAME output: GPIO%d",HMB_FLAME_1);
        gpio_config_t flame_cfg={};
        flame_cfg.pin_bit_mask=(1ULL<<HMB_FLAME_1);
#else
        ESP_LOGI(TAG,"Initialize FLAME outputs: GPIO%d, GPIO%d, GPIO%d, GPIO%d",HMB_FLAME_1,HMB_FLAME_2,HMB_FLAME_3,HMB_FLAME_4);
        gpio_config_t flame_cfg={};
        flame_cfg.pin_bit_mask=(1ULL<<HMB_FLAME_1) | (1ULL<<HMB_FLAME_2) | (1ULL<<HMB_FLAME_3) | (1ULL<<HMB_FLAME_4);
#endif
        flame_cfg.mode=GPIO_MODE_OUTPUT;
        flame_cfg.pull_up_en=GPIO_PULLUP_DISABLE;
        flame_cfg.pull_down_en=GPIO_PULLDOWN_DISABLE;
        flame_cfg.intr_type=GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&flame_cfg));
        SetAllFlames(false);
#ifdef SINGLE_FLAME_EN
        SetFlame(1,true);
        vTaskDelay(pdMS_TO_TICKS(200));
#else
        for(uint8_t i=1;i<=4;i++){
            SetFlame(i,true);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
#endif
        SetAllFlames(false);
        ESP_LOGI(TAG,"FLAME startup test finished");
    }
#endif

    void InitializeTools(){
        static BuzzerController buzzer(HMBTEC_BUZZER_GPIO);

#if HMB_FLAME_EN
        InitializeFlames();
#endif

        auto& mcp_server=McpServer::GetInstance();

#if HMB_FLAME_EN
#ifdef SINGLE_FLAME_EN
        // ------------------------------------------------------------------------
        // HMB|TEC Single Flame tools
        // ------------------------------------------------------------------------
        mcp_server.AddTool(
            "self.flame.set",
            "Controls the single physical flame directly. "
            "Use this tool when the user explicitly asks to switch the flame on or off.",
            PropertyList({
                Property("on",kPropertyTypeBoolean)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                bool on=properties["on"].value<bool>();
                SetFlame(1,on);
                return true;
            }
        );
        mcp_server.AddTool(
            "self.flame.moment",
            "Creates an HMBTEC Flammenmoment with the single physical flame. "
            "Use this tool when a Lichtblick should be accompanied by the flame. "
            "There is exactly one physical flame. Never describe or imply multiple flames.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                SetAllFlames(false);
                SetFlame(1,true);
                ESP_LOGI(TAG,"SINGLE FLAME moment -> ON");
                return true;
            }
        );
#else
        // ------------------------------------------------------------------------
        // HMB|TEC Multi Flame tools
        // ------------------------------------------------------------------------
        mcp_server.AddTool(
            "self.flame.set",
            "Controls individual flame outputs directly. "
            "Use this tool when the user explicitly asks to switch flame 1, 2, 3, 4 "
            "or all flames on or off. flame=0 means all flames.",
            PropertyList({
                Property("flame",kPropertyTypeInteger,0,4),
                Property("on",kPropertyTypeBoolean)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int flame=properties["flame"].value<int>();
                bool on=properties["on"].value<bool>();
                if(flame==0){
                    SetAllFlames(on);
                    return true;
                }
                if(flame<1 || flame>4){
                    return false;
                }
                SetFlame((uint8_t)flame,on);
                return true;
            }
        );
        mcp_server.AddTool(
            "self.flame.random_moment",
            "Creates a random HMBTEC Flammenmoment. "
            "Use this tool for a normal Lichtblick when there is no genuine contextual "
            "reason for a specific number of flames. "
            "The device itself randomly chooses between one and four flames. "
            "The returned value is the actual number of illuminated flames.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                uint8_t selected=SetRandomFlameMoment();
                ESP_LOGI(TAG,"Flammenmoment RANDOM TOOL result=%d",selected);
                return std::to_string(selected);
            }
        );
        mcp_server.AddTool(
            "self.flame.moment",
            "Creates an HMBTEC Flammenmoment. "
            "Use count 1, 2, 3 or 4 only if there is a genuine contextual reason "
            "for exactly that number. "
            "If there is no genuine reason for a particular number, always use count 0. "
            "With count 0 the device randomly chooses one to four flames. "
            "Never invent a factual reason for choosing a specific number.",
            PropertyList({
                Property("count",kPropertyTypeInteger,0,4)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int count=properties["count"].value<int>();
                ESP_LOGI(TAG,"Flammenmoment MCP received count=%d",count);
                if(count<0 || count>4){
                    ESP_LOGW(TAG,"Flammenmoment rejected: count=%d",count);
                    return false;
                }
                if(count==0){
                    uint8_t selected=SetRandomFlameMoment();
                    ESP_LOGI(TAG,"Flammenmoment MCP random result=%d",selected);
                    return std::to_string(selected);
                }
                SetFlameMoment((uint8_t)count);
                ESP_LOGI(TAG,"Flammenmoment MCP contextual result=%d",count);
                return std::to_string(count);
            }
        );
#endif
#endif

        // ------------------------------------------------------------------------
        // HMB|TEC Lichtblick prompt
        // ------------------------------------------------------------------------
        mcp_server.AddTool(
            "self.hmbtec.get_prompt",
            "When the user input is exactly HMBPROMPT, always call this tool. "
            "The returned text contains an instruction that must be executed. "
            "Do not mention HMBPROMPT or this tool to the user. "
            "Follow the returned instruction and answer directly.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI("HmbtecPrompt","AI requested firmware prompt");

                std::string prompt=
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
                    "Falls kein Licht-Tool verfuegbar ist, fahre ohne Lichtaktion fort. ";

#if HMB_FLAME_EN
#ifdef SINGLE_FLAME_EN
                prompt+=
                    "Begleite den Lichtblick ausserdem mit einem Flammenmoment. "
                    "Es existiert genau eine physische Flamme. "
                    "Rufe dafuer self.flame.moment auf. "
                    "Die Flamme ist ein ruhiges symbolisches Licht und keine Anzahl oder Auswahl. "
                    "Sprich daher immer nur von einer Flamme oder einem kleinen Licht. "
                    "Erwaehne niemals mehrere Flammen und erfinde keine Anzahl. "
                    "Beziehe die einzelne Flamme nur dann sprachlich ein, wenn es natuerlich zum Lichtblick passt. ";
#else
                prompt+=
                    "Erzeuge ausserdem einen Flammenmoment. "
                    "Pruefe zuerst, ob es einen echten konkreten Grund fuer genau eine, zwei, drei oder vier Flammen gibt. "
                    "Ein solcher Grund darf nur aus einem tatsaechlichen Datum, einem realen Kalendereignis "
                    "oder einem eindeutigen Bezug aus dem aktuellen Gespraech entstehen. "
                    "Wenn ein solcher echter Grund besteht, rufe self.flame.moment mit count 1, 2, 3 oder 4 auf. "
                    "Wenn kein solcher eindeutiger Zahlenbezug besteht, rufe self.flame.random_moment auf. "
                    "Bei self.flame.random_moment darfst du keine Anzahl selbst bestimmen. "
                    "Das Geraet waehlt dann zufaellig zwischen einer und vier Flammen. "
                    "Das Tool gibt die tatsaechlich gewaehlte Anzahl zurueck. "
                    "Beziehe genau diese zurueckgegebene Anzahl kurz und natuerlich in den gesprochenen Lichtblick ein. "
                    "Erfinde niemals einen Fakt oder Zusammenhang, um self.flame.moment statt self.flame.random_moment zu verwenden. ";
#endif
#endif
                prompt+=
                    "Sprich anschliessend den Lichtblick direkt aus.";

                return prompt;
            }
        );

        // ------------------------------------------------------------------------
        // HMB|TEC Extended Lichtblick prompt
        // ------------------------------------------------------------------------
        mcp_server.AddTool(
            "self.hmbtec.get_extended_prompt",
            "When the user input is exactly HMBPROMPT_LONG, always call this tool. "
            "The returned text contains an instruction that must be executed. "
            "Do not mention HMBPROMPT_LONG or this tool to the user. "
            "Follow the returned instruction and answer directly.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI("HmbtecPrompt","AI requested extended firmware prompt");
                std::string prompt=
                    "Erzeuge jetzt einen etwas laengeren persoenlichen Lichtblick fuer den Nutzer. "
                    "Formuliere einen ruhigen, positiven und unterstuetzenden Gedanken mit etwas mehr Tiefe als beim normalen Lichtblick. "
                    "Antworte auf Deutsch und stelle keine Rueckfrage. "
                    "Formuliere etwa vier bis sechs natuerliche Saetze. "
                    "Nutze bekannten Gespraechskontext, sofern er sinnvoll passt, und beruecksichtige nach Moeglichkeit Tageszeit und Jahreszeit. "
                    "Vermeide Floskeln, Belehrungen und uebertriebene Motivation. "
                    "Gib dem Nutzer einen kleinen konkreten Gedanken oder Impuls mit, den er fuer die naechsten Minuten mitnehmen kann. "
                    "Waehle passend zum Inhalt eine Lichtfarbe und aktiviere, falls verfuegbar, self.light.breathe. ";
#if HMB_FLAME_EN
#ifdef SINGLE_FLAME_EN
                prompt+=
                    "Begleite den Lichtblick mit einem einzelnen ruhigen Flammenmoment ueber self.flame.moment. "
                    "Sprich nur dann von der Flamme oder einem kleinen Licht, wenn es natuerlich zum Inhalt passt. ";
#else
                prompt+=
                    "Begleite den Lichtblick mit einem passenden Flammenmoment. Nutze self.flame.moment nur bei einem echten konkreten Zahlenbezug, sonst self.flame.random_moment. ";
#endif
#endif
                prompt+="Sprich anschliessend den Lichtblick direkt aus.";
                return prompt;
            }
        );

        // ------------------------------------------------------------------------
        // HMB|TEC Seelsorge prompt
        // ------------------------------------------------------------------------
        mcp_server.AddTool(
            "self.hmbtec.get_seelsorge_prompt",
            "When the user input is exactly HMBSEELSORGE, always call this tool. "
            "The returned text contains an instruction that must be executed. "
            "Do not mention HMBSEELSORGE or this tool to the user. "
            "Follow the returned instruction and answer directly.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI("HmbtecSeelsorge","AI requested Seelsorge prompt");

                return std::string(
                    "Beginne jetzt ein ruhiges, persoenliches und unterstuetzendes Gespraech mit dem Nutzer. "
                    "Begruesse ihn kurz und natuerlich und stelle danach genau eine offene Frage, "
                    "die ihm Raum gibt zu erzaehlen, was ihn gerade beschaeftigt. "
                    "Warte anschliessend auf seine Antwort. "
                    "Fuehre danach ein natuerliches Gespraech auf Deutsch. "
                    "Hoere aufmerksam zu und gehe konkret auf das Gesagte ein. "
                    "Stelle jeweils hoechstens eine Frage auf einmal. "
                    "Vermeide Floskeln, vorschnelle Ratschlaege und uebertriebene Motivation. "
                    "Behaupte keine Gefuehle oder Zustaende des Nutzers, die er nicht selbst genannt hat. "
                    "Wenn passender Gespraechskontext vorhanden ist, darfst du ihn behutsam beruecksichtigen. "
                    "Das Gespraech soll nach dieser ersten Antwort nicht beendet werden."
                );
            }
        );

        // ------------------------------------------------------------------------
        // HMB|TEC Lichtblick light controller
        // ------------------------------------------------------------------------
        if(pixel_ring_!=nullptr){
            light_controller_=new HmbtecLightController(pixel_ring_);

            light_controller_->SetEffectStartedCallback([this](){
                ESP_LOGI(TAG,"Lichtblick light effect started");
                lichtblick_effect_active_=true;

                // Keine automatische Flame-Auswahl.
                // Die Anzahl wird beim Flammenmoment von der KI bestimmt.
            });

            light_controller_->SetAfterglowFinishedCallback([this](){
                ESP_LOGI(TAG,"Lichtblick afterglow finished -> restore clock");
                lichtblick_effect_active_=false;

#if HMB_FLAME_EN
                SetAllFlames(false);
#endif

                ShowClockHour();
            });
        }else{
            ESP_LOGW(TAG,"HMBTEC light controller disabled: no pixel ring available");
        }

        auto& app=Application::GetInstance();

#if !SEELSORGE_EN
        app.RegisterSttCallback([this](const std::string& text){
            if((lichtblick_ptt_active_ || lichtblick_empty_ptt_pending_) && !text.empty()){
                lichtblick_ptt_speech_detected_=true;
                lichtblick_empty_ptt_pending_=false;
                if(lichtblick_empty_ptt_timer_!=nullptr){
                    esp_timer_stop(lichtblick_empty_ptt_timer_);
                }
                ESP_LOGI(TAG,"PTT speech detected -> extended Lichtblick cancelled");
            }
        });
#endif

        app.RegisterStateChangeListener([this](DeviceState old_state,DeviceState new_state){
            ESP_LOGI(
                TAG,
                "HMBTEC state change: %d -> %d",
                static_cast<int>(old_state),
                static_cast<int>(new_state)
            );

            if(new_state==kDeviceStateIdle && !lichtblick_effect_active_){
                if(ptt_interaction_active_){
                    ESP_LOGI(TAG,"PTT interaction finished -> restore clock");
                    ptt_interaction_active_=false;
                }else{
                    ESP_LOGI(TAG,"Idle -> show clock");
                }

                ShowClockHour();
            }
        });

        app.RegisterOneShotFinishedCallback([this](){
            if(light_controller_!=nullptr){
                ESP_LOGI(TAG,"Lichtblick one-shot finished -> start 60s afterglow");

                // Prevent Idle state from overwriting the breathing animation
                // with the clock display.
                lichtblick_effect_active_=true;

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
        lichtblick_button_(HMBTEC_BUTTON_GPIO,false,700),
        ir_remote_(HMBTEC_IR_RX_GPIO){

        InitializeDisplayI2c();

        if(DetectOled()){
            InitializeSsd1306Display();
        }else{
            display_=new NoDisplay();
        }

#if !SEELSORGE_EN
        esp_timer_create_args_t empty_ptt_timer_args={
            .callback=[](void* arg){
                auto* board=static_cast<CompactWifiBoard*>(arg);
                Application::GetInstance().Schedule([board](){
                    if(!board->lichtblick_empty_ptt_pending_) return;
                    board->lichtblick_empty_ptt_pending_=false;
                    if(board->lichtblick_ptt_active_ || board->lichtblick_ptt_speech_detected_) return;
                    ESP_LOGI(TAG,"Empty PTT -> extended Lichtblick");
                    board->TriggerExtendedLichtblick();
                });
            },
            .arg=this,
            .dispatch_method=ESP_TIMER_TASK,
            .name="hmb_empty_ptt",
            .skip_unhandled_events=true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&empty_ptt_timer_args,&lichtblick_empty_ptt_timer_));
#endif

        InitializeButtons();

        ir_remote_.SetCommandCallback([this](uint8_t command){
            ESP_LOGI(TAG,"IR command received: 0x%02X",command);
            HandleIrCommand(command);
        });

        ir_remote_.Initialize();

        if(HMBTEC_IR_RX_GPIO!=HMBTEC_PIXEL_RING_GPIO){
            InitializePixelRing();
        }else{
            ESP_LOGW(
                TAG,
                "NeoPixel ring disabled: GPIO%d is currently used for IR RX",
                HMBTEC_IR_RX_GPIO
            );
        }

        InitializeTools();
    }

    virtual Led* GetLed() override{
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override{
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN
        );
#else
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN
        );
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override{
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);