#
# project local config options, override the global config options
#

# ----------------------------------------------------------------------------
# override global config options
# ----------------------------------------------------------------------------
# enable/disable wlan, default to y
export __CONFIG_WLAN := n

# enable/disable XIP, default to y
export __CONFIG_XIP := n

export __CONFIG_PSRAM := y
export __CONFIG_PSRAM_CHIP_OPI32 := y

export __CONFIG_XPLAY := n
