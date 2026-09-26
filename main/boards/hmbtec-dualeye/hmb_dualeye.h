#pragma once
#include "device_state.h"
class HmbDualEye {
public:
    void Init();
    void SetState(DeviceState state);
private:
    DeviceState state_ = kDeviceStateUnknown;
};
