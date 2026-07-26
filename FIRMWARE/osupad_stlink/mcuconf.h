// Reuse the proven STM32F103 clock and USB configuration, but pair it with
// the local ST-Link board implementation that does not set an Arduino
// bootloader request in the backup registers.
#pragma once

#include "../../platforms/chibios/boards/STM32_F103_STM32DUINO/configs/mcuconf.h"
