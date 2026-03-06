/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"

/* LCD display resolution */
#define BSP_LCD_H_RES              (720)
#define BSP_LCD_V_RES              (1280)

/* LCD display color */
#define BSP_LCD_BITS_PER_PIXEL     (24)
#define BSP_LCD_COLOR_SPACE        (LCD_RGB_ELEMENT_ORDER_RGB)

/* MIPI DSI */
#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1000)

#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP display return handles
 */
typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;  /*!< MIPI DSI bus handle */
    esp_lcd_panel_io_handle_t   io;            /*!< ESP LCD IO handle */
    esp_lcd_panel_handle_t      panel;         /*!< ESP LCD panel handle */
} bsp_lcd_handles_t;

/**
 * @brief Create new display panel
 *
 * Initializes MIPI DSI PHY power, DSI bus, panel IO, runs the board-specific
 * power sequencing over I2C, and creates the ILI9881C panel with Waveshare
 * init commands.
 *
 * After this call, the panel is reset, initialized and turned on.
 *
 * @param[out] ret_handles  All esp_lcd handles
 * @return
 *      - ESP_OK on success
 *      - error code on failure
 */
esp_err_t bsp_display_new(bsp_lcd_handles_t *ret_handles);

/**
 * @brief Delete display panel and free resources
 */
void bsp_display_delete(void);

#ifdef __cplusplus
}
#endif
