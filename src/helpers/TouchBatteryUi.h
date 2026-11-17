#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Standard LVGL / FontAwesome UTF-8 battery and charge symbols (mapped in all
// Montserrat 12/14/16+ faces across every Touch UI board).
// Note: scripts/build/lucide_font.py maps U+F240 -> battery-full, U+F241/U+F242 -> battery-medium,
// U+F243 -> battery-low, U+F244 -> battery (empty), and U+F0E7 -> zap.
#ifndef TOUCH_BATT_SYM_FULL
#define TOUCH_BATT_SYM_FULL   "\xEF\x89\x80"  /* U+F240 LV_SYMBOL_BATTERY_FULL  */
#define TOUCH_BATT_SYM_3      "\xEF\x89\x81"  /* U+F241 LV_SYMBOL_BATTERY_3     */
#define TOUCH_BATT_SYM_2      "\xEF\x89\x82"  /* U+F242 LV_SYMBOL_BATTERY_2     */
#define TOUCH_BATT_SYM_1      "\xEF\x89\x83"  /* U+F243 LV_SYMBOL_BATTERY_1     */
#define TOUCH_BATT_SYM_EMPTY  "\xEF\x89\x84"  /* U+F244 LV_SYMBOL_BATTERY_EMPTY */
#define TOUCH_BATT_SYM_CHARGE "\xEF\x83\xA7"  /* U+F0E7 LV_SYMBOL_CHARGE (bolt) */
#endif

enum TouchBatteryBand : uint8_t {
  TOUCH_BATT_BAND_UNKNOWN = 0,
  TOUCH_BATT_BAND_RED     = 1,  // 0..19%  (low / critical)
  TOUCH_BATT_BAND_ORANGE  = 2,  // 20..49% (medium / warning)
  TOUCH_BATT_BAND_GREEN   = 3,  // 50..100% (healthy / high)
};

// Map battery percentage (-1 if unknown, 0..100 otherwise) to Green / Orange / Red band.
static inline TouchBatteryBand touchBatteryBandForPct(int pct) {
  if (pct < 0)  return TOUCH_BATT_BAND_UNKNOWN;
  if (pct < 20) return TOUCH_BATT_BAND_RED;
  if (pct < 50) return TOUCH_BATT_BAND_ORANGE;
  return TOUCH_BATT_BAND_GREEN;
}

// Resolve 24-bit RGB hex color for a battery percentage, preserving the Green / Orange / Red
// level color whether charging or discharging. If pct < 0 while charging, falls back to green.
static inline uint32_t touchBatteryColorForPct(int pct, bool charging,
                                               uint32_t green_rgb,
                                               uint32_t orange_rgb,
                                               uint32_t red_rgb,
                                               uint32_t unknown_rgb) {
  switch (touchBatteryBandForPct(pct)) {
    case TOUCH_BATT_BAND_GREEN:   return green_rgb;
    case TOUCH_BATT_BAND_ORANGE:  return orange_rgb;
    case TOUCH_BATT_BAND_RED:     return red_rgb;
    case TOUCH_BATT_BAND_UNKNOWN: return charging ? green_rgb : unknown_rgb;
  }
  return unknown_rgb;
}

// Return the distinct battery level glyph for the given percentage, even while charging,
// using the 4 visually distinct glyphs in the generated touch fonts:
//   >= 75% : FULL (3 bars)
//   40..74%: MEDIUM (2 bars)
//   15..39%: LOW (1 bar)
//   <  15% : EMPTY (0 bars)
static inline const char* touchBatteryLevelGlyphForPct(int pct) {
  if (pct >= 75) return TOUCH_BATT_SYM_FULL;
  if (pct >= 40) return TOUCH_BATT_SYM_2;
  if (pct >= 15) return TOUCH_BATT_SYM_1;
  return TOUCH_BATT_SYM_EMPTY;
}

// Format the status-bar percentage text:
// - Discharging: "79%" (or "?" if unknown)
// - Charging:    "<bolt> 79%" (or "<bolt>" if unknown)
static inline void touchBatteryFormatStatusPct(char* out, size_t cap, int pct, bool charging) {
  if (!out || cap == 0) return;
  if (charging) {
    if (pct >= 0) snprintf(out, cap, TOUCH_BATT_SYM_CHARGE " %d%%", pct);
    else          snprintf(out, cap, "%s", TOUCH_BATT_SYM_CHARGE);
  } else {
    if (pct >= 0) snprintf(out, cap, "%d%%", pct);
    else          snprintf(out, cap, "?");
  }
}

// Format a combined "[<bolt> ]<battery-level-glyph>" string for icon-only surfaces.
static inline void touchBatteryFormatIconWithCharge(char* out, size_t cap, int pct, bool charging) {
  if (!out || cap == 0) return;
  const char* level_glyph = touchBatteryLevelGlyphForPct(pct);
  if (charging) snprintf(out, cap, TOUCH_BATT_SYM_CHARGE " %s", level_glyph);
  else          snprintf(out, cap, "%s", level_glyph);
}
