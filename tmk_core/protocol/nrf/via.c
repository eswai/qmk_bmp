/*
Copyright 2020 Sekigon

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "quantum.h"
#include "via.h"

#include <string.h>

#include "apidef.h"
#include "bmp_macro.h"
#include "bmp_config.h"
#include "bmp_via.h"

bool       via_keymap_update_flag = false;
bool       via_macro_update_flag   = false;
bool       via_exkc_update_flag   = false;
extern int bootloader_jump_counter;

static bool     via_layout_update_flag = false;
static uint32_t via_layout_code        = 0;

static inline const bmp_api_config_t *get_config() {
    return BMPAPI->app.get_config();
}

static inline uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row,
                                                  uint8_t col) {
    bmp_api_keypos_t keypos = {.col = col, .row = row};
    return BMPAPI->app.keymap_key_to_keycode(layer, &keypos);
}

static inline void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row,
                                              uint8_t col, uint16_t keycode) {
    bmp_api_keypos_t keypos = {.col = col, .row = row};
    if (dynamic_keymap_get_keycode(layer, row, col) == keycode) {
        // no update
        return;
    }

    if (BMPAPI->app.set_keycode_to_keymap(layer, &keypos, keycode) == BMP_OK) {
        via_keymap_update_flag = true;
    } else {
        xprintf("<via>Failed to set keycode\n");
    }
}

static inline uint8_t dynamic_keymap_get_layer_count() {
    bmp_api_keymap_info_t keymap_info;
    BMPAPI->app.get_keymap_info(&keymap_info);

    uint8_t layer = DYNAMIC_KEYMAP_MAX_LEN / keymap_info.keynum;

    return layer < 32 ? layer : 32;
}

static inline void dynamic_keymap_get_buffer(uint16_t offset, uint16_t size,
                                             uint8_t *dst) {
    if (size & 1) {
        xprintf("<via>Error: offset should be even.");
    }

    bmp_api_keymap_info_t keymap_info;
    BMPAPI->app.get_keymap_info(&keymap_info);
    const bmp_api_matrix_config_t *matrix_config = &get_config()->matrix;

    uint8_t key_per_layer = matrix_config->rows * matrix_config->cols;
    uint8_t layer         = (offset / sizeof(uint16_t)) / key_per_layer;
    uint8_t row = ((offset / sizeof(uint16_t)) - layer * key_per_layer) /
                  matrix_config->cols;
    uint8_t col = (offset / sizeof(uint16_t)) - layer * key_per_layer -
                  row * matrix_config->cols;

    uint16_t idx = 0;
    while (idx < size) {
        uint16_t keycode = dynamic_keymap_get_keycode(layer, row, col);
        *dst++           = keycode >> 8;
        *dst++           = keycode & 0xFF;

        col++;
        if (col >= matrix_config->cols) {
            col = 0;
            row++;
            if (row >= matrix_config->rows) {
                row = 0;
                layer++;
            }
        }

        idx += 2;
    }
}

static inline void dynamic_keymap_set_buffer(uint16_t offset, uint16_t size,
                                             const uint8_t *dat) {
    if (size & 1) {
        xprintf("<via>Error: offset should be even.");
    }

    bmp_api_keymap_info_t keymap_info;
    BMPAPI->app.get_keymap_info(&keymap_info);
    const bmp_api_matrix_config_t *matrix_config = &get_config()->matrix;

    uint8_t key_per_layer = matrix_config->rows * matrix_config->cols;
    uint8_t layer         = (offset / sizeof(uint16_t)) / key_per_layer;
    uint8_t row = ((offset / sizeof(uint16_t)) - layer * key_per_layer) /
                  matrix_config->cols;
    uint8_t col = (offset / sizeof(uint16_t)) - layer * key_per_layer -
                  row * matrix_config->cols;

    uint16_t idx = 0;
    while (idx < size) {
        uint16_t keycode = ((uint16_t)(*dat++) << 8);
        keycode |= (*dat++);
        dynamic_keymap_set_keycode(layer, row, col, keycode);

        col++;
        if (col >= matrix_config->cols) {
            col = 0;
            row++;
            if (row >= matrix_config->rows) {
                row = 0;
                layer++;
            }
        }

        idx += 2;
    }
}

__attribute__((weak)) void dynamic_keymap_reset(void) {}

__attribute__((weak)) void raw_hid_receive_kb(const uint8_t *data,
                                              uint8_t        length) {}

extern bmp_ex_keycode_t     bmp_ex_keycodes[BMP_EX_KC_LEN];
extern uint32_t             bmp_ex_keycode_num;
static bmp_api_config_t     s_bmp_config;
static bmp_qmk_config_t     s_tapping_term_config;
static bmp_encoder_config_t s_encoder_config;

static void fill_config_buffer(uint8_t *dest, uint8_t const *item_data,
                        uint32_t offset, uint32_t size) {
    uint8_t len = 32 - 5;
    if (size - offset < len) {
        len = size - offset;
    }
    memcpy(&dest[offset], &item_data[2], len);
}

static void set_via_bmp_item(uint8_t *data, uint8_t length) {
    const uint8_t item_idx  = data[2];
    uint8_t      *item_data = &data[3];

    switch (item_idx) {
        case item_extend_keycode:
            if (item_data[0] < BMP_EX_KC_LEN) {
                if (memcmp(&bmp_ex_keycodes[item_data[0]], &item_data[1],
                           sizeof(bmp_ex_keycode_t)) == 0) {
                    break;
                }
                memcpy(&bmp_ex_keycodes[item_data[0]], &item_data[1],
                       sizeof(bmp_ex_keycode_t));
                via_exkc_update_flag = true;
                if (bmp_ex_keycode_num < item_data[0] + 1) {
                    bmp_ex_keycode_num = item_data[0] + 1;
                }
            }
            break;
        case item_config_buffer: {
            uint16_t offset = (item_data[0] | ((uint16_t)item_data[1] << 8));
            if (offset < sizeof(s_bmp_config)) {
                uint8_t *config = (uint8_t *)&s_bmp_config;
                fill_config_buffer(config, item_data, offset, sizeof(s_bmp_config));
            } else {
                BMPAPI->app.set_config(&s_bmp_config);
                save_config();
            }
        } break;
        case item_tapping_term_buffer: {
            uint16_t offset = (item_data[0] | ((uint16_t)item_data[1] << 8));
            if (offset < sizeof(s_tapping_term_config)) {
                uint8_t *config = (uint8_t *)&s_tapping_term_config;
                fill_config_buffer(config, item_data, offset,
                            sizeof(s_tapping_term_config));
            } else {
                set_tapping_term_config(&s_tapping_term_config);
                save_tapping_term_file();
            }
        }
        case item_encoder_buffer: {
            uint16_t offset = (item_data[0] | ((uint16_t)item_data[1] << 8));
            if (offset < sizeof(s_encoder_config)) {
                uint8_t *config = (uint8_t *)&s_encoder_config;
                fill_config_buffer(config, item_data, offset,
                            sizeof(s_encoder_config));
            } else {
                set_encoder_config(&s_encoder_config);
                save_encoder_config();
            }
        } break;
        case item_remove_files: {
            BMPAPI->app.delete_file(item_data[0]);
        } break;
    }
}

static void get_via_bmp_item(uint8_t *data, uint8_t length) {
    const uint8_t item_idx  = data[2];
    uint8_t      *item_data = &data[3];

    switch (item_idx) {
        case item_protocol_version:
            item_data[0] = BMP_VIA_PROTOCOL_VER;
            break;
        case item_extend_keycode_count:
            item_data[0] = BMP_EX_KC_LEN;
            break;
        case item_extend_keycode:
            if (item_data[0] < BMP_EX_KC_LEN) {
                memcpy(&item_data[1], &bmp_ex_keycodes[item_data[0]],
                       sizeof(bmp_ex_keycode_t));
            }
            break;
        case item_config_len: {
            uint16_t len = sizeof(bmp_api_config_t);
            item_data[0] = len & 0xff;
            item_data[1] = len >> 8;
        } break;
        case item_config_buffer: {
            uint16_t offset = (item_data[0] | ((uint16_t)(item_data[1]) << 8));
            const uint8_t *config = (uint8_t *)BMPAPI->app.get_config();
            memcpy(&item_data[2], &config[offset], 32 - 5);
        } break;
        case item_tapping_term_len: {
            uint16_t len = sizeof(s_tapping_term_config);
            item_data[0] = len & 0xff;
            item_data[1] = len >> 8;
        } break;
        case item_tapping_term_buffer: {
            uint16_t offset = (item_data[0] | ((uint16_t)(item_data[1]) << 8));
            const uint8_t *config = (uint8_t *)get_tapping_term_config();
            memcpy(&item_data[2], &config[offset], 32 - 5);
        } break;
        case item_encoder_len: {
            uint16_t len = sizeof(s_encoder_config);
            item_data[0] = len & 0xff;
            item_data[1] = len >> 8;
        } break;
        case item_encoder_buffer: {
            uint16_t offset = (item_data[0] | ((uint16_t)(item_data[1]) << 8));
            const uint8_t *config = (uint8_t *)get_bmp_encoder_config();
            memcpy(&item_data[2], &config[offset], 32 - 5);
        } break;
    }
}

void raw_hid_receive_bmp(uint8_t *data, uint8_t length) {
    const uint8_t *command_id   = &data[0];
    const uint8_t *command_data = &data[1];
    switch (*command_id) {
        case id_set_keyboard_value: {
            switch (command_data[0]) {
                case id_control_save_flag:
                    if (length > 3) {
                        if (via_layout_update_flag) {
                            BMPAPI->app.set_layout_code(
                                (const uint8_t *)(&via_layout_code));
                        }

                        if (via_keymap_update_flag) {
                            if (BMPAPI->app.save_file(KEYMAP_RECORD) == 0) {
                                data[2] = 0;
                            } else {
                                data[2] = 0xFF;
                            }
                            via_keymap_update_flag = false;
                        }

                        if (via_exkc_update_flag) {
                            save_ex_keycode_file();
                            via_exkc_update_flag = false;
                        }

                        if (via_macro_update_flag) {
                            bmp_macro_save_file();
                            via_macro_update_flag = false;
                        }
                    }
                    break;
                case id_bmp_item:
                    set_via_bmp_item(data, length);
                    break;
            }
        } break;

        case id_get_keyboard_value: {
            switch (command_data[0]) {
                case id_bmp_item:
                    get_via_bmp_item(data, length);
                    break;
            }
        } break;

        default:
            break;
    }

    raw_hid_receive_kb(data, length);
}

void eeconfig_init_via(void) { }

bool via_eeprom_is_valid(void) { return true; }

void via_eeprom_set_valid(bool valid) { }

void via_eeprom_reset(void) { xprintf("<via>eeprom reset: not implemented\n"); }

uint32_t via_get_layout_options(void) {
    if (via_layout_update_flag) {
        return via_layout_code;
    }

    bmp_api_keymap_info_t keymap_info;
    BMPAPI->app.get_keymap_info(&keymap_info);

    // return as big endian
    via_layout_code = ((uint32_t)keymap_info.layout_code[3] << 24) |
                      ((uint32_t)keymap_info.layout_code[2] << 16) |
                      ((uint32_t)keymap_info.layout_code[1] << 8) |
                      ((uint32_t)keymap_info.layout_code[0]);
    return via_layout_code;
}

void via_set_layout_options(uint32_t value) {
    via_layout_code        = value;
    via_layout_update_flag = true;
}

void bmp_via_receive_cb(uint8_t *data, uint8_t length,
                        int (*raw_hid_send)(const uint8_t *data,
                                            uint8_t        length)) {
    uint8_t *command_id   = &data[0];
    uint8_t *command_data = &data[1];
    switch (*command_id) {
        case id_get_protocol_version: {
            command_data[0] = VIA_PROTOCOL_VERSION >> 8;
            command_data[1] = VIA_PROTOCOL_VERSION & 0xFF;
            break;
        }
        case id_get_keyboard_value: {
            switch (command_data[0]) {
                case id_uptime: {
                    uint32_t value  = timer_read32();
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                case id_layout_options: {
                    uint32_t value = via_get_layout_options();
                    xprintf("<via>get_layout_options:0x%08x\n", value);
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                case id_switch_matrix_state: {
                    const bmp_api_config_t *config = get_config();
                    if ((config->matrix.cols / 8 + 1) * config->matrix.rows <=
                        28) {
                        uint8_t i = 1;
                        for (uint8_t row = 0; row < config->matrix.rows;
                             row++) {
                            matrix_row_t value = matrix_get_row(row);

                            if (config->matrix.cols > 24) {
                                command_data[i++] = (value >> 24) & 0xFF;
                            }

                            if (config->matrix.cols > 16) {
                                command_data[i++] = (value >> 16) & 0xFF;
                            }

                            if (config->matrix.cols > 8) {
                                command_data[i++] = (value >> 8) & 0xFF;
                            }

                            command_data[i++] = value & 0xFF;
                        }
                    }
                    break;
                }
                default: {
                    raw_hid_receive_bmp(data, length);
                    break;
                }
            }
            break;
        }
        case id_set_keyboard_value: {
            switch (command_data[0]) {
                case id_layout_options: {
                    uint32_t value = ((uint32_t)command_data[1] << 24) |
                                     ((uint32_t)command_data[2] << 16) |
                                     ((uint32_t)command_data[3] << 8) |
                                     (uint32_t)command_data[4];
                    xprintf("<via>set_layout_options:0x%08x\n", value);
                    via_set_layout_options(value);
                    break;
                }
                default: {
                    raw_hid_receive_bmp(data, length);
                    break;
                }
            }
            break;
        }
        case id_dynamic_keymap_get_keycode: {
            xprintf("<via>get_keycode\n");
            uint16_t keycode = dynamic_keymap_get_keycode(
                command_data[0], command_data[1], command_data[2]);
            command_data[3] = keycode >> 8;
            command_data[4] = keycode & 0xFF;
            break;
        }
        case id_dynamic_keymap_set_keycode: {
            xprintf("<via>set_keycode. l:%d, r:%d, c:%d, k:0x%04x\n",
                    command_data[0], command_data[1], command_data[2],
                    (command_data[3] << 8) | command_data[4]);
            dynamic_keymap_set_keycode(
                command_data[0], command_data[1], command_data[2],
                (command_data[3] << 8) | command_data[4]);
            break;
        }
        case id_dynamic_keymap_reset: {
            xprintf("<via>keymap_reset\n");
            dynamic_keymap_reset();
            break;
        }
        case id_dynamic_keymap_macro_get_count: {
            xprintf("<via>dynamic_macro_get_count\n");
            command_data[0] = BMP_MACRO_COUNT;
            break;
        }
        case id_dynamic_keymap_macro_get_buffer_size: {
            xprintf("<via>dynamic_macro_get_buffer_size\n");
            uint16_t size = BMP_MACRO_FILE_LEN;

            command_data[0] = size >> 8;
            command_data[1] = size & 0xFF;
            break;
        }
        case id_dynamic_keymap_macro_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];  // size <= 28
            xprintf("<via>dynamic_macro_get_buffer, offset:%d, size:%d\n", offset, size);

            bmp_macro_get_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_macro_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];  // size <= 28
            xprintf("<via>dynamic_macro_set_buffer, offset:%d, size:%d\n", offset, size);

            bmp_macro_set_buffer(offset, size, &command_data[3]);
            via_macro_update_flag = true;

            if (size == 1 && offset + size == BMP_MACRO_FILE_LEN &&
                command_data[3] == 0) {
                bmp_macro_save_file();
                via_macro_update_flag = false;
            }

            break;
        }
        case id_dynamic_keymap_macro_reset: {
            xprintf("<via>id_dynamic_keymap_macro_reset\n");
            bmp_macro_reset();
            break;
        }
        case id_dynamic_keymap_get_layer_count: {
            command_data[0] = dynamic_keymap_get_layer_count();
            xprintf("<via>dynamic_keymap_get_layer_count, %d\n",
                    command_data[0]);
            break;
        }
        case id_dynamic_keymap_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            xprintf("<via>dynamic_keymap_get_buffer, offset:%d, size:%d\n",
                    offset, size);

            dynamic_keymap_get_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            xprintf("<via>dynamic_keymap_set_buffer, offset:%d, size:%d\n",
                    offset, size);

            dynamic_keymap_set_buffer(offset, size, &command_data[3]);

            break;
        }
        case id_eeprom_reset: {
            via_eeprom_reset();
            break;
        }
        case id_bootloader_jump: {
            raw_hid_send(data, length);

            bootloader_jump_counter = 10;
            break;
        }
        default: {
            // Unknown command
            *command_id = id_unhandled;
            break;
        }
    }

    // return response to via configurator
    raw_hid_send(data, length);
}

bool process_record_via(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        if (keycode >= MACRO00 && keycode <= MACRO15) {
            uint8_t id = keycode - MACRO00;
            bmp_macro_send(id);
            return false;
        }
    }

    return true;
}

void via_init(void) {}
