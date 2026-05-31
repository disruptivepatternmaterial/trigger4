/*
 * IBM Plex Sans fonts for M5GFX, generated from the OFL TTFs via Adafruit
 * fontconvert (tools/gen_fonts.sh). The generated headers use the Adafruit
 * GFXfont/GFXglyph names + PROGMEM; M5GFX (LovyanGFX) provides compatible
 * lgfx::GFXfont/lgfx::GFXglyph types, so we alias them and stub PROGMEM
 * before pulling the data in. Include this from a C++ TU only.
 *
 *   tier 1 (label/title) -> IBMPlexSans_Medium9pt7b
 *   tier 2 (value / D,P) -> IBMPlexSans_SemiBold18pt7b
 *   tier 3 (hero %)      -> IBMPlexSans_SemiBold28pt7b
 */
#pragma once

#include <M5GFX.h>

#ifndef PROGMEM
#define PROGMEM
#endif

using lgfx::GFXfont;
using lgfx::GFXglyph;

#include "ibmplex_sans_med9.h"
#include "ibmplex_sans_sb18.h"
#include "ibmplex_sans_sb28.h"
