#pragma once

#include "device_state.h"
#include "button.h"

#include <string>

enum class HmbEyeExpression {
    Neutral,
    Happy,
    Relaxed,
    Curious,
    Thinking,
    Surprised,
    Sad,
    Confused,
    Sleepy
};

enum class HmbEyeLook {
    Auto,
    Center,
    Left,
    Right,
    Up,
    Down,
    UpLeft,
    UpRight,
    DownLeft,
    DownRight
};

class HmbDualEye {
public:
    void Init();
    void SetState(DeviceState state);
    bool SetExpression(const std::string& expression,int duration_ms=4000);
    void ClearExpression();
    bool SetLook(const std::string& direction,int duration_ms=3000);
    void ClearLook();
private:
    DeviceState state_=kDeviceStateUnknown;
};
