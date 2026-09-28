#pragma once

#include "device_state.h"
#include "button.h"

class HmbDualEye {
public:
    void Init();
    void SetState(DeviceState state);
private:
    DeviceState state_ = kDeviceStateUnknown;
};
