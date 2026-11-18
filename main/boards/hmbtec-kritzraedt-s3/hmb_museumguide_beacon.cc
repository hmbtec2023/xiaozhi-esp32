/*
 * ================================================================
 * HMB | TEC Build Information
 * ================================================================
 * PROJECT_NAME:    HMB_MuseumGuide_Beacon
 * PROJECT_VERSION: 0.1.3
 * BOARD:           HMBTEC Kritzraedt ESP32-S3
 * FRAMEWORK:       ESP-IDF 6.1 / NimBLE
 *
 * FEATURES:
 * - Passiver BLE-Observer
 * - MAC-Adresse und RSSI
 * - Advertising-Daten als HEX
 * - Keine BLE-Verbindungen
 * - Keine automatischen XiaoZhi-Trigger
 * ================================================================
 */

#include "hmb_museumguide_beacon.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_err.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

namespace HmbMuseumGuideBeacon {

static const char* TAG = "HMB_BEACON";
static bool started = false;


static constexpr int RSSI_WINDOW = 10;
static constexpr int MAX_BEACONS = 16;
static constexpr uint32_t LOG_INTERVAL_MS = 1000;

struct BeaconEntry {
    uint8_t id[20] = {};
    uint8_t mac[6] = {};
    uint8_t addr_type = 0;
    int8_t samples[RSSI_WINDOW] = {};
    uint8_t count = 0;
    uint8_t index = 0;
    uint32_t last_seen = 0;
    uint32_t last_log = 0;
    bool used = false;
};

static BeaconEntry beacons[MAX_BEACONS];


static int GapEvent(struct ble_gap_event* event, void*) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    const auto& d = event->disc;
    const uint8_t* data = d.data;
    const size_t len = d.length_data;

    for (size_t pos = 0; pos < len; ) {
        const uint8_t ad_len = data[pos];

        if (ad_len == 0 || pos + 1 + ad_len > len) break;

        if (ad_len >= 26 &&
            data[pos + 1] == 0xFF &&
            data[pos + 2] == 0x4C &&
            data[pos + 3] == 0x00 &&
            data[pos + 4] == 0x02 &&
            data[pos + 5] == 0x15) {

            const uint8_t* id = &data[pos + 6];
            const uint32_t now = esp_log_timestamp();

            BeaconEntry* entry = nullptr;

            for (auto& b : beacons) {
                if (b.used &&
                    memcmp(b.id, id, 20) == 0 &&
                    memcmp(b.mac, d.addr.val, 6) == 0 &&
                    b.addr_type == d.addr.type) {
                    entry = &b;
                    break;
                }
            }

            if (!entry) {
                for (auto& b : beacons) {
                    if (!b.used || now - b.last_seen > 30000) {
                        b = BeaconEntry{};
                        memcpy(b.id, id, 20);
                        memcpy(b.mac, d.addr.val, 6);
                        b.addr_type = d.addr.type;
                        b.used = true;
                        entry = &b;
                        break;
                    }
                }
            }

            if (!entry) return 0;

            entry->samples[entry->index] = d.rssi;
            entry->index = (entry->index + 1) % RSSI_WINDOW;

            if (entry->count < RSSI_WINDOW) {
                entry->count++;
            }

            entry->last_seen = now;

            if (now - entry->last_log >= LOG_INTERVAL_MS) {
                int sum = 0;

                for (int i = 0; i < entry->count; i++) {
                    sum += entry->samples[i];
                }

                const float average = float(sum) / entry->count;

                const uint16_t major =
                    (uint16_t(id[16]) << 8) | id[17];

                const uint16_t minor =
                    (uint16_t(id[18]) << 8) | id[19];

                ESP_LOGI(TAG,
                    "iBeacon UUID="
                    "%02X%02X%02X%02X-%02X%02X-%02X%02X-"
                    "%02X%02X-%02X%02X%02X%02X%02X%02X "
                    "MAC=%02X:%02X:%02X:%02X:%02X:%02X "
                    "MAJOR=%u MINOR=%u RSSI=%d AVG=%.1f N=%u",
                    id[0], id[1], id[2], id[3],
                    id[4], id[5], id[6], id[7],
                    id[8], id[9], id[10], id[11],
                    id[12], id[13], id[14], id[15],
                    d.addr.val[5], d.addr.val[4], d.addr.val[3],
                    d.addr.val[2], d.addr.val[1], d.addr.val[0],
                    major, minor, d.rssi,
                    average, unsigned(entry->count));

                entry->last_log = now;
            }

            break;
        }

        pos += 1 + ad_len;
    }

    return 0;
}

static void OnSync() {

    uint8_t own_addr_type = 0;

    int rc = ble_hs_id_infer_auto(0, &own_addr_type);

    if (rc != 0) {
        ESP_LOGE(TAG, "BLE address error: %d", rc);
        return;
    }

    struct ble_gap_disc_params params = {};

    params.passive = 1;
    params.filter_duplicates = 0;
    params.itvl = 0;
    params.window = 0;

    rc = ble_gap_disc(
        own_addr_type,
        BLE_HS_FOREVER,
        &params,
        GapEvent,
        nullptr
    );

    if (rc == 0) {
        ESP_LOGI(TAG, "Passive BLE scan started");
    } else {
        ESP_LOGE(TAG, "BLE scan error: %d", rc);
    }
}

static void HostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void Start() {

    if (started) {
        return;
    }

    ESP_LOGI(TAG, "HMB MuseumGuide Beacon V0.1.3");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "Source: %s", __FILE__);

    esp_err_t err = nimble_port_init();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s",
                 esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.sync_cb = OnSync;

    started = true;

    nimble_port_freertos_init(HostTask);
}

} // namespace HmbMuseumGuideBeacon
