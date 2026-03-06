/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_ili9881c.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

static const char *TAG = "bsp_waveshare";

static bool s_i2c_initialized = false;
static i2c_master_bus_handle_t s_i2c_handle = NULL;
static esp_ldo_channel_handle_t s_disp_phy_pwr_chan = NULL;
static bsp_lcd_handles_t s_disp_handles;

/* -------------------------------------------------------------------------- */
/*  I2C                                                                       */
/* -------------------------------------------------------------------------- */

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_initialized) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = -1,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &s_i2c_handle), TAG, "I2C bus init failed");
    s_i2c_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (s_i2c_initialized && s_i2c_handle) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_i2c_handle), TAG, "I2C bus deinit failed");
        s_i2c_handle = NULL;
        s_i2c_initialized = false;
    }
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    return s_i2c_handle;
}

/* -------------------------------------------------------------------------- */
/*  Board-specific display power sequencing                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Power-on sequence for the Waveshare 7" DSI display.
 *
 * Communicates with an I2C device at address 0x45 (likely a PMIC or IO
 * expander on the Waveshare carrier board) to enable display power and
 * toggle the display reset line.
 */
static esp_err_t bsp_display_power_on(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x45,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_handle, &dev_cfg, &dev_handle),
                        TAG, "Add I2C device 0x45 failed");

    /* Each transmit sends {register, value} */
    const uint8_t data[] = {
        0x95, 0x11,
        0x95, 0x17,
        0x96, 0x00,
        0x96, 0xFF,
    };

    esp_err_t ret = ESP_OK;
    /* Enable power rail */
    ESP_GOTO_ON_ERROR(i2c_master_transmit(dev_handle, &data[0], 2, 1000), cleanup, TAG, "I2C tx failed");
    ESP_GOTO_ON_ERROR(i2c_master_transmit(dev_handle, &data[2], 2, 1000), cleanup, TAG, "I2C tx failed");
    /* Toggle reset: low */
    ESP_GOTO_ON_ERROR(i2c_master_transmit(dev_handle, &data[4], 2, 1000), cleanup, TAG, "I2C tx failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    /* Toggle reset: high */
    ESP_GOTO_ON_ERROR(i2c_master_transmit(dev_handle, &data[6], 2, 1000), cleanup, TAG, "I2C tx failed");

cleanup:
    i2c_master_bus_rm_device(dev_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Display power-on sequence failed");

    /* Wait for the display to become ready after reset */
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Display power-on sequence complete");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*  Waveshare-specific ILI9881C init commands (720x1280 panel)                */
/* -------------------------------------------------------------------------- */

static const ili9881c_lcd_init_cmd_t s_waveshare_init_cmds[] = {
    /* CMD_Page 3 */
    {0xFF, (uint8_t []){0x98, 0x81, 0x03}, 3, 0},
    {0x01, (uint8_t []){0x00}, 1, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0x03, (uint8_t []){0x73}, 1, 0},
    {0x04, (uint8_t []){0x00}, 1, 0},
    {0x05, (uint8_t []){0x00}, 1, 0},
    {0x06, (uint8_t []){0x0A}, 1, 0},
    {0x07, (uint8_t []){0x00}, 1, 0},
    {0x08, (uint8_t []){0x00}, 1, 0},
    {0x09, (uint8_t []){0x61}, 1, 0},
    {0x0A, (uint8_t []){0x00}, 1, 0},
    {0x0B, (uint8_t []){0x00}, 1, 0},
    {0x0C, (uint8_t []){0x01}, 1, 0},
    {0x0D, (uint8_t []){0x00}, 1, 0},
    {0x0E, (uint8_t []){0x00}, 1, 0},
    {0x0F, (uint8_t []){0x61}, 1, 0},
    {0x10, (uint8_t []){0x61}, 1, 0},
    {0x11, (uint8_t []){0x00}, 1, 0},
    {0x12, (uint8_t []){0x00}, 1, 0},
    {0x13, (uint8_t []){0x00}, 1, 0},
    {0x14, (uint8_t []){0x00}, 1, 0},
    {0x15, (uint8_t []){0x00}, 1, 0},
    {0x16, (uint8_t []){0x00}, 1, 0},
    {0x17, (uint8_t []){0x00}, 1, 0},
    {0x18, (uint8_t []){0x00}, 1, 0},
    {0x19, (uint8_t []){0x00}, 1, 0},
    {0x1A, (uint8_t []){0x00}, 1, 0},
    {0x1B, (uint8_t []){0x00}, 1, 0},
    {0x1C, (uint8_t []){0x00}, 1, 0},
    {0x1D, (uint8_t []){0x00}, 1, 0},
    {0x1E, (uint8_t []){0x40}, 1, 0},
    {0x1F, (uint8_t []){0x80}, 1, 0},
    {0x20, (uint8_t []){0x06}, 1, 0},
    {0x21, (uint8_t []){0x01}, 1, 0},
    {0x22, (uint8_t []){0x00}, 1, 0},
    {0x23, (uint8_t []){0x00}, 1, 0},
    {0x24, (uint8_t []){0x00}, 1, 0},
    {0x25, (uint8_t []){0x00}, 1, 0},
    {0x26, (uint8_t []){0x00}, 1, 0},
    {0x27, (uint8_t []){0x00}, 1, 0},
    {0x28, (uint8_t []){0x33}, 1, 0},
    {0x29, (uint8_t []){0x03}, 1, 0},
    {0x2A, (uint8_t []){0x00}, 1, 0},
    {0x2B, (uint8_t []){0x00}, 1, 0},
    {0x2C, (uint8_t []){0x00}, 1, 0},
    {0x2D, (uint8_t []){0x00}, 1, 0},
    {0x2E, (uint8_t []){0x00}, 1, 0},
    {0x2F, (uint8_t []){0x00}, 1, 0},
    {0x30, (uint8_t []){0x00}, 1, 0},
    {0x31, (uint8_t []){0x00}, 1, 0},
    {0x32, (uint8_t []){0x00}, 1, 0},
    {0x33, (uint8_t []){0x00}, 1, 0},
    {0x34, (uint8_t []){0x04}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x37, (uint8_t []){0x00}, 1, 0},
    {0x38, (uint8_t []){0x3C}, 1, 0},
    {0x39, (uint8_t []){0x00}, 1, 0},
    {0x3A, (uint8_t []){0x00}, 1, 0},
    {0x3B, (uint8_t []){0x00}, 1, 0},
    {0x3C, (uint8_t []){0x00}, 1, 0},
    {0x3D, (uint8_t []){0x00}, 1, 0},
    {0x3E, (uint8_t []){0x00}, 1, 0},
    {0x3F, (uint8_t []){0x00}, 1, 0},
    {0x40, (uint8_t []){0x00}, 1, 0},
    {0x41, (uint8_t []){0x00}, 1, 0},
    {0x42, (uint8_t []){0x00}, 1, 0},
    {0x43, (uint8_t []){0x00}, 1, 0},
    {0x44, (uint8_t []){0x00}, 1, 0},
    {0x50, (uint8_t []){0x10}, 1, 0},
    {0x51, (uint8_t []){0x32}, 1, 0},
    {0x52, (uint8_t []){0x54}, 1, 0},
    {0x53, (uint8_t []){0x76}, 1, 0},
    {0x54, (uint8_t []){0x98}, 1, 0},
    {0x55, (uint8_t []){0xBA}, 1, 0},
    {0x56, (uint8_t []){0x10}, 1, 0},
    {0x57, (uint8_t []){0x32}, 1, 0},
    {0x58, (uint8_t []){0x54}, 1, 0},
    {0x59, (uint8_t []){0x76}, 1, 0},
    {0x5A, (uint8_t []){0x98}, 1, 0},
    {0x5B, (uint8_t []){0xBA}, 1, 0},
    {0x5C, (uint8_t []){0xDC}, 1, 0},
    {0x5D, (uint8_t []){0xFE}, 1, 0},
    {0x5E, (uint8_t []){0x00}, 1, 0},
    {0x5F, (uint8_t []){0x0E}, 1, 0},
    {0x60, (uint8_t []){0x0F}, 1, 0},
    {0x61, (uint8_t []){0x0C}, 1, 0},
    {0x62, (uint8_t []){0x0D}, 1, 0},
    {0x63, (uint8_t []){0x06}, 1, 0},
    {0x64, (uint8_t []){0x07}, 1, 0},
    {0x65, (uint8_t []){0x02}, 1, 0},
    {0x66, (uint8_t []){0x02}, 1, 0},
    {0x67, (uint8_t []){0x02}, 1, 0},
    {0x68, (uint8_t []){0x02}, 1, 0},
    {0x69, (uint8_t []){0x01}, 1, 0},
    {0x6A, (uint8_t []){0x00}, 1, 0},
    {0x6B, (uint8_t []){0x02}, 1, 0},
    {0x6C, (uint8_t []){0x15}, 1, 0},
    {0x6D, (uint8_t []){0x14}, 1, 0},
    {0x6E, (uint8_t []){0x02}, 1, 0},
    {0x6F, (uint8_t []){0x02}, 1, 0},
    {0x70, (uint8_t []){0x02}, 1, 0},
    {0x71, (uint8_t []){0x02}, 1, 0},
    {0x72, (uint8_t []){0x02}, 1, 0},
    {0x73, (uint8_t []){0x02}, 1, 0},
    {0x74, (uint8_t []){0x02}, 1, 0},
    {0x75, (uint8_t []){0x0E}, 1, 0},
    {0x76, (uint8_t []){0x0F}, 1, 0},
    {0x77, (uint8_t []){0x0C}, 1, 0},
    {0x78, (uint8_t []){0x0D}, 1, 0},
    {0x79, (uint8_t []){0x06}, 1, 0},
    {0x7A, (uint8_t []){0x07}, 1, 0},
    {0x7B, (uint8_t []){0x02}, 1, 0},
    {0x7C, (uint8_t []){0x02}, 1, 0},
    {0x7D, (uint8_t []){0x02}, 1, 0},
    {0x7E, (uint8_t []){0x02}, 1, 0},
    {0x7F, (uint8_t []){0x01}, 1, 0},
    {0x80, (uint8_t []){0x00}, 1, 0},
    {0x81, (uint8_t []){0x02}, 1, 0},
    {0x82, (uint8_t []){0x14}, 1, 0},
    {0x83, (uint8_t []){0x15}, 1, 0},
    {0x84, (uint8_t []){0x02}, 1, 0},
    {0x85, (uint8_t []){0x02}, 1, 0},
    {0x86, (uint8_t []){0x02}, 1, 0},
    {0x87, (uint8_t []){0x02}, 1, 0},
    {0x88, (uint8_t []){0x02}, 1, 0},
    {0x89, (uint8_t []){0x02}, 1, 0},
    {0x8A, (uint8_t []){0x02}, 1, 0},

    /* CMD_Page 4 */
    {0xFF, (uint8_t []){0x98, 0x81, 0x04}, 3, 0},
    {0x38, (uint8_t []){0x01}, 1, 0},
    {0x39, (uint8_t []){0x00}, 1, 0},
    {0x6C, (uint8_t []){0x15}, 1, 0},
    {0x6E, (uint8_t []){0x2A}, 1, 0},
    {0x6F, (uint8_t []){0x33}, 1, 0},
    {0x3A, (uint8_t []){0x94}, 1, 0},
    {0x8D, (uint8_t []){0x14}, 1, 0},
    {0x87, (uint8_t []){0xBA}, 1, 0},
    {0x26, (uint8_t []){0x76}, 1, 0},
    {0xB2, (uint8_t []){0xD1}, 1, 0},
    {0xB5, (uint8_t []){0x06}, 1, 0},
    {0x3B, (uint8_t []){0x98}, 1, 0},

    /* CMD_Page 1 */
    {0xFF, (uint8_t []){0x98, 0x81, 0x01}, 3, 0},
    {0x22, (uint8_t []){0x0A}, 1, 0},
    {0x31, (uint8_t []){0x00}, 1, 0},
    {0x53, (uint8_t []){0x71}, 1, 0},
    {0x55, (uint8_t []){0x8F}, 1, 0},
    {0x40, (uint8_t []){0x33}, 1, 0},
    {0x50, (uint8_t []){0x96}, 1, 0},
    {0x51, (uint8_t []){0x96}, 1, 0},
    {0x60, (uint8_t []){0x23}, 1, 0},
    /* Positive gamma */
    {0xA0, (uint8_t []){0x08}, 1, 0},
    {0xA1, (uint8_t []){0x1D}, 1, 0},
    {0xA2, (uint8_t []){0x2A}, 1, 0},
    {0xA3, (uint8_t []){0x10}, 1, 0},
    {0xA4, (uint8_t []){0x15}, 1, 0},
    {0xA5, (uint8_t []){0x28}, 1, 0},
    {0xA6, (uint8_t []){0x1C}, 1, 0},
    {0xA7, (uint8_t []){0x1D}, 1, 0},
    {0xA8, (uint8_t []){0x7E}, 1, 0},
    {0xA9, (uint8_t []){0x1D}, 1, 0},
    {0xAA, (uint8_t []){0x29}, 1, 0},
    {0xAB, (uint8_t []){0x6B}, 1, 0},
    {0xAC, (uint8_t []){0x1A}, 1, 0},
    {0xAD, (uint8_t []){0x18}, 1, 0},
    {0xAE, (uint8_t []){0x4B}, 1, 0},
    {0xAF, (uint8_t []){0x20}, 1, 0},
    {0xB0, (uint8_t []){0x27}, 1, 0},
    {0xB1, (uint8_t []){0x50}, 1, 0},
    {0xB2, (uint8_t []){0x64}, 1, 0},
    {0xB3, (uint8_t []){0x39}, 1, 0},
    /* Negative gamma */
    {0xC0, (uint8_t []){0x08}, 1, 0},
    {0xC1, (uint8_t []){0x1D}, 1, 0},
    {0xC2, (uint8_t []){0x2A}, 1, 0},
    {0xC3, (uint8_t []){0x10}, 1, 0},
    {0xC4, (uint8_t []){0x15}, 1, 0},
    {0xC5, (uint8_t []){0x28}, 1, 0},
    {0xC6, (uint8_t []){0x1C}, 1, 0},
    {0xC7, (uint8_t []){0x1D}, 1, 0},
    {0xC8, (uint8_t []){0x7E}, 1, 0},
    {0xC9, (uint8_t []){0x1D}, 1, 0},
    {0xCA, (uint8_t []){0x29}, 1, 0},
    {0xCB, (uint8_t []){0x6B}, 1, 0},
    {0xCC, (uint8_t []){0x1A}, 1, 0},
    {0xCD, (uint8_t []){0x18}, 1, 0},
    {0xCE, (uint8_t []){0x4B}, 1, 0},
    {0xCF, (uint8_t []){0x20}, 1, 0},
    {0xD0, (uint8_t []){0x27}, 1, 0},
    {0xD1, (uint8_t []){0x50}, 1, 0},
    {0xD2, (uint8_t []){0x64}, 1, 0},
    {0xD3, (uint8_t []){0x39}, 1, 0},

    /* CMD_Page 0 */
    {0xFF, (uint8_t []){0x98, 0x81, 0x00}, 3, 0},
    {0x3A, (uint8_t []){0x77}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 150},
    {0x29, (uint8_t []){0x00}, 0, 20},
};

/* -------------------------------------------------------------------------- */
/*  Display init / deinit                                                     */
/* -------------------------------------------------------------------------- */

esp_err_t bsp_display_new(bsp_lcd_handles_t *ret_handles)
{
    ESP_RETURN_ON_FALSE(ret_handles, ESP_ERR_INVALID_ARG, TAG, "ret_handles is NULL");

    /* 1. Power on MIPI DSI PHY via LDO */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_disp_phy_pwr_chan),
                        TAG, "Acquire LDO channel for DPHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY powered on");

    /* 2. Board-specific I2C power sequencing */
    ESP_RETURN_ON_ERROR(bsp_display_power_on(), TAG, "Display power-on failed");

    /* 3. Create MIPI DSI bus */
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = 0,
        .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus),
                        TAG, "New DSI bus failed");

    /* 4. Create panel IO (DBI) */
    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io),
                        TAG, "New panel IO DBI failed");

    /* 5. Create ILI9881C panel with Waveshare init commands and 720x1280 timing */
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 80,
        .virtual_channel = 0,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = 1,
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_back_porch = 239,
            .hsync_pulse_width = 50,
            .hsync_front_porch = 33,
            .vsync_back_porch = 20,
            .vsync_pulse_width = 30,
            .vsync_front_porch = 2,
        },
    };

    ili9881c_vendor_config_t vendor_config = {
        .init_cmds = s_waveshare_init_cmds,
        .init_cmds_size = sizeof(s_waveshare_init_cmds) / sizeof(s_waveshare_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = BSP_LCD_MIPI_DSI_LANE_NUM,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9881c(io, &panel_config, &panel),
                        TAG, "New ILI9881C panel failed");

    /* 6. Enable DMA2D, reset, init, turn on */
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(panel), TAG, "Enable DMA2D failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "Panel ON failed");

    /* 7. Store and return handles */
    s_disp_handles.mipi_dsi_bus = mipi_dsi_bus;
    s_disp_handles.io = io;
    s_disp_handles.panel = panel;

    *ret_handles = s_disp_handles;

    ESP_LOGI(TAG, "Display initialized (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

void bsp_display_delete(void)
{
    if (s_disp_handles.panel) {
        esp_lcd_panel_del(s_disp_handles.panel);
        s_disp_handles.panel = NULL;
    }
    if (s_disp_handles.io) {
        esp_lcd_panel_io_del(s_disp_handles.io);
        s_disp_handles.io = NULL;
    }
    if (s_disp_handles.mipi_dsi_bus) {
        esp_lcd_del_dsi_bus(s_disp_handles.mipi_dsi_bus);
        s_disp_handles.mipi_dsi_bus = NULL;
    }
    if (s_disp_phy_pwr_chan) {
        esp_ldo_release_channel(s_disp_phy_pwr_chan);
        s_disp_phy_pwr_chan = NULL;
    }
}

#endif /* SOC_MIPI_DSI_SUPPORTED */
