#pragma once

// Skeleton for a future dedicated security-tooling submenu, built on the
// generic menu_t tree in ui/menu.h (same menu_item_t/menu_link_submenu()
// pattern the existing RFID/IR/Wi-Fi/Bluetooth submenus in main.c use) --
// this file is intentionally empty of real declarations until a specific
// feature set is defined. Not a replacement for menu.c/h's generic tree
// logic; this would only define the specific menu_t instance and its
// item callbacks, wired into the top-level menu the same way
// action_wifi_setup()/action_rfid_clone()/etc. are wired in main.c today.
//
// When a feature is specified, add its menu_t/menu_item_t declarations
// here (following main.c's existing "s_..._menu_items" naming and
// menu_link_submenu() wiring pattern) and add "ui/security_menu.c" to
// main/CMakeLists.txt's SRCS list.
