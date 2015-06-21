#ifndef CONFIG_H
#define CONFIG_H

#include <xcb/xproto.h>

#define DEBUG
#define MULTIHEAD

#define MODIFIER_MASK XCB_MOD_MASK_1 | XCB_MOD_MASK_SHIFT
#define MOVE_MOUSE_BUTTON 1
#define RESIZE_MOUSE_BUTTON 3

#define BORDER_COLOR_UNFOCUSED 0xFF000000
#define BORDER_COLOR_FOCUSED 0xFFABABAB
#define BORDER_WIDTH 2

#define LEFT_PADDING 4
#define RIGHT_PADDING 4
#define TOP_PADDING 24
#define BOTTOM_PADDING 4

#endif /* CONFIG_H */
