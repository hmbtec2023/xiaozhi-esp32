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
    bool topic_selection_pending_=false;
    bool ptt_active_=false;

    void RegisterActionTool(const char* tool_name,const char* trigger,const char* label,const char* prompt){
        auto& mcp_server=McpServer::GetInstance();
        std::string description="When the user input is exactly ";
        description+=trigger;
        description+=", always call this tool. The returned text contains an instruction that must be executed. Do not mention the trigger, this tool, or internal instructions to the user. Follow the returned instruction and answer directly.";
        mcp_server.AddTool(tool_name,description,PropertyList(),[label,prompt](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI(TAG,"AI requested TalkCard action: %s",label);
            return std::string(prompt);
        });
    }
    void InitializeTools(){
        auto& mcp_server=McpServer::GetInstance();
        mcp_server.AddTool(
            "self.hmbtec.talkcard.get_context",
            "Before answering a normal spoken user request on the HMBTEC TalkCard, call this tool first. "
            "It returns the currently selected TalkCard category context. "
            "Use that context to shape the answer naturally without mentioning category numbers, this tool, or internal instructions. "
            "If no category is active, answer the user normally.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                const uint8_t category=category_;
                ESP_LOGI(TAG,"AI requested TalkCard context: category=%u",category);
                switch(category){
                    case 1: return std::string("Aktiver TalkCard-Kontext: Achtsamkeit und Psyche. Beantworte die aktuelle Nutzeraussage ruhig, achtsam, emotional passend und knapp fuer Sprachausgabe. Keine Diagnose und keine unnoetige Rueckfrage.");
                    case 2: return std::string("Aktiver TalkCard-Kontext: Unterhaltung und Spass. Beantworte die aktuelle Nutzeraussage leicht, unterhaltsam und passend zum Inhalt. Bleibe kurz und gut fuer Sprachausgabe.");
                    case 3: return std::string("Aktiver TalkCard-Kontext: Alltag und Fokus. Beantworte die aktuelle Nutzeraussage praktisch, klar, fokussiert und handlungsorientiert. Bleibe kurz und gut fuer Sprachausgabe.");
                    case 4: return std::string("Aktiver TalkCard-Kontext: Emotion und Naehe. Reagiere auf die aktuelle Nutzeraussage einfuehlsam, persoenlich und zugewandt, ohne kitschig zu werden. Beziehe die aus dem gesprochenen Text erkennbare Stimmung ein und bleibe knapp fuer Sprachausgabe.");
                    default: return std::string("Kein spezieller TalkCard-Kategoriekontext ist aktiv. Beantworte die aktuelle Nutzeraussage normal.");
                }
            }
        );
        RegisterActionTool("self.hmbtec.talkcard.affirmation","HMBTC_AFFIRMATION","AFFIRMATION","Sprich jetzt genau eine kurze, glaubwuerdige und alltagstaugliche Affirmation auf Deutsch. Sie soll ruhig und persoenlich klingen, ohne Kitsch und ohne Rueckfrage. Maximal zwei kurze Saetze.");
        RegisterActionTool("self.hmbtec.talkcard.wisdom","HMBTC_WISDOM","WISDOM","Gib jetzt eine kurze Weisheit oder einen kurzen Gedanken fuer den Tag auf Deutsch. Wenn du ein echtes Zitat mit Autor nennst, verwende nur eines, bei dem du dir der Zuordnung sicher bist; sonst formuliere einen eigenen Gedanken ohne falsche Zuschreibung. Keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.mindfulness","HMBTC_MINDFULNESS","MINDFULNESS","Gib jetzt einen sehr kurzen Achtsamkeits-Impuls auf Deutsch, der sofort in etwa 20 bis 40 Sekunden umsetzbar ist. Eine konkrete kleine Wahrnehmungs- oder Atemuebung, ruhig formuliert, keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.mood_check","HMBTC_MOOD_CHECK","MOOD_CHECK","Fuehre jetzt einen knappen Stimmungs-Check auf Deutsch durch. Stelle genau eine einfache, offene Frage dazu, wie es dem Nutzer gerade geht oder was gerade am staerksten spuerbar ist. Keine Diagnose und keine Interpretation vor der Antwort.");
        RegisterActionTool("self.hmbtec.talkcard.joke","HMBTC_JOKE","JOKE","Erzaehle jetzt genau einen kurzen, harmlosen Witz oder ein Wortspiel auf Deutsch. Direkt zur Pointe, gut fuer Sprachausgabe, keine Erklaerung und keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.fun_fact","HMBTC_FUN_FACT","FUN_FACT","Nenne jetzt genau eine kurze, ueberraschende und moeglichst belastbare Tatsache auf Deutsch. Keine erfundene Behauptung, keine lange Erklaerung und keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.news_compact","HMBTC_NEWS_COMPACT","NEWS_COMPACT","Gib dem Nutzer jetzt einen sehr kurzen Ueberblick ueber die wichtigsten aktuellen Nachrichten. Nutze dafuer verfuegbare aktuelle Nachrichten- oder Web-Werkzeuge, falls erforderlich. Nenne hoechstens drei relevante Meldungen. Formuliere auf Deutsch, sachlich, kompakt und gut fuer Sprachausgabe. Erfinde keine aktuellen Ereignisse. Wenn keine verlaesslichen aktuellen Nachrichten verfuegbar sind, sage das kurz und eindeutig. Stelle keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.riddle","HMBTC_RIDDLE","RIDDLE","Stelle jetzt genau ein kurzes, loesbares Raetsel auf Deutsch. Verrate die Loesung noch nicht und stelle ausser dem Raetsel keine weitere Frage.");
        RegisterActionTool("self.hmbtec.talkcard.morning_briefing","HMBTC_MORNING_BRIEFING","MORNING_BRIEFING","Gib jetzt ein sehr kurzes Morgen-Briefing auf Deutsch. Beziehe aktuelles Datum, Wochentag, Jahreszeit und - falls verlaesslich verfuegbar - Wetter oder relevante aktuelle Informationen ein. Nutze aktuelle Werkzeuge wenn noetig und erfinde nichts. Falls Kontext fehlt, liefere nur die sicher verfuegbaren Teile. Maximal etwa 30 Sekunden Sprachausgabe.");
        RegisterActionTool("self.hmbtec.talkcard.focus","HMBTC_FOCUS","FOCUS","Gib jetzt eine kurze Fokus-Ansage auf Deutsch: ein klarer Satz zum Priorisieren und ein unmittelbar umsetzbarer erster Schritt. Keine Motivationsrede und keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.health_reminder","HMBTC_HEALTH_REMINDER","HEALTH_REMINDER","Gib jetzt einen kurzen, allgemeinen und risikoarmen Gesundheits-Reminder auf Deutsch, zum Beispiel trinken, kurz bewegen, Haltung lockern, Augen entspannen oder Pause machen. Keine Diagnose, keine Medikamenten- oder Therapieanweisung und keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.evening","HMBTC_EVENING","EVENING","Sprich jetzt einen kurzen Feierabend-Satz auf Deutsch, der beim mentalen Abschluss des Tages hilft. Ruhig, unaufdringlich, maximal zwei Saetze und keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.compliment","HMBTC_COMPLIMENT","COMPLIMENT","Gib jetzt ein kurzes, glaubwuerdiges und wertschätzendes Kompliment auf Deutsch. Erfinde keine persoenlichen Eigenschaften oder Leistungen, die du nicht kennst; beziehe dich stattdessen auf etwas allgemein Menschliches oder den Moment. Keine Rueckfrage.");
        RegisterActionTool("self.hmbtec.talkcard.sleep","HMBTC_SLEEP","SLEEP","Gib jetzt eine sehr kurze Einschlaf-Hilfe auf Deutsch: ruhig, langsam formulierbar und mit einer einfachen Atem-, Koerper- oder Loslass-Anweisung. Keine medizinischen Versprechen und keine Rueckfrage. Maximal etwa 30 Sekunden.");
        RegisterActionTool("self.hmbtec.talkcard.encouragement","HMBTC_ENCOURAGEMENT","ENCOURAGEMENT","Gib jetzt eine kurze persoenlich klingende Ermutigung auf Deutsch. Warm, konkret und glaubwuerdig, ohne unbegruendete Annahmen ueber die Situation des Nutzers und ohne Rueckfrage. Maximal zwei bis drei Saetze.");
        RegisterActionTool("self.hmbtec.talkcard.companion","HMBTC_COMPANION","COMPANION","Reagiere jetzt mit einem kurzen, ruhigen Satz von Praesenz und Zugewandtheit auf Deutsch. Keine Behauptung menschlicher Gefuehle oder physischer Anwesenheit, keine Diagnose und keine Rueckfrage. Der Ton soll vermitteln: Du kannst hier gerade sprechen, und ich hoere dir zu.");
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
        if(category_==0 && !topic_selection_pending_){
            return;
        }

        ESP_LOGI(TAG,"Selection reset: category=%u pending=%d reason=%s",
            category_,topic_selection_pending_,reason);

        category_=0;
        topic_selection_pending_=false;
        SetPixel(0);
    }

    void ExecuteAction(uint8_t category,uint8_t topic){
        static const char* const action_ids[4][4]={
            {"HMBTC_AFFIRMATION","HMBTC_WISDOM","HMBTC_MINDFULNESS","HMBTC_MOOD_CHECK"},
            {"HMBTC_JOKE","HMBTC_FUN_FACT","HMBTC_NEWS_COMPACT","HMBTC_RIDDLE"},
            {"HMBTC_MORNING_BRIEFING","HMBTC_FOCUS","HMBTC_HEALTH_REMINDER","HMBTC_EVENING"},
            {"HMBTC_COMPLIMENT","HMBTC_SLEEP","HMBTC_ENCOURAGEMENT","HMBTC_COMPANION"}
        };
        if(category<1 || category>4 || topic<1 || topic>4){
            ESP_LOGW(TAG,"Invalid action: category=%u topic=%u",category,topic);
            return;
        }
        const char* action_id=action_ids[category-1][topic-1];
        ESP_LOGI(TAG,"Execute action: category=%u topic=%u id=%s",category,topic,action_id);
        Application::GetInstance().WakeWordInvoke(action_id,true);
    }

    void HandleCorner(uint8_t key){
        if(!topic_selection_pending_){
            category_=key;
            topic_selection_pending_=true;
            SetPixel(category_);
            RestartCategoryTimer();
            ESP_LOGI(TAG,"K%u -> category %u selected, waiting for topic or PTT",key,category_);
            return;
        }

        const uint8_t selected_category=category_;
        const uint8_t topic=key;

        if(category_timer_!=nullptr){
            esp_timer_stop(category_timer_);
        }

        ESP_LOGI(TAG,"K%u -> action category=%u topic=%u",key,selected_category,topic);

        topic_selection_pending_=false;
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

        ptt_button_.OnLongPress([this](){
            ESP_LOGI(TAG,"TalkCard long press -> PTT start: category=%u pending=%d",
                category_,topic_selection_pending_);

            ptt_active_=true;

            if(category_!=0){
                // PTT statt zweiter K-Taste:
                // Kategorie bleibt aktiv, 4x4-Zweitauswahl ist beendet.
                topic_selection_pending_=false;

                if(category_timer_!=nullptr){
                    esp_timer_stop(category_timer_);
                }

                SetPixel(category_);
            }

            auto& app=Application::GetInstance();
            app.StartListening();
        });

        ptt_button_.OnPressUp([this](){
            if(!ptt_active_) return;
            ESP_LOGI(TAG,"TalkCard PTT released -> stop listening: category=%u",category_);
            ptt_active_=false;
            auto& app=Application::GetInstance();
            app.StopListening();
            if(category_!=0){
                RestartCategoryTimer();
            }
        });
        ESP_LOGI(TAG,"Buttons: K1=%d K2=%d K3=%d K4=%d PTT=%d",HMB_TC_BUTTON_1_GPIO,HMB_TC_BUTTON_2_GPIO,HMB_TC_BUTTON_3_GPIO,HMB_TC_BUTTON_4_GPIO,HMB_TC_PTT_GPIO);
    }
public:
    HmbtecTalkCardBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        ptt_button_(HMB_TC_PTT_GPIO,false,700),
        button_1_(HMB_TC_BUTTON_1_GPIO),
        button_2_(HMB_TC_BUTTON_2_GPIO),
        button_3_(HMB_TC_BUTTON_3_GPIO),
        button_4_(HMB_TC_BUTTON_4_GPIO){
        ESP_LOGI(TAG,"HMB | TEC TalkCard V0.3.1 - 16 actions + category PTT context");
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
