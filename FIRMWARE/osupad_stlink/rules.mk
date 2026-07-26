# MCU name
MCU = STM32F103

# No BOOTLOADER is configured: this firmware is linked to 0x08000000 and must
# be programmed through SWD/ST-Link (or the STM32F103 ROM UART bootloader).

# Build Options
#   change yes to no to disable
#
BOOTMAGIC_ENABLE = no       # Enable Bootmagic Lite
MOUSEKEY_ENABLE = yes       # Mouse keys
EXTRAKEY_ENABLE = yes       # Audio control and System control
CONSOLE_ENABLE = no         # Console for debug
COMMAND_ENABLE = no         # Commands for debug and configuration
NKRO_ENABLE = no            # Enable N-Key Rollover
ENCODER_ENABLE = no
LTO_ENABLE = yes
RGBLIGHT_ENABLE = yes
ENCODER_MAP_ENABLE = no
