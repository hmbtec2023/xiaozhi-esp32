// Auto-generated language config
// Language: de-DE with en-US fallback
#pragma once

#include <string_view>

#ifndef de_de
    #define de_de  // 預設語言
#endif

namespace Lang {
    // 语言元数据
    constexpr const char* CODE = "de-DE";

    // 字符串资源 (en-US as fallback for missing keys)
    namespace Strings {
        constexpr const char* ACCESS_VIA_BROWSER = "，Browser öffnen ";
        constexpr const char* ACTIVATION = "Gerät aktivieren";
        constexpr const char* BATTERY_CHARGING = "Wird geladen";
        constexpr const char* BATTERY_FULL = "Batterie voll";
        constexpr const char* BATTERY_LOW = "Niedriger Batteriestand";
        constexpr const char* BATTERY_NEED_CHARGE = "Niedriger Batteriestand, bitte aufladen";
        constexpr const char* CHECKING_NEW_VERSION = "Neue Version prüfen...";
        constexpr const char* CHECK_NEW_VERSION_FAILED = "Neue Version prüfen fehlgeschlagen, Wiederholung in %d Sekunden: %s";
        constexpr const char* CONNECTED_TO = "Verbunden mit ";
        constexpr const char* CONNECTING = "Verbindung wird hergestellt...";
        constexpr const char* CONNECTION_SUCCESSFUL = "Verbindung erfolgreich";
        constexpr const char* CONNECT_TO = "Verbinden zu ";
        constexpr const char* CONNECT_TO_HOTSPOT = "Handy mit Hotspot verbinden ";
        constexpr const char* DETECTING_MODULE = "Modul erkennen...";
        constexpr const char* DOWNLOAD_ASSETS_FAILED = "Fehler beim Herunterladen der Ressourcen";
        constexpr const char* ENTERING_WIFI_CONFIG_MODE = "Netzwerkkonfigurationsmodus eingeben...";
        constexpr const char* ERROR = "Fehler";
        constexpr const char* FLIGHT_MODE_OFF = "Flugmodus ist deaktiviert";
        constexpr const char* FLIGHT_MODE_ON = "Flugmodus ist aktiviert";
        constexpr const char* FOUND_NEW_ASSETS = "Neue Ressourcen gefunden: %s";
        constexpr const char* HELLO_MY_FRIEND = "Hallo, mein Freund!";
        constexpr const char* INFO = "Information";
        constexpr const char* INITIALIZING = "Initialisierung...";
        constexpr const char* LISTENING = "Zuhören...";
        constexpr const char* LOADING_ASSETS = "Ressourcen werden geladen...";
        constexpr const char* LOADING_PROTOCOL = "Verbindung zum Server...";
        constexpr const char* MAX_VOLUME = "Maximale Lautstärke";
        constexpr const char* MODEM_INIT_ERROR = "Modem-Initialisierung fehlgeschlagen";
        constexpr const char* MUTED = "Stummgeschaltet";
        constexpr const char* NEW_VERSION = "Neue Version ";
        constexpr const char* OTA_UPGRADE = "OTA-Upgrade";
        constexpr const char* PIN_ERROR = "Bitte SIM-Karte einlegen";
        constexpr const char* PLEASE_WAIT = "Bitte warten...";
        constexpr const char* REGISTERING_NETWORK = "Auf Netzwerk warten...";
        constexpr const char* REG_ERROR = "Netzwerkverbindung fehlgeschlagen, bitte Datenkartenstatus prüfen";
        constexpr const char* RTC_MODE_OFF = "AEC aus";
        constexpr const char* RTC_MODE_ON = "AEC ein";
        constexpr const char* SCANNING_WIFI = "Wi-Fi scannen...";
        constexpr const char* SERVER_ERROR = "Senden fehlgeschlagen, bitte Netzwerk prüfen";
        constexpr const char* SERVER_NOT_CONNECTED = "Service-Verbindung fehlgeschlagen, bitte später versuchen";
        constexpr const char* SERVER_NOT_FOUND = "Verfügbaren Service suchen";
        constexpr const char* SERVER_TIMEOUT = "Antwort-Timeout";
        constexpr const char* SPEAKING = "Sprechen...";
        constexpr const char* STANDBY = "Bereitschaft";
        constexpr const char* SWITCH_TO_4G_NETWORK = "Zu 4G wechseln...";
        constexpr const char* SWITCH_TO_WIFI_NETWORK = "Zu Wi-Fi wechseln...";
        constexpr const char* UPGRADE_FAILED = "Upgrade fehlgeschlagen";
        constexpr const char* UPGRADING = "System wird aktualisiert...";
        constexpr const char* VERSION = "Version ";
        constexpr const char* VOLUME = "Lautstärke ";
        constexpr const char* WAKE_WORD_HINT = "Sag „%s“, um mich aufzuwecken.";
        constexpr const char* WARNING = "Warnung";
        constexpr const char* WIFI_CONFIG_MODE = "Netzwerkkonfigurationsmodus";
    }

    // 音效资源 (en-US as fallback for missing audio files)
    namespace Sounds {

        extern const char ogg_0_start[] asm("_binary_0_ogg_start");
        extern const char ogg_0_end[] asm("_binary_0_ogg_end");
        static const std::string_view OGG_0 {
        static_cast<const char*>(ogg_0_start),
        static_cast<size_t>(ogg_0_end - ogg_0_start)
        };

        extern const char ogg_1_start[] asm("_binary_1_ogg_start");
        extern const char ogg_1_end[] asm("_binary_1_ogg_end");
        static const std::string_view OGG_1 {
        static_cast<const char*>(ogg_1_start),
        static_cast<size_t>(ogg_1_end - ogg_1_start)
        };

        extern const char ogg_2_start[] asm("_binary_2_ogg_start");
        extern const char ogg_2_end[] asm("_binary_2_ogg_end");
        static const std::string_view OGG_2 {
        static_cast<const char*>(ogg_2_start),
        static_cast<size_t>(ogg_2_end - ogg_2_start)
        };

        extern const char ogg_3_start[] asm("_binary_3_ogg_start");
        extern const char ogg_3_end[] asm("_binary_3_ogg_end");
        static const std::string_view OGG_3 {
        static_cast<const char*>(ogg_3_start),
        static_cast<size_t>(ogg_3_end - ogg_3_start)
        };

        extern const char ogg_4_start[] asm("_binary_4_ogg_start");
        extern const char ogg_4_end[] asm("_binary_4_ogg_end");
        static const std::string_view OGG_4 {
        static_cast<const char*>(ogg_4_start),
        static_cast<size_t>(ogg_4_end - ogg_4_start)
        };

        extern const char ogg_5_start[] asm("_binary_5_ogg_start");
        extern const char ogg_5_end[] asm("_binary_5_ogg_end");
        static const std::string_view OGG_5 {
        static_cast<const char*>(ogg_5_start),
        static_cast<size_t>(ogg_5_end - ogg_5_start)
        };

        extern const char ogg_6_start[] asm("_binary_6_ogg_start");
        extern const char ogg_6_end[] asm("_binary_6_ogg_end");
        static const std::string_view OGG_6 {
        static_cast<const char*>(ogg_6_start),
        static_cast<size_t>(ogg_6_end - ogg_6_start)
        };

        extern const char ogg_7_start[] asm("_binary_7_ogg_start");
        extern const char ogg_7_end[] asm("_binary_7_ogg_end");
        static const std::string_view OGG_7 {
        static_cast<const char*>(ogg_7_start),
        static_cast<size_t>(ogg_7_end - ogg_7_start)
        };

        extern const char ogg_8_start[] asm("_binary_8_ogg_start");
        extern const char ogg_8_end[] asm("_binary_8_ogg_end");
        static const std::string_view OGG_8 {
        static_cast<const char*>(ogg_8_start),
        static_cast<size_t>(ogg_8_end - ogg_8_start)
        };

        extern const char ogg_9_start[] asm("_binary_9_ogg_start");
        extern const char ogg_9_end[] asm("_binary_9_ogg_end");
        static const std::string_view OGG_9 {
        static_cast<const char*>(ogg_9_start),
        static_cast<size_t>(ogg_9_end - ogg_9_start)
        };

        extern const char ogg_activation_start[] asm("_binary_activation_ogg_start");
        extern const char ogg_activation_end[] asm("_binary_activation_ogg_end");
        static const std::string_view OGG_ACTIVATION {
        static_cast<const char*>(ogg_activation_start),
        static_cast<size_t>(ogg_activation_end - ogg_activation_start)
        };

        extern const char ogg_err_pin_start[] asm("_binary_err_pin_ogg_start");
        extern const char ogg_err_pin_end[] asm("_binary_err_pin_ogg_end");
        static const std::string_view OGG_ERR_PIN {
        static_cast<const char*>(ogg_err_pin_start),
        static_cast<size_t>(ogg_err_pin_end - ogg_err_pin_start)
        };

        extern const char ogg_err_reg_start[] asm("_binary_err_reg_ogg_start");
        extern const char ogg_err_reg_end[] asm("_binary_err_reg_ogg_end");
        static const std::string_view OGG_ERR_REG {
        static_cast<const char*>(ogg_err_reg_start),
        static_cast<size_t>(ogg_err_reg_end - ogg_err_reg_start)
        };

        extern const char ogg_exclamation_start[] asm("_binary_exclamation_ogg_start");
        extern const char ogg_exclamation_end[] asm("_binary_exclamation_ogg_end");
        static const std::string_view OGG_EXCLAMATION {
        static_cast<const char*>(ogg_exclamation_start),
        static_cast<size_t>(ogg_exclamation_end - ogg_exclamation_start)
        };

        extern const char ogg_low_battery_start[] asm("_binary_low_battery_ogg_start");
        extern const char ogg_low_battery_end[] asm("_binary_low_battery_ogg_end");
        static const std::string_view OGG_LOW_BATTERY {
        static_cast<const char*>(ogg_low_battery_start),
        static_cast<size_t>(ogg_low_battery_end - ogg_low_battery_start)
        };

        extern const char ogg_popup_start[] asm("_binary_popup_ogg_start");
        extern const char ogg_popup_end[] asm("_binary_popup_ogg_end");
        static const std::string_view OGG_POPUP {
        static_cast<const char*>(ogg_popup_start),
        static_cast<size_t>(ogg_popup_end - ogg_popup_start)
        };

        extern const char ogg_success_start[] asm("_binary_success_ogg_start");
        extern const char ogg_success_end[] asm("_binary_success_ogg_end");
        static const std::string_view OGG_SUCCESS {
        static_cast<const char*>(ogg_success_start),
        static_cast<size_t>(ogg_success_end - ogg_success_start)
        };

        extern const char ogg_upgrade_start[] asm("_binary_upgrade_ogg_start");
        extern const char ogg_upgrade_end[] asm("_binary_upgrade_ogg_end");
        static const std::string_view OGG_UPGRADE {
        static_cast<const char*>(ogg_upgrade_start),
        static_cast<size_t>(ogg_upgrade_end - ogg_upgrade_start)
        };

        extern const char ogg_vibration_start[] asm("_binary_vibration_ogg_start");
        extern const char ogg_vibration_end[] asm("_binary_vibration_ogg_end");
        static const std::string_view OGG_VIBRATION {
        static_cast<const char*>(ogg_vibration_start),
        static_cast<size_t>(ogg_vibration_end - ogg_vibration_start)
        };

        extern const char ogg_welcome_start[] asm("_binary_welcome_ogg_start");
        extern const char ogg_welcome_end[] asm("_binary_welcome_ogg_end");
        static const std::string_view OGG_WELCOME {
        static_cast<const char*>(ogg_welcome_start),
        static_cast<size_t>(ogg_welcome_end - ogg_welcome_start)
        };

        extern const char ogg_wificonfig_start[] asm("_binary_wificonfig_ogg_start");
        extern const char ogg_wificonfig_end[] asm("_binary_wificonfig_ogg_end");
        static const std::string_view OGG_WIFICONFIG {
        static_cast<const char*>(ogg_wificonfig_start),
        static_cast<size_t>(ogg_wificonfig_end - ogg_wificonfig_start)
        };
    }
}
