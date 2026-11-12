// Build: g++ -std=c++17 -I src -o /tmp/test_touch_battery_ui test/test_touch_battery_ui.cpp && /tmp/test_touch_battery_ui
#include <cassert>
#include <cstring>
#include "helpers/TouchBatteryUi.h"

int main() {
  constexpr uint32_t kGreen   = 0x4CC38Au;
  constexpr uint32_t kOrange  = 0xF2A33Au;
  constexpr uint32_t kRed     = 0xF06A6Eu;
  constexpr uint32_t kUnknown = 0x8A969Cu;

  // 1. Battery level color bands: Red (< 20%), Orange (20..49%), Green (>= 50%), Unknown (< 0)
  assert(touchBatteryBandForPct(-1)  == TOUCH_BATT_BAND_UNKNOWN);
  assert(touchBatteryBandForPct(0)   == TOUCH_BATT_BAND_RED);
  assert(touchBatteryBandForPct(19)  == TOUCH_BATT_BAND_RED);
  assert(touchBatteryBandForPct(20)  == TOUCH_BATT_BAND_ORANGE);
  assert(touchBatteryBandForPct(49)  == TOUCH_BATT_BAND_ORANGE);
  assert(touchBatteryBandForPct(50)  == TOUCH_BATT_BAND_GREEN);
  assert(touchBatteryBandForPct(79)  == TOUCH_BATT_BAND_GREEN);
  assert(touchBatteryBandForPct(100) == TOUCH_BATT_BAND_GREEN);

  // 2. Color selection preserves Green/Orange/Red both when discharging and when charging
  const bool chgs[] = { false, true };
  for (bool chg : chgs) {
    assert(touchBatteryColorForPct(5,   chg, kGreen, kOrange, kRed, kUnknown) == kRed);
    assert(touchBatteryColorForPct(19,  chg, kGreen, kOrange, kRed, kUnknown) == kRed);
    assert(touchBatteryColorForPct(20,  chg, kGreen, kOrange, kRed, kUnknown) == kOrange);
    assert(touchBatteryColorForPct(35,  chg, kGreen, kOrange, kRed, kUnknown) == kOrange);
    assert(touchBatteryColorForPct(49,  chg, kGreen, kOrange, kRed, kUnknown) == kOrange);
    assert(touchBatteryColorForPct(50,  chg, kGreen, kOrange, kRed, kUnknown) == kGreen);
    assert(touchBatteryColorForPct(79,  chg, kGreen, kOrange, kRed, kUnknown) == kGreen);
    assert(touchBatteryColorForPct(100, chg, kGreen, kOrange, kRed, kUnknown) == kGreen);
  }
  assert(touchBatteryColorForPct(-1, false, kGreen, kOrange, kRed, kUnknown) == kUnknown);
  assert(touchBatteryColorForPct(-1, true,  kGreen, kOrange, kRed, kUnknown) == kGreen);

  // 3. 5-step solid FontAwesome battery level glyphs (never replaced by empty outline when charging)
  assert(std::strcmp(touchBatteryLevelGlyphForPct(-1),  TOUCH_BATT_SYM_EMPTY) == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(0),   TOUCH_BATT_SYM_EMPTY) == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(14),  TOUCH_BATT_SYM_EMPTY) == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(15),  TOUCH_BATT_SYM_1)     == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(39),  TOUCH_BATT_SYM_1)     == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(40),  TOUCH_BATT_SYM_2)     == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(64),  TOUCH_BATT_SYM_2)     == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(65),  TOUCH_BATT_SYM_3)     == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(89),  TOUCH_BATT_SYM_3)     == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(90),  TOUCH_BATT_SYM_FULL)  == 0);
  assert(std::strcmp(touchBatteryLevelGlyphForPct(100), TOUCH_BATT_SYM_FULL)  == 0);

  // 4. Unambiguous charging vs not-charging formatting for status bar % and combined icon
  char buf[16];
  touchBatteryFormatStatusPct(buf, sizeof(buf), 79, false);
  assert(std::strcmp(buf, "79%") == 0);

  touchBatteryFormatStatusPct(buf, sizeof(buf), 79, true);
  assert(std::strcmp(buf, TOUCH_BATT_SYM_CHARGE " 79%") == 0);

  touchBatteryFormatStatusPct(buf, sizeof(buf), -1, false);
  assert(std::strcmp(buf, "?") == 0);

  touchBatteryFormatStatusPct(buf, sizeof(buf), -1, true);
  assert(std::strcmp(buf, TOUCH_BATT_SYM_CHARGE) == 0);

  touchBatteryFormatIconWithCharge(buf, sizeof(buf), 79, false);
  assert(std::strcmp(buf, TOUCH_BATT_SYM_3) == 0);

  touchBatteryFormatIconWithCharge(buf, sizeof(buf), 79, true);
  assert(std::strcmp(buf, TOUCH_BATT_SYM_CHARGE " " TOUCH_BATT_SYM_3) == 0);

  return 0;
}
