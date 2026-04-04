
#include <LiquidCrystal.h>

/**
vss = -
vdd = +
rw = grnd
d0-d3 = float
V0 = 4k (leaning low)
a = +
k = -
*/
LiquidCrystal lcd(3, 5, 10, 11, 12, 24); // RS, E, D4, D5, D6, D7 - remember to shift values

void setup() {
  delay(200);
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Hello");
  lcd.setCursor(0,1);
  lcd.print("Teensy 4.1");
}
