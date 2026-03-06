# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0
#
# project_include.cmake — automatically included by ESP-IDF for every project
# that depends on the imgui component. Provides imgui_font_add().

# Remember the imgui component directory at include time so the function
# can locate the compression script regardless of caller context.
set(_IMGUI_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Get the Python interpreter from ESP-IDF build properties.
idf_build_get_property(_IMGUI_PYTHON PYTHON)

# Path to the bundled ImGui font files, for use with imgui_font_add().
set(IMGUI_FONTS_DIR "${_IMGUI_COMPONENT_DIR}/imgui/misc/fonts")

# imgui_font_add(<ttf_file> [SYMBOL <name>])
#
# Convert a TTF font file into a compressed C source that can be loaded with
#   ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF()
#
# The generated files are placed in the current component's build directory.
# A header file named <symbol>.h is generated, declaring:
#   extern const unsigned int  <symbol>_compressed_size;
#   extern const unsigned char <symbol>_compressed_data[];
#
# Arguments:
#   <ttf_file>        Path to the .ttf file (relative to CMAKE_CURRENT_SOURCE_DIR
#                     or absolute).
#   SYMBOL <name>     C identifier prefix for the generated arrays.
#                     Defaults to the filename with dots and hyphens replaced
#                     by underscores.
#
# Example:
#   imgui_font_add(fonts/Roboto-Medium.ttf)
#   # Generates Roboto_Medium.h / Roboto_Medium.c
#
#   imgui_font_add(fonts/Roboto-Medium.ttf SYMBOL my_font)
#   # Generates my_font.h / my_font.c
#
function(imgui_font_add ttf_file)
    cmake_parse_arguments(ARG "" "SYMBOL" "" ${ARGN})

    # Resolve the input path
    if(IS_ABSOLUTE "${ttf_file}")
        set(ttf_abs "${ttf_file}")
    else()
        set(ttf_abs "${CMAKE_CURRENT_SOURCE_DIR}/${ttf_file}")
    endif()
    if(NOT EXISTS "${ttf_abs}")
        message(FATAL_ERROR "imgui_font_add: file not found: ${ttf_abs}")
    endif()

    # Derive the symbol name from the filename if not provided
    if(ARG_SYMBOL)
        set(symbol "${ARG_SYMBOL}")
    else()
        get_filename_component(fname "${ttf_file}" NAME_WE)
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" symbol "${fname}")
    endif()

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/imgui_fonts")
    set(out_c "${out_dir}/${symbol}.c")
    set(out_h "${out_dir}/${symbol}.h")

    # CMAKE_CURRENT_LIST_DIR points to the imgui component when this file
    # is first included by ESP-IDF's build system.
    set(script "${_IMGUI_COMPONENT_DIR}/tools/font_compress.py")

    file(MAKE_DIRECTORY "${out_dir}")

    add_custom_command(
        OUTPUT "${out_c}" "${out_h}"
        COMMAND "${_IMGUI_PYTHON}" "${script}" "${ttf_abs}" "${symbol}" "${out_c}" "${out_h}"
        DEPENDS "${ttf_abs}" "${script}"
        COMMENT "Compressing font ${ttf_file} -> ${symbol}"
        VERBATIM
    )

    # Add generated source to the calling component and make the header visible.
    target_sources(${COMPONENT_LIB} PRIVATE "${out_c}")
    target_include_directories(${COMPONENT_LIB} PUBLIC "${out_dir}")
endfunction()
