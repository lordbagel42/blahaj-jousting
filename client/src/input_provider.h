// client/src/input_provider.h
#pragma once
#include "game_state.h"

class InputProvider {
public:
    virtual ~InputProvider() = default;
    virtual DriveCommand read() = 0;
};
