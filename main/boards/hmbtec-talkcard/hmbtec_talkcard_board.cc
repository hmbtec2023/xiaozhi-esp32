#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "led/circular_strip.h"
#include <esp_log.h>
#include <esp_timer.h>
#include "mcp_server.h"

#define TAG "HmbTalkCard"

class HmbtecTalkCardBoard : public WifiBoard {
private:
    NoDisplay display_;
    Button boot_button_;
    Button ptt_button_;
    Button button_1_;
    Button button_2_;
    Button button_3_;
    Button button_4_;
    CircularStrip* pixel_=nullptr;
    esp_timer_handle_t category_timer_=nullptr;
    uint8_t category_=0;
    bool ptt_active_=false;

    void InitializeTools(){
        auto& mcp_server=McpServer::GetInstance();

        mcp_server.AddTool(
            "self.hmbtec.talkcard.news_compact",
            "When the user input is exactly HMBTC_NEWS_COMPACT, always call this tool. "
            "The returned text contains an instruction that must be executed. "
            "Do not mention HMBTC_NEWS_COMPACT or this tool to the user. "
            "Follow the returned instruction and answer directly.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI("HmbTalkCard","AI requested NEWS_COMPACT prompt");

                return std::string(
                    "Gib dem Nutzer jetzt einen sehr kurzen Ueberblick ueber die wichtigsten aktuellen Nachrichten. "
                    "Nutze dafuer die verfuegbaren aktuellen Nachrichten- oder Web-Werkzeuge, falls erforderlich. "
                    "Nenne hoechstens drei relevante Meldungen. "
                    "Formuliere auf Deutsch, sachlich, kompakt und gut fuer Sprachausgabe. "
                    "Erfinde keine aktuellen Ereignisse. "
                    "Wenn keine verlaesslichen aktuellen Nachrichten verfuegbar sind, sage das kurz und eindeutig. "
                    "Stelle keine Rueckfrage und sprich die Zusammenfassung direkt aus."
                );
            }
        );
    }

    static void CategoryTimerCallback(void* arg){
        auto* self=static_cast<HmbtecTalkCardBoard*>(arg);
        self->ResetSelection("timeout");
    }
    void SetPixel(uint8_t category){
        if(pixel_==nullptr){
            return;
        }
        switch(category){
            case 1: pixel_->SetAllColor({0,90,20}); break;      // green
            case 2: pixel_->SetAllColor({120,45,0}); break;     // orange
            case 3: pixel_->SetAllColor({0,35,120}); break;     // blue
            case 4: pixel_->SetAllColor({110,0,65}); break;     // pink
            default: pixel_->SetAllColor({0,0,0}); break;
        }
    }
    void RestartCategoryTimer(){
        if(category_timer_==nullptr){
            return;
        }
        esp_timer_stop(category_timer_);
        esp_timer_start_once(category_timer_,static_cast<uint64_t>(HMB_TC_TIMEOUT_MS)*1000ULL);
    }
    void ResetSelection(const char* reason){
        if(category_==0){
            return;
        }
        ESP_LOGI(TAG,"Selection reset: category=%u reason=%s",category_,reason);
        category_=0;
        SetPixel(0);
    }

    void ExecuteAction(uint8_t category,uint8_t topic){
        ESP_LOGI(TAG,"Execute action: category=%u topic=%u",category,topic);

        auto& app=Application::GetInstance();

        if(category==2 && topic==3){
            ESP_LOGI(TAG,"Action 2/3: NEWS_COMPACT");
            app.WakeWordInvoke("HMBTC_NEWS_COMPACT",true);
            return;
        }

        ESP_LOGW(TAG,"Action not implemented: category=%u topic=%u",category,topic);
    }

    void HandleCorner(uint8_t key){
        if(category_==0){
            category_=key;
            SetPixel(category_);
            RestartCategoryTimer();
            ESP_LOGI(TAG,"K%u -> category %u selected",key,category_);
            return;
        }

        const uint8_t selected_category=category_;
        const uint8_t topic=key;

        if(category_timer_!=nullptr){
            esp_timer_stop(category_timer_);
        }

        ESP_LOGI(TAG,"K%u -> action category=%u topic=%u",key,selected_category,topic);

        category_=0;
        SetPixel(0);

        ExecuteAction(selected_category,topic);
    }

    void InitializePixel(){
        ESP_LOGI(TAG,"RGB pixel: GPIO%d count=%d",HMB_TC_PIXEL_GPIO,HMB_TC_PIXEL_COUNT);
        pixel_=new CircularStrip(HMB_TC_PIXEL_GPIO,HMB_TC_PIXEL_COUNT);
        pixel_->SetAllColor({0,0,0});
    }
    
    void InitializeCategoryTimer(){
        esp_timer_create_args_t args={};
        args.callback=&CategoryTimerCallback;
        args.arg=this;
        args.dispatch_method=ESP_TIMER_TASK;
        args.name="tc_category";
        ESP_ERROR_CHECK(esp_timer_create(&args,&category_timer_));
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
        button_1_.OnClick([this](){ HandleCorner(1); });
        button_2_.OnClick([this](){ HandleCorner(2); });
        button_3_.OnClick([this](){ HandleCorner(3); });
        button_4_.OnClick([this](){ HandleCorner(4); });
        ptt_button_.OnPressDown([this](){
            if(ptt_active_){
                return;
            }
            ptt_active_=true;
            ESP_LOGI(TAG,"PTT down: category=%u",category_);
            Application::GetInstance().StartListening();
        });
        ptt_button_.OnPressUp([this](){
            if(!ptt_active_){
                return;
            }
            ptt_active_=false;
            ESP_LOGI(TAG,"PTT up: category=%u",category_);
            Application::GetInstance().StopListening();
            if(category_!=0){
                RestartCategoryTimer();
            }
        });
        ESP_LOGI(TAG,"Buttons: K1=%d K2=%d K3=%d K4=%d PTT=%d",HMB_TC_BUTTON_1_GPIO,HMB_TC_BUTTON_2_GPIO,HMB_TC_BUTTON_3_GPIO,HMB_TC_BUTTON_4_GPIO,HMB_TC_PTT_GPIO);
    }
public:
    HmbtecTalkCardBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        ptt_button_(HMB_TC_PTT_GPIO),
        button_1_(HMB_TC_BUTTON_1_GPIO),
        button_2_(HMB_TC_BUTTON_2_GPIO),
        button_3_(HMB_TC_BUTTON_3_GPIO),
        button_4_(HMB_TC_BUTTON_4_GPIO){
        ESP_LOGI(TAG,"HMB | TEC TalkCard V0.2.0 - XiaoZhi actions");
        InitializePixel();
        InitializeCategoryTimer();
        InitializeButtons();
        InitializeTools();
    }
    virtual Led* GetLed() override{
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }
    virtual AudioCodec* GetAudioCodec() override{
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
        return &audio_codec;
    }
    virtual Display* GetDisplay() override{
        return &display_;
    }
};

DECLARE_BOARD(HmbtecTalkCardBoard);
