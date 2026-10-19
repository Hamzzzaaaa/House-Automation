// ===== SMART HOME - SLAVE (Arduino 2) =====
// Climate (TMP36, Pot, 2 Fans, Heater), Light (LDR, 2 Bulbs),
// Solar panel (LDR + Servo).  Controlled by the MASTER over I2C.
#include <Wire.h>
#include <Servo.h>

#define SLAVE_ADDR 8
#define TEMP_PIN   A0   // TMP36
#define POT_PIN    A1   // Fan speed
#define LDR_ROOM   A2   // LDR 1 (room)
#define LDR_SUN    A3   // LDR 2 (sun)
#define FAN1       5
#define FAN2       6
#define HEATER     7    // Yellow LED
#define BULB1      8
#define BULB2      9
#define SOLAR_PIN  10

// Adjust to the min/max you see on LDR 2 in Tinkercad
#define SUN_MAX 679     // brightest = East = 0 degrees
#define SUN_MIN 6       // darkest   = West = 180 degrees

Servo solarServo;
volatile bool systemActive = false;     // flag from master
int temperature = 0;
int lightsOn = 0;
unsigned long lastShow = 0;
bool lastFlag = false;                  // for the Serial message below

void receiveEvent(int n) {
  while (Wire.available()) systemActive = Wire.read();
}

void requestEvent() {
  Wire.write((byte)(temperature + 40));
  Wire.write((byte)lightsOn);
}

void allOff() {
  analogWrite(FAN1, 0);
  analogWrite(FAN2, 0);
  digitalWrite(HEATER, LOW);
  digitalWrite(BULB1, LOW);
  digitalWrite(BULB2, LOW);
  lightsOn = 0;
}

void setup() {
  Serial.begin(9600);
  pinMode(FAN1, OUTPUT);  pinMode(FAN2, OUTPUT);
  pinMode(HEATER, OUTPUT);
  pinMode(BULB1, OUTPUT); pinMode(BULB2, OUTPUT);
  solarServo.attach(SOLAR_PIN);
  solarServo.write(0);
  Wire.begin(SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
}

void loop() {
  // Always measure temperature so the master can show it
  float volts = analogRead(TEMP_PIN) * 5.0 / 1024.0;
  temperature = (int)((volts - 0.5) * 100.0);

  // Tell us on the Serial Monitor when the master switches us on or off
  if (systemActive != lastFlag) {
    lastFlag = systemActive;
    Serial.println(systemActive ? "SLAVE: system ACTIVE" : "SLAVE: system INACTIVE");
  }

  if (!systemActive) {                  // inactive = nothing works
    allOff();
    solarServo.write(0);
    delay(100);
    return;
  }

  // ---- Climate control ----
  int pot = analogRead(POT_PIN);
  int speed = 0;
  if (temperature > 27) {
    speed = map(pot, 0, 1023, 0, 255);  // Pot sets fan speed
  }
  analogWrite(FAN1, speed);
  analogWrite(FAN2, speed);
  digitalWrite(HEATER, temperature < 17 ? HIGH : LOW);

  // ---- Light control ----
  int room = analogRead(LDR_ROOM);
  if (room < 100) { digitalWrite(BULB1, HIGH); digitalWrite(BULB2, HIGH); lightsOn = 1; }
  else            { digitalWrite(BULB1, LOW);  digitalWrite(BULB2, LOW);  lightsOn = 0; }

  // ---- Solar panel: bright (East, 0) -> dark (West, 180) ----
  int sun = constrain(analogRead(LDR_SUN), SUN_MIN, SUN_MAX);
  int angle = map(sun, SUN_MAX, SUN_MIN, 0, 180);
  solarServo.write(angle);

  // ---- Readings on Serial Monitor ----
  if (millis() - lastShow > 500) {
    lastShow = millis();
    Serial.print("Temp="); Serial.print(temperature);
    Serial.print("C Fan="); Serial.print(speed);
    Serial.print(" Heater="); Serial.print(digitalRead(HEATER));
    Serial.print(" LDRroom="); Serial.print(room);
    Serial.print(" Bulbs="); Serial.print(lightsOn);
    Serial.print(" LDRsun="); Serial.print(sun);
    Serial.print(" Solar="); Serial.println(angle);
  }
  delay(50);
}
