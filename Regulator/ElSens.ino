#ifdef ARDUINO_ARCH_SAMD
#include <avdweb_AnalogReadFast.h>
#endif
//#include <Wire.h>
//#define I2C_ADC121         0x50

const int ELSENS_ANALOG_MIDDLE_VALUE = 512; // set 0 for Grove El. sensor CT

const unsigned long OVERHEATED_COOLDOWN_TIME = PUMP_STOP_MILLIS - 30000; // resume 30 sec before pump stops

unsigned long overheatedStart = 0;

void elsensSetup() {
#ifdef I2C_ADC121
  const byte REG_ADDR_RESULT = 0x00;
  const byte REG_ADDR_CONFIG = 0x02;
  Wire.begin();
  Wire.setClock(400000);
  Wire.beginTransmission(I2C_ADC121);
  Wire.write(REG_ADDR_CONFIG);
  Wire.write(REG_ADDR_RESULT);
  Wire.endTransmission();
  Wire.beginTransmission(I2C_ADC121);
  Wire.write(REG_ADDR_RESULT);
  Wire.endTransmission();
#endif
}

void elsensLoop() {

  const int PUMP_POWER = 40;

  // system's power factor characteristics
  const float PF_ANGLE_INTERVAL = PI * 0.33;
  const float PF_ANGLE_SHIFT = PI * 0.22;

  // Grove I2C ADC and Grove Electricity Sensor CT
//  const int ELSENS_MAX_VALUE = 2300;
//  const float ELSENS_VALUE_COEF = 1.12;
//  const int ELSENS_VALUE_SHIFT = 200;
//  const int ELSENS_MIN_HEATING_VALUE = 300;

#ifdef ARDUINO_SAMD_MKRZERO
  // ACS712 20A analogReadFast over MKR Connector Carrier A pin's voltage divider with capacitor removed
  const int ELSENS_MAX_VALUE = 1500;
  const float ELSENS_VALUE_COEF = 1.86;
#else
  // 5 V ATmega 'analog' pin and ACS712 sensor 20A version
  const int ELSENS_MAX_VALUE = 1900;
  const float ELSENS_VALUE_COEF = 1.38;
#endif
  const int ELSENS_VALUE_SHIFT = 80;
  const int ELSENS_MIN_HEATING_VALUE = 250;

  // waiting for water to cooldown
  if (overheatedStart != 0) {
    if (state == RegulatorState::OVERHEATED && (loopStartMillis - overheatedStart) < OVERHEATED_COOLDOWN_TIME && !buttonPressed)
      return;
    overheatedStart = 0;
    state = RegulatorState::MONITORING;
  }

  elsens = readElSens();

  if (heatingPower > 0 && elsens < ELSENS_MIN_HEATING_VALUE) {
    overheatedStart = loopStartMillis;
    state = RegulatorState::OVERHEATED;
    msg.print(F("overheated"));
    eventsWrite(OVERHEATED_EVENT, elsens, heatingPower);
    alarmSound();
  }

  if (elsens > ELSENS_MIN_HEATING_VALUE) {
    float ratio = 1.0 - ((float) elsens / ELSENS_MAX_VALUE); // to 'guess' the 'power factor'
    elsensPower = (int) (elsens * ELSENS_VALUE_COEF * cos(PF_ANGLE_SHIFT + ratio * PF_ANGLE_INTERVAL)) + ELSENS_VALUE_SHIFT;
  } else {
    elsensPower = mainRelayOn ? PUMP_POWER : 0;
  }
}

boolean elsensCheckPump() {

  const int ELSENS_MIN_ON_VALUE = 40;

  delay(1000); // pump run-up
  int v = readElSens();
  if (v < ELSENS_MIN_ON_VALUE) {
    digitalWrite(MAIN_RELAY_PIN, LOW);
    mainRelayOn = false;
    alarmCause = AlarmCause::PUMP;
    eventsWrite(PUMP_EVENT, v, ELSENS_MIN_ON_VALUE);
    return false;
  }
  return true;
}

byte overheatedSecondsLeft() {
  return (OVERHEATED_COOLDOWN_TIME - (loopStartMillis - overheatedStart)) / 1000;
}

/**
 * return value is RMS of sampled values
 */
int readElSens() {

  // set 1 for Grove El. sensor CT
  const int RMS_INT_SCALE = 10;

  unsigned long long sum = 0;
  int n = 0;
  unsigned long start_time = millis();
  while (millis() - start_time < 200) { // in 200 ms measures 10 50Hz AC oscillations
    long v = (short) elsensAnalogRead() - ELSENS_ANALOG_MIDDLE_VALUE;
    sum += v * v;
    n++;
  }
  if (ELSENS_ANALOG_MIDDLE_VALUE == 0) {
    n = n / 2; // half of the values are zeros for removed negative voltages
  }
  return sqrt((double) sum / n) * RMS_INT_SCALE;
}

void elsensWaitZeroCrossing() {

  const int ZC_BAND = 10;

  unsigned long startMillis = millis();
  while (millis() - startMillis < 10) { // 10 milliseconds of AC half wave
    short v = elsensAnalogRead();
    if (v > ELSENS_ANALOG_MIDDLE_VALUE - ZC_BAND && v < ELSENS_ANALOG_MIDDLE_VALUE + ZC_BAND)
      break;
  }
}

unsigned short elsensAnalogRead() {
#ifdef I2C_ADC121
  Wire.requestFrom(I2C_ADC121, 2);
  byte buff[2];
  Wire.readBytes(buff, 2);
  return (buff[0] << 8) | buff[1];
#elif ARDUINO_ARCH_SAMD
  return analogReadFast(ELSENS_PIN);
#else
  return analogRead(ELSENS_PIN);
#endif
}

