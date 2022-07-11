#
# project local config options, override the global config options
#

# ----------------------------------------------------------------------------
# override global config options
# ----------------------------------------------------------------------------
# enable/disable XIP, default to y
export __CONFIG_XIP := y

# enable/disable OTA, default to n
export __CONFIG_OTA := y

# enable/disable JPEG, default to n
export __CONFIG_JPEG := y
export __CONFIG_JPEG_SHAR_SRAM_64K := n

export __CONFIG_PSRAM := y
export __CONFIG_PSRAM_CHIP_OPI32 := y

##export __CONFIG_WLAN_AP := n
