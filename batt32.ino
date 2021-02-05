// battery is PIN 35
// this site https://www.tinytronics.nl/shop/en/platforms/ttgo/lilygo-ttgo-t-energy-esp32-wrover-with-18650-battery-holder

#include <driver/adc.h>
#include "esp_adc_cal.h"

void setup() {
Serial.begin(115200);
esp_adc_cal_characteristics_t adc_chars;
esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC_ATTEN_DB_2_5, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
pinMode(14, OUTPUT);
}

void loop() {
digitalWrite(14, HIGH);
delay(1000);
float measurement = (float) analogRead(35);
float battery_voltage = (measurement / 4095.0) * 7.26;
digitalWrite(14, LOW);
Serial.print("battery_voltage  ");
Serial.print(battery_voltage);
Serial.println("");

}
