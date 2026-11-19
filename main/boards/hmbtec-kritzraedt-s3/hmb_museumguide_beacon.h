#pragma once

/*
 * HMB | TEC MuseumGuide Beacon
 * Version: 0.1.7
 *
 * Passiver BLE-Observer fuer die Raumerkennung.
 * Keine BLE-Verbindungen.
 * Raumwechsel werden ueber einen Callback gemeldet.
 */

namespace HmbMuseumGuideBeacon {

    using RoomChangedCallback = void (*)(int room_index);

    void Start();
    void SetRoomChangedCallback(RoomChangedCallback callback);

}
