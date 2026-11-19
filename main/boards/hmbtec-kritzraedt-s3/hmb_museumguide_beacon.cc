/*
 * ================================================================
 * HMB | TEC Build Information
 * ================================================================
 * PROJECT_NAME:    HMB_MuseumGuide_Beacon
 * PROJECT_VERSION: 0.1.7
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
static RoomChangedCallback room_changed_callback = nullptr;

void SetRoomChangedCallback(RoomChangedCallback callback) {
    room_changed_callback = callback;
}


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

// ================================================================
// HMB | TEC MuseumGuide – Raumzuordnung
// ================================================================

static constexpr uint32_t ROOM_CONFIRM_MS = 5000;
static constexpr uint32_t BEACON_TIMEOUT_MS = 5000;
static constexpr float ROOM_HYSTERESIS_DB = 8.0f;

struct RoomConfig {
    const char* name;
    uint8_t mac[6];  // NimBLE: umgekehrte Byte-Reihenfolge
};

static const RoomConfig rooms[] = {
    {"RAUM_1", {0xC7, 0x40, 0xA8, 0x86, 0xC0, 0xC4}},
    {"RAUM_2", {0x25, 0x30, 0x84, 0x6D, 0x86, 0xEB}},
    {"RAUM_3", {0x2C, 0x48, 0x0E, 0x26, 0x67, 0xDD}}
};

static constexpr int ROOM_COUNT = sizeof(rooms) / sizeof(rooms[0]);

static int active_room = -1;
static int candidate_room = -1;
static uint32_t candidate_since = 0;
static uint32_t candidate_last_valid = 0;
static constexpr uint32_t ROOM_GRACE_MS = 1500;

static int GetRoomIndex(const uint8_t* mac) {
    for (int i = 0; i < ROOM_COUNT; i++) {
        if (memcmp(mac, rooms[i].mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static float GetAverage(const BeaconEntry& b) {
    if (b.count == 0) return -127.0f;

    int sum = 0;
    for (int i = 0; i < b.count; i++) {
        sum += b.samples[i];
    }
    return float(sum) / b.count;
}

static void UpdateRoomDetection(uint32_t now) {

    float levels[ROOM_COUNT];

    for (int i = 0; i < ROOM_COUNT; i++) {
        levels[i] = -127.0f;
    }

    for (const auto& b : beacons) {
        if (!b.used || b.count == 0 ||
            now - b.last_seen > BEACON_TIMEOUT_MS) {
            continue;
        }

        const int room = GetRoomIndex(b.mac);

        if (room >= 0) {
            const float avg = GetAverage(b);
            if (avg > levels[room]) {
                levels[room] = avg;
            }
        }
    }

    int strongest = -1;
    float strongest_level = -127.0f;

    for (int i = 0; i < ROOM_COUNT; i++) {
        if (levels[i] > strongest_level) {
            strongest_level = levels[i];
            strongest = i;
        }
    }

    const bool valid =
        strongest >= 0 &&
        strongest != active_room &&
        (active_room < 0 ||
         levels[active_room] <= -127.0f ||
         strongest_level >= levels[active_room] + ROOM_HYSTERESIS_DB);

    if (!valid) {
        if (candidate_room >= 0) {
            if (now - candidate_last_valid > ROOM_GRACE_MS) {
                candidate_room = -1;
            }
        }
        return;
    }

    if (candidate_room != strongest) {
        candidate_room = strongest;
        candidate_since = now;
        candidate_last_valid = now;

        ESP_LOGI(TAG, "ROOM_CANDIDATE=%s RSSI_AVG=%.1f",
                 rooms[strongest].name, strongest_level);
        return;
    }

    const uint32_t gap = now - candidate_last_valid;

    if (gap > ROOM_GRACE_MS) {
        candidate_since = now;
        candidate_last_valid = now;
        return;
    }

    // Regulaere Messintervalle zaehlen zur Bestaetigungszeit.
    // Nur Unterbrechungen ueber ROOM_GRACE_MS starten neu.
    candidate_last_valid = now;

    if (now - candidate_since >= ROOM_CONFIRM_MS) {
        active_room = strongest;
        candidate_room = -1;

        ESP_LOGW(TAG, "ROOM_CHANGED=%s RSSI_AVG=%.1f",
                 rooms[active_room].name, strongest_level);

        if (room_changed_callback) {
            room_changed_callback(active_room);
        }
    }
}

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

            UpdateRoomDetection(now);

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

    ESP_LOGI(TAG, "HMB MuseumGuide Beacon V0.1.4");
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
