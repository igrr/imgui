/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: MIT
 *
 * ImGui user config for ESP-IDF.
 * Selected via: -DIMGUI_USER_CONFIG="imgui_esp_config.h"
 */

#pragma once

/* Disable the default Unix/Win32 shell handler which calls fork()/execvp(),
 * neither of which is available on ESP-IDF / FreeRTOS. */
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS

