// Copyright 2024 splitkb.com (support@splitkb.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "halcyon.h"
#include "custom_display.h"

#include "hardware/structs/rosc.h"

// Fonts mono2
#include "graphics/fonts/sixtyfour.qff.h"
#include "graphics/fonts/Retron2000-27.qff.h"
#include "graphics/fonts/Retron2000-underline-27.qff.h"

// assets
static const char *qwerty = "QWERTY";
static const char *dvorak = "DVORAK";
static const char *nav    = "NAVIGATION";
static const char *sym    = "NUMBERS";
static const char *func   = "FKEY";
static const char *adj    = "ADJUST";
static const char *undef  = "UNDEF";
static const char *caps   = "Caps";
static const char *wpm    = "WPM: ";

enum layers {
    _QWERTY   = 0,
    _DVORAK   = 1,
    _NAV      = 2,
    _SYM      = 3,
    _FUNCTION = 4,
    _ADJUST   = 5,
};

static painter_font_handle_t sixtyfour;
static painter_font_handle_t Retron27;
static painter_font_handle_t Retron27_underline;

static uint8_t lcd_surface_fb[SURFACE_REQUIRED_BUFFER_BYTE_SIZE(135, 240, 16)];

static int color_value = 0;

painter_device_t lcd;
painter_device_t lcd_surface;

led_t         last_led_usb_state = {0};
layer_state_t last_layer_state   = {0};

#define GRID_WIDTH 27
#define GRID_HEIGHT 48
#define CELL_SIZE 4 // Cell size excluding outline
#define OUTLINE_SIZE 1

// Define the probability factor for initial alive cells
#define INITIAL_ALIVE_PROBABILITY 0.2 // 20% chance of being alive

bool grid[GRID_HEIGHT][GRID_WIDTH];         // Current state
bool new_grid[GRID_HEIGHT][GRID_WIDTH];     // Next state
bool changed_grid[GRID_HEIGHT][GRID_WIDTH]; // Tracks changed cells

// scrolling text
static bool     animation_ended  = false;
static char     currentLayer[12] = "\0";
static int      currentColor[3];
static int      lateral_scroll         = 0;
static int      scroll_update_interval = 150;
static uint32_t last_scroll_update     = 0;

uint32_t get_random_32bit(void) {
    uint32_t random_value = 0;
    for (int i = 0; i < 32; i++) {
        wait_ms(1);
        random_value = (random_value << 1) | (rosc_hw->randombit & 1);
    }
    return random_value;
}

void init_grid() {
    // Initialize grid with alive cells
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            grid[y][x]         = (rand() < INITIAL_ALIVE_PROBABILITY * RAND_MAX); // Use probability factor
            changed_grid[y][x] = true;                                            // Mark all as changed initially
        }
    }
}

void draw_grid() {
    uint8_t hue      = 0; // Hue for alive cells
    uint8_t sat      = 0; // Saturation for alive cells
    uint8_t val_dead = 0; // Brightness for dead cells

    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (changed_grid[y][x]) { // Only update changed cells
                uint16_t left   = x * (CELL_SIZE + OUTLINE_SIZE);
                uint16_t top    = y * (CELL_SIZE + OUTLINE_SIZE);
                uint16_t right  = left + CELL_SIZE + OUTLINE_SIZE;
                uint16_t bottom = top + CELL_SIZE + OUTLINE_SIZE;

                // Draw the outline
                qp_rect(lcd_surface, left, top, right, bottom, hue, sat, val_dead, true);

                // Draw the filled cell inside the outline if it's alive
                if (grid[y][x]) {
                    switch (color_value) {
                        case 0:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_0, true);
                            break;
                        case 1:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_1, true);
                            break;
                        case 2:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_2, true);
                            break;
                        case 3:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_3, true);
                            break;
                        case 4:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_4, true);
                            break;
                        case 5:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_5, true);
                            break;
                        case 6:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_6, true);
                            break;
                        case 7:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_7, true);
                            break;
                        default:
                            qp_rect(lcd_surface, left + OUTLINE_SIZE, top + OUTLINE_SIZE, right - OUTLINE_SIZE, bottom - OUTLINE_SIZE, HSV_LAYER_UNDEF, true);
                    }
                }
            }
        }
    }
}

void update_grid() {
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int alive_neighbors = 0;

            // Count alive neighbors
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dy == 0 && dx == 0) continue; // Skip the current cell
                    int ny = y + dy;
                    int nx = x + dx;
                    if (ny >= 0 && ny < GRID_HEIGHT && nx >= 0 && nx < GRID_WIDTH) {
                        alive_neighbors += grid[ny][nx];
                    }
                }
            }

            // Apply the rules of the Game of Life
            if (grid[y][x]) {
                // Any live cell with two or three live neighbours survives.
                new_grid[y][x] = (alive_neighbors == 2 || alive_neighbors == 3);
            } else {
                // Any dead cell with exactly three live neighbours becomes a live cell.
                new_grid[y][x] = (alive_neighbors == 3);
            }

            // Track changed cells
            changed_grid[y][x] = (grid[y][x] != new_grid[y][x]);
        }
    }

    // Copy new grid state to current grid
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            grid[y][x] = new_grid[y][x];
        }
    }
}

// Function to add a cluster of cells at a random position
void add_cell_cluster() {
    int cluster_size = 3; // Size of the cluster (3x3)
    int x            = rand() % (GRID_WIDTH - cluster_size);
    int y            = rand() % (GRID_HEIGHT - cluster_size);

    for (int dy = 0; dy < cluster_size; dy++) {
        for (int dx = 0; dx < cluster_size; dx++) {
            bool is_alive                = rand() % 2; // Randomly choose between 0 and 1
            grid[y + dy][x + dx]         = is_alive;   // Set the cell to be alive
            changed_grid[y + dy][x + dx] = true;       // Mark the cell as changed
        }
    }
}

void update_display(void) {
    static bool first_run_led   = false;
    static bool first_run_layer = false;

    if (first_run_layer == false) {
        // Load fonts
        sixtyfour          = qp_load_font_mem(font_sixtyfour);
        Retron27           = qp_load_font_mem(font_Retron2000_27);
        Retron27_underline = qp_load_font_mem(font_Retron2000_underline_27);
    }

    if (last_led_usb_state.raw != host_keyboard_led_state().raw || first_run_led == false) {
        led_t led_usb_state = host_keyboard_led_state();

        led_usb_state.caps_lock ? qp_drawtext_recolor(lcd_surface, 5, LCD_HEIGHT - Retron27->line_height * 2 - 10, Retron27_underline, caps, HSV_CAPS_ON, HSV_BLACK) : qp_drawtext_recolor(lcd_surface, 5, LCD_HEIGHT - Retron27->line_height * 2 - 10, Retron27, caps, HSV_CAPS_OFF, HSV_BLACK);

        last_led_usb_state = led_usb_state;
        first_run_led      = true;
    }

    int wpm_height = LCD_HEIGHT - Retron27->line_height - 5;
    qp_drawtext_recolor(lcd_surface, 5, wpm_height, Retron27, wpm, HSV_WPM_TEXT, HSV_BLACK);
    char wpm_str[4] = {0};
    int  wpm_value  = get_current_wpm();
    if (wpm_value > 999) {
        wpm_value = 999; // Cap WPM at 999
    }
    snprintf(wpm_str, sizeof(wpm_str), "%03d", wpm_value);
    qp_drawtext_recolor(lcd_surface, 5 + qp_textwidth(Retron27, wpm), wpm_height, Retron27, wpm_str, HSV_WPM_NUM, HSV_BLACK);

    if (last_layer_state != layer_state || first_run_layer == false) {
        lateral_scroll  = 5;
        animation_ended = false;

        switch (get_highest_layer(layer_state | default_layer_state)) {
            case _QWERTY:
                strcpy(currentLayer, qwerty);
                memcpy(currentColor, (int[]){HSV_LAYER_0}, sizeof(currentColor));
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, qwerty, HSV_LAYER_0, HSV_BLACK);
                break;
            case _DVORAK:
                strcpy(currentLayer, dvorak);
                memcpy(currentColor, (int[]){HSV_LAYER_1}, sizeof(currentColor));
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, dvorak, HSV_LAYER_1, HSV_BLACK);
                break;
            case _NAV:
                strcpy(currentLayer, nav);
                memcpy(currentColor, (int[]){HSV_LAYER_2}, sizeof(currentColor));
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, nav, HSV_LAYER_2, HSV_BLACK);
                break;
            case _SYM:
                strcpy(currentLayer, sym);
                memcpy(currentColor, (int[]){HSV_LAYER_3}, sizeof(currentColor));
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, sym, HSV_LAYER_3, HSV_BLACK);
                break;
            case _FUNCTION:
                strcpy(currentLayer, func);
                memcpy(currentColor, (int[]){HSV_LAYER_4}, sizeof(currentColor));
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, func, HSV_LAYER_4, HSV_BLACK);
                break;
            case _ADJUST:
                strcpy(currentLayer, adj);
                memcpy(currentColor, (int[]){HSV_LAYER_5}, sizeof(currentColor));
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, adj, HSV_LAYER_5, HSV_BLACK);
                break;
            default:
                qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, undef, HSV_LAYER_UNDEF, HSV_BLACK);
        }
        last_layer_state = layer_state;
        first_run_layer  = true;
    } else {
        if (!animation_ended && timer_elapsed32(last_scroll_update) >= scroll_update_interval) {
            lateral_scroll -= 5;
            last_scroll_update = timer_read32();

            if (lateral_scroll < -qp_textwidth(sixtyfour, currentLayer)) {
                animation_ended = true;
                lateral_scroll  = 5;
            }

            qp_rect(lcd_surface, 0, 0, LCD_WIDTH - 1, 10 + sixtyfour->line_height, HSV_BLACK, true);

            qp_drawtext_recolor(lcd_surface, lateral_scroll, 5, sixtyfour, currentLayer, currentColor[0], currentColor[1], currentColor[2], HSV_BLACK);
        }
    }
}

void game_of_life(void) {
    static uint32_t last_draw                     = 0;
    static bool     second_display_set            = false;
    static uint32_t previous_matrix_activity_time = 0;

    if (!second_display_set) {
        srand(get_random_32bit());
        init_grid();
        color_value        = rand() % 8;
        second_display_set = true;
    }

    if (timer_elapsed32(last_draw) >= 100) { // Throttle to 10 fps
        draw_grid();
        update_grid();

        if (previous_matrix_activity_time != last_matrix_activity_time()) {
            color_value = rand() % 8;
            add_cell_cluster();
            previous_matrix_activity_time = last_matrix_activity_time();
        }

        last_draw = timer_read32();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//                           halcyon.c CALLBACKS
// ═══════════════════════════════════════════════════════════════════════════
// power management
void module_suspend_power_down_kb(void) {
    qp_power(lcd, false);
}

void module_suspend_wakeup_init_kb(void) {
    qp_power(lcd, true);
}

// initialization callback
bool module_post_init_kb(void) {
    // Turn on backlight
    backlight_enable();

    // Make the devices
    lcd         = qp_st7789_make_spi_device(LCD_WIDTH, LCD_HEIGHT, LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_SPI_DIVISOR, LCD_SPI_MODE);
    lcd_surface = qp_make_rgb565_surface(LCD_WIDTH, LCD_HEIGHT, lcd_surface_fb);

    // Initialise the LCD
    qp_init(lcd, LCD_ROTATION);
    qp_set_viewport_offsets(lcd, LCD_OFFSET_X, LCD_OFFSET_Y);
    qp_clear(lcd);
    qp_rect(lcd, 0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, HSV_BLACK, true);
    qp_power(lcd, true);
    qp_flush(lcd);

    // Initialise the LCD surface
    qp_init(lcd_surface, LCD_ROTATION);
    qp_rect(lcd_surface, 0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, HSV_BLACK, true);
    qp_surface_draw(lcd_surface, lcd, 0, 0, 0);
    qp_flush(lcd);

    if (!module_post_init_user()) {
        return false;
    }

    return true;
}

// main loop
bool display_module_housekeeping_task_kb(bool second_display) {
    if (!display_module_housekeeping_task_user(second_display)) {
        return false;
    }

    if (second_display) {
        game_of_life();
    }

    // Update display information (layers, numlock, etc.)
    if (!second_display) {
        update_display();
    }

    // Move surface to lcd
    qp_surface_draw(lcd_surface, lcd, 0, 0, 0);
    qp_flush(lcd);

    return true;
}
