# enable wpm
WPM_ENABLE = yes

SRC += $(USER_PATH)/splitkb/custom_display/custom_display.c
POST_CONFIG_H += $(USER_PATH)/splitkb/custom_display/config.h

# Fonts
SRC += $(USER_PATH)/splitkb/custom_display/graphics/fonts/sixtyfour.qff.c \
       $(USER_PATH)/splitkb/custom_display/graphics/fonts/Retron2000-27.qff.c \
       $(USER_PATH)/splitkb/custom_display/graphics/fonts/Retron2000-underline-27.qff.c
