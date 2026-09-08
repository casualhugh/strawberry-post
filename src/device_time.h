#pragma once

#include <stdint.h>

bool setDeviceTime(uint64_t epochSeconds);
bool deviceTimeIsSet();
uint64_t deviceEpochSeconds();
