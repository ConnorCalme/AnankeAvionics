
#include <LiquidCrystal.h>

LiquidCrystal lcd(3, 5, 10, 11, 12, 24); // RS, E, D4, D5, D6, D7

void setup() {
  delay(200);
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Hello");
  lcd.setCursor(0,1);
  lcd.print("Teensy 4.1");
}

void loop() {}
