#include <hal.h>

#if HAL_USE_PAL || defined(__DOXYGEN__)
const PALConfig pal_default_config = {
    {VAL_GPIOAODR, VAL_GPIOACRL, VAL_GPIOACRH},
    {VAL_GPIOBODR, VAL_GPIOBCRL, VAL_GPIOBCRH},
    {VAL_GPIOCODR, VAL_GPIOCCRL, VAL_GPIOCCRH},
    {VAL_GPIODODR, VAL_GPIODCRL, VAL_GPIODCRH},
#if STM32_HAS_GPIOE
    {VAL_GPIOEODR, VAL_GPIOECRL, VAL_GPIOECRH},
#endif
};
#endif

__attribute__((weak)) void enter_bootloader_mode_if_requested(void) {}

void __early_init(void) {
    enter_bootloader_mode_if_requested();
    stm32_clock_init();
}

void boardInit(void) {
    // Preserve SWD while releasing JTAG pins. Do not set BKP->DR10: the
    // STM32duino board profile uses it to request its USB bootloader.
    AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
}
