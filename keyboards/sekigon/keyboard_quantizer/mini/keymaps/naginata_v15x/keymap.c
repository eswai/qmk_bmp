// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "full.h"

#include "virtser.h"
#include "os_detection.h"
#include "cli.h"
#include "dynamic_config.h"
#include "quantizer_mouse.h"
#include "naginata.h"

user_config_t user_config;
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {{{KC_NO}}};

// To compile keymap_introspection.c, declear combo here
extern combo_t key_combos[COMBO_LEN_MAX];

int8_t virtser_send_wrap(uint8_t c) {
    virtser_send(c);
    return 0;
}

void keyboard_pre_init_user(void) {
    cli_init();
    print_set_sendchar(virtser_send_wrap);
    dynamic_config_init();
}

void keyboard_post_init_user(void) {
    user_config.raw = eeconfig_read_user();
    set_mouse_gesture_threshold(get_mouse_gesture_threshold());

    // 薙刀式
    uint16_t ngonkeys[] = {KC_H, KC_J};
    uint16_t ngoffkeys[] = {KC_F, KC_G};
    set_naginata(ngonkeys, ngoffkeys);
    // 薙刀式

    wait_ms(400);
    switch (detected_host_os()) {
        case OS_WINDOWS:
        switchOS(NG_WIN);
        break;
        case OS_MACOS:
        case OS_IOS:
        switchOS(NG_MAC);
        break;
        case OS_LINUX:
        switchOS(NG_LINUX);
        break;
        default:
        switchOS(NG_WIN);
    }
}

void housekeeping_task_user(void) {
    static os_variant_t detected_os = OS_UNSURE;
    if (detected_os != detected_host_os()) {
        dynamic_config_activate_default_apps();
        detected_os = detected_host_os();
    }

    dynamic_config_task();
    cli_exec();
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    return pre_process_record_mouse(keycode, record);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    bool cont = process_record_dynamic_config(keycode, record) //
                && process_record_mouse(keycode, record);

    if (record->event.pressed) {
        switch (keycode) {
            case QK_KB_0 ... QK_KB_3:
                // Used for mouse gesture
                break;
            case QK_KB_4:
                send_bootstrap_macro();
                break;
            case QK_KB_5:
                set_keyboard_language(LANG_US);
                user_config.keyboard_lang_us_or_jp = 0;
                eeconfig_update_user(user_config.raw);
                break;
            case QK_KB_6:
                set_keyboard_language(LANG_JP);
                user_config.keyboard_lang_us_or_jp = 1;
                eeconfig_update_user(user_config.raw);
                break;
            case QK_KB_7:
                set_os_language(LANG_US);
                user_config.os_lang_us_or_jp = 0;
                eeconfig_update_user(user_config.raw);
                break;
            case QK_KB_8:
                set_os_language(LANG_JP);
                user_config.os_lang_us_or_jp = 1;
                eeconfig_update_user(user_config.raw);
                break;
        }
    }

    // 薙刀式
    if (!process_naginata(keycode, record))
        return false;
    // 薙刀式

    return cont;
}

void post_process_record_user(uint16_t keycode, keyrecord_t* record) {
    post_process_record_mouse(keycode, record);
}

 void eeconfig_init_user(void) {
    user_config.keyboard_lang_us_or_jp = get_keyboard_language() == LANG_JP ? 1 : 0;
    user_config.os_lang_us_or_jp       = get_os_language() == LANG_JP ? 1 : 0;

    eeconfig_update_user(user_config.raw);
 }