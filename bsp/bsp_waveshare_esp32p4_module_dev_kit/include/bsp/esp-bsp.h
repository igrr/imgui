/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP BSP: Waveshare ESP32-P4 Module Dev Kit
 */

#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "bsp/display.h"

/**************************************************************************************************
 *  BSP Capabilities
 **************************************************************************************************/
#define BSP_CAPS_DISPLAY        1
#define BSP_CAPS_TOUCH          0

/**************************************************************************************************
 *  Pinout
 **************************************************************************************************/
#define BSP_I2C_SCL           (GPIO_NUM_8)
#define BSP_I2C_SDA           (GPIO_NUM_7)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init I2C driver
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *      - ESP_FAIL              I2C driver installation error
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Get I2C driver handle
 *
 * @return I2C bus handle
 */
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

#ifdef __cplusplus
}
#endif
