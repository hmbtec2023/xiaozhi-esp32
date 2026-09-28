#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "hmb_dualeye.h"
#include "mcp_server.h"

#include <esp_log.h>

#define TAG "HmbDualEyeBoard"

class HmbDualEyeBoard : public WifiBoard {
private:
    NoDisplay display_;

    Button boot_button_;
    Button lichtblick_button_;

    bool lichtblick_ptt_active_ = false;

#if HMB_DUALEYE_ENABLED
    HmbDualEye eyes_;
#endif

    // ------------------------------------------------------------------------
    // Buttons
    // ------------------------------------------------------------------------

    void InitializeButtons(){
        // --------------------------------------------------------------------
        // XiaoZhi Boot Button
        // --------------------------------------------------------------------

        boot_button_.OnClick([this](){
            auto& app = Application::GetInstance();

            if(app.GetDeviceState() == kDeviceStateStarting){
                EnterWifiConfigMode();
                return;
            }

            app.ToggleChatState();
        });

        // --------------------------------------------------------------------
        // HMB | TEC Lichtblick Button
        //
        // GPIO44 / Board-Label RX
        //
        // Short Press:
        //   Lichtblick One-Shot über HMBPROMPT
        //
        // Long Press >= 700 ms:
        //   Push-to-Talk starten
        //
        // Release nach Long Press:
        //   Push-to-Talk beenden
        // --------------------------------------------------------------------

        lichtblick_button_.OnClick([this](){
#if SEELSORGE_EN
            ESP_LOGI(TAG, "Lichtblick button -> Seelsorge");
            TriggerSeelsorge();
#else
            ESP_LOGI(TAG, "Lichtblick button -> Lichtblick");
            TriggerLichtblick();
#endif
        });

        lichtblick_button_.OnLongPress([this](){
            ESP_LOGI(TAG, "Lichtblick long press -> PTT start");

            lichtblick_ptt_active_ = true;

            auto& app = Application::GetInstance();
            app.StartListening();
        });

        lichtblick_button_.OnPressUp([this](){
            if(!lichtblick_ptt_active_){
                return;
            }

            ESP_LOGI(TAG, "Lichtblick PTT released -> stop listening");

            lichtblick_ptt_active_ = false;

            auto& app = Application::GetInstance();
            app.StopListening();
        });
    }

    void TriggerLichtblick(){
        ESP_LOGI(TAG, "Lichtblick triggered");
        auto& app = Application::GetInstance();
        app.WakeWordInvoke("HMBPROMPT", true);
    }

    void TriggerSeelsorge(){
        ESP_LOGI(TAG, "Seelsorge triggered");
        auto& app = Application::GetInstance();
        // Kein One-Shot: Nach der ersten KI-Antwort bleibt XiaoZhi im Dialogmodus.
        app.WakeWordInvoke("HMBSEELSORGE", false);
    }

    // ------------------------------------------------------------------------
    // HMB | TEC MCP Tools
    // ------------------------------------------------------------------------

    void InitializeTools(){
        auto& mcp_server = McpServer::GetInstance();

        // --------------------------------------------------------------------
        // Lichtblick One-Shot Prompt
        //
        // Wird ausgelöst, wenn die Firmware über WakeWordInvoke()
        // den internen Trigger "HMBPROMPT" an XiaoZhi übergibt.
        // --------------------------------------------------------------------

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
                    "Sprich anschliessend den Lichtblick direkt aus."
                );
            }
        );


#if SEELSORGE_EN
        mcp_server.AddTool(
            "self.hmbtec.get_seelsorge_prompt",
            "When the user input is exactly HMBSEELSORGE, always call this tool. "
            "The returned text contains an instruction that must be executed. "
            "Do not mention HMBSEELSORGE or this tool to the user. "
            "Follow the returned instruction and answer directly.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ESP_LOGI("HmbtecSeelsorge", "AI requested Seelsorge prompt");
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
#endif

#if HMB_DUALEYE_ENABLED
        // --------------------------------------------------------------------
        // HMB | TEC AI Eye Expression
        //
        // Die KI setzt nur einen semantischen Ausdruck.
        // Rendering, Bewegung, Blinzeln und Timeout laufen lokal im ESP32.
        // --------------------------------------------------------------------

        mcp_server.AddTool(
            "self.hmbtec.eyes.set_expression",
            "Set a temporary facial expression for the physical HMBTEC eyes. "
            "Use this when a visible nonverbal reaction fits the conversation. "
            "Allowed expression values: neutral, happy, relaxed, curious, thinking, surprised, sad, confused, sleepy. "
            "Use subtle expressions and do not call the tool for every sentence. "
            "duration_ms controls how long the expression remains active from 500 to 15000 milliseconds. "
            "After the timeout the eyes automatically return to neutral behavior.",
            PropertyList({
                Property("expression", kPropertyTypeString),
                Property("duration_ms", kPropertyTypeInteger, 500, 15000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto expression=properties["expression"].value<std::string>();
                int duration_ms=properties["duration_ms"].value<int>();

                ESP_LOGI(
                    TAG,
                    "AI eye expression -> %s (%d ms)",
                    expression.c_str(),
                    duration_ms
                );

                return eyes_.SetExpression(expression,duration_ms);
            }
        );

        mcp_server.AddTool(
            "self.hmbtec.eyes.look",
            "Control the gaze direction of the physical HMBTEC eyes. "
            "Use this tool when the user explicitly asks the eyes to look in a direction. "
            "Allowed direction values: auto, center, left, right, up, down, up_left, up_right, down_left, down_right. "
            "duration_ms controls how long the gaze is held from 500 to 15000 milliseconds. "
            "After the timeout the eyes return to automatic gaze behavior.",
            PropertyList({
                Property("direction", kPropertyTypeString),
                Property("duration_ms", kPropertyTypeInteger, 500, 15000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto direction=properties["direction"].value<std::string>();
                int duration_ms=properties["duration_ms"].value<int>();
                ESP_LOGI(TAG,"AI eye look -> %s (%d ms)",direction.c_str(),duration_ms);
                return eyes_.SetLook(direction,duration_ms);
            }
        );
#endif

        ESP_LOGI(TAG, "HMBTEC MCP tools initialized");
    }

public:
    // ------------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------------

    HmbDualEyeBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        lichtblick_button_(HMBTEC_BUTTON_GPIO, false, 700){

#if HMB_DUALEYE_ENABLED
        display_.SetEmotionCallback([this](const char* emotion){
            if(!emotion){
                return;
            }
            std::string e(emotion);
            if(e=="neutral") eyes_.ClearExpression();
            else if(e=="happy") eyes_.SetExpression("happy",15000);
            else if(e=="relaxed") eyes_.SetExpression("relaxed",15000);
            else if(e=="sad") eyes_.SetExpression("sad",15000);
            else if(e=="thinking") eyes_.SetExpression("thinking",15000);
            else if(e=="surprised") eyes_.SetExpression("surprised",15000);
            else if(e=="confused") eyes_.SetExpression("confused",15000);
            else if(e=="sleepy") eyes_.SetExpression("sleepy",15000);
            else eyes_.ClearExpression();
            ESP_LOGI(TAG,"XiaoZhi emotion -> DualEye: %s",emotion);
        });
#endif

        InitializeButtons();
        InitializeTools();

#if HMB_DUALEYE_ENABLED
        // --------------------------------------------------------------------
        // HMB | TEC DualEye
        // --------------------------------------------------------------------

        eyes_.Init();

        Application::GetInstance().RegisterStateChangeListener(
            [this](DeviceState old_state, DeviceState new_state){
                ESP_LOGI(
                    TAG,
                    "State change: %d -> %d",
                    static_cast<int>(old_state),
                    static_cast<int>(new_state)
                );

                eyes_.SetState(new_state);
            }
        );

        ESP_LOGI(TAG, "DualEye enabled");
#else
        ESP_LOGI(TAG, "DualEye disabled - XiaoZhi baseline test");
#endif
    }

    // ------------------------------------------------------------------------
    // LED
    // ------------------------------------------------------------------------

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    // ------------------------------------------------------------------------
    // Audio
    // ------------------------------------------------------------------------

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN
        );

        return &codec;
    }

    // ------------------------------------------------------------------------
    // Display
    // ------------------------------------------------------------------------

    Display* GetDisplay() override {
        return &display_;
    }
};

DECLARE_BOARD(HmbDualEyeBoard);
