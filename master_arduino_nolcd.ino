// ===== SMART HOME - MASTER (Arduino 1) - I2C LCD DRIVER BUILT IN =====
// Security + Dashboard: Ultrasonic, Keypad, LCD, Door servo,
// Red/Green LED, Buzzer, PIR.  Talks to the SLAVE over I2C.
// Keypad and LCD are driven by hand, so no extra library is needed.
#include <Wire.h>
#include <Servo.h>

#define SLAVE_ADDR 8
#define SIG_PIN    10   // Ultrasonic SIG
#define SERVO_PIN  11   // Door servo
#define RED_LED    12   // Door closed
#define GREEN_LED  13   // Door open
#define BUZZER     A0
#define PIR_PIN    A1

Servo doorServo;

// ---- Keypad 4x4 (rows D2-D5, columns D6-D9) ----
char keys[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[4] = {2, 3, 4, 5};
byte colPins[4] = {6, 7, 8, 9};
char prevKey = 0;

// ---- Variables ----
const String PASSWORD = "1234";
String input = "";
String msg = "";
int  attempts = 0;
bool systemActive = false;        // FLAG variable
bool lastActive   = false;
bool alarmOn  = false;            // 3 wrong passwords
bool motion   = false;            // PIR
bool doorOpen = false;
unsigned long doorTimer = 0, lastComm = 0, lastLcd = 0;
int temperature = 0;              // received from slave
int lightsOn = 0;                 // received from slave
long dist = 999;                  // last ultrasonic reading
int closeCount = 0, farCount = 0;
unsigned long lastPing = 0, lastDist = 0;
bool lastMotion = false;

long readDistanceCm() {
  pinMode(SIG_PIN, OUTPUT);
  digitalWrite(SIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(SIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(SIG_PIN, LOW);
  pinMode(SIG_PIN, INPUT);
  long duration = pulseIn(SIG_PIN, HIGH, 30000);
  if (duration == 0) return 999;            // nothing detected
  return duration * 0.01723;
}

char scanKey() {
  for (int c = 0; c < 4; c++) {
    digitalWrite(colPins[c], LOW);
    for (int r = 0; r < 4; r++) {
      if (digitalRead(rowPins[r]) == LOW) {
        digitalWrite(colPins[c], HIGH);
        return keys[r][c];
      }
    }
    digitalWrite(colPins[c], HIGH);
  }
  return 0;
}

// ---- I2C LCD driver (written by hand, no library) ----
// Click the LCD in Tinkercad to see its I2C address and type.
#define LCD_ADDR 0x20     // address from the LCD properties (0x20 = 32)
#define LCD_TYPE 0        // 0 = MCP23008 backpack, 1 = PCF8574 backpack

#if LCD_TYPE == 0
const byte LCD_RS = 0x02, LCD_E = 0x04, LCD_BL = 0x80, LCD_SHIFT = 3;
#else
const byte LCD_RS = 0x01, LCD_E = 0x04, LCD_BL = 0x08, LCD_SHIFT = 4;
#endif

void lcdBus(byte val) {
  Wire.beginTransmission(LCD_ADDR);
#if LCD_TYPE == 0
  Wire.write(0x09);                 // GPIO register of the MCP23008
#endif
  Wire.write(val);
  Wire.endTransmission();
}

void lcdNibble(byte nib, bool rs) {
  byte out = LCD_BL | (rs ? LCD_RS : 0) | ((nib & 0x0F) << LCD_SHIFT);
  lcdBus(out);
  lcdBus(out | LCD_E);              // pulse the Enable line
  lcdBus(out);
  delayMicroseconds(100);
}

void lcdSend(byte v, bool rs) {
  lcdNibble(v >> 4, rs);
  lcdNibble(v & 0x0F, rs);
}

void lcdInit() {
#if LCD_TYPE == 0
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(0x00);                 // IODIR register
  Wire.write(0x00);                 // all pins are outputs
  Wire.endTransmission();
#endif
  delay(50);
  lcdNibble(0x03, false); delay(5);
  lcdNibble(0x03, false); delay(5);
  lcdNibble(0x03, false); delay(1);
  lcdNibble(0x02, false);           // switch to 4-bit mode
  lcdSend(0x28, false);             // 2 lines, 5x8 font
  lcdSend(0x0C, false);             // display on, no cursor
  lcdSend(0x01, false); delay(3);   // clear
  lcdSend(0x06, false);             // cursor moves right
}

void lcdPrintLine(int row, String t) {
  lcdSend(0x80 | (row ? 0x40 : 0x00), false);   // move to start of the row
  for (unsigned int i = 0; i < t.length(); i++) lcdSend(t[i], true);
}

String lastLine[2] = {"", ""};
// Dashboard text goes to the LCD and to the Serial Monitor
void printLine(int row, String t) {
  while (t.length() < 16) t += ' ';
  t = t.substring(0, 16);
  if (t != lastLine[row]) {
    lastLine[row] = t;
    lcdPrintLine(row, t);
    Serial.print("DASHBOARD line ");
    Serial.print(row + 1);
    Serial.print(": ");
    Serial.println(t);
  }
}

void refreshLCD() {
  if (!systemActive) {
    printLine(0, "SYSTEM INACTIVE");
    printLine(1, "Nothing working");
    return;
  }
  char l0[17];
  snprintf(l0, 17, "ACT T:%3dC L:%s", temperature, lightsOn ? "ON " : "OFF");
  printLine(0, String(l0));
  printLine(1, motion ? String("MOTION ALARM!") : msg);
}

void resetSystem() {
  input = ""; attempts = 0;
  alarmOn = false; motion = false; doorOpen = false;
  noTone(BUZZER);
  doorServo.write(0);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, systemActive ? HIGH : LOW);   // red = door closed
}

void openDoor() {
  alarmOn = false;
  attempts = 0;
  doorOpen = true;
  doorTimer = millis();
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
  doorServo.write(90);
  msg = "Door OPEN";
  Serial.println("Door OPEN");
}

void closeDoor() {
  doorOpen = false;
  doorServo.write(0);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  msg = "Door CLOSED";
  Serial.println("Door CLOSED");
}

void handleKey(char k) {
  Serial.print("Key: "); Serial.println(k);
  if (k == '#') {                              // end of password
    if (input == PASSWORD) {
      openDoor();
    } else {
      attempts++;
      msg = "Wrong! Try " + String(attempts) + "/3";
      Serial.println(msg);
      if (attempts >= 3) {
        alarmOn = true;
        msg = "ALARM! LOCKED";
        Serial.println("ALARM ON");
      }
    }
    input = "";
  } else if (k == '*') {                       // clear
    input = "";
    msg = "Cleared";
  } else if (input.length() < 8) {
    input += k;
    String stars = "";
    for (unsigned int i = 0; i < input.length(); i++) stars += "*";
    msg = "Pass: " + stars;
  }
}

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < 4; i++) {
    pinMode(rowPins[i], INPUT_PULLUP);
    pinMode(colPins[i], OUTPUT);
    digitalWrite(colPins[i], HIGH);
  }
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  doorServo.attach(SERVO_PIN);
  Wire.begin();                    // Master
  lcdInit();
  resetSystem();
  refreshLCD();
}

void loop() {
  // 1) Proximity -> system flag (ping every 100 ms, filtered)
  if (millis() - lastPing >= 100) {
    lastPing = millis();
    dist = readDistanceCm();
    if (dist < 50) { closeCount++; farCount = 0; }
    else           { farCount++;  closeCount = 0; }
    if (closeCount >= 2) systemActive = true;    // 2 close readings in a row
    if (farCount   >= 4) systemActive = false;   // 4 far readings in a row
  }
  if (millis() - lastDist >= 1000) {             // show distance once a second
    lastDist = millis();
    Serial.print("Dist="); Serial.print(dist); Serial.println(" cm");
  }

  if (systemActive != lastActive) {
    lastActive = systemActive;
    resetSystem();
    if (systemActive) msg = "Enter password#";
    Serial.println(systemActive ? "SYSTEM ACTIVE" : "SYSTEM INACTIVE");
  }

  // 2) Send flag to slave, read temperature + lights back
  if (millis() - lastComm > 500) {
    lastComm = millis();
    Wire.beginTransmission(SLAVE_ADDR);
    Wire.write(systemActive ? 1 : 0);
    Wire.endTransmission();
    if (systemActive) {
      Wire.requestFrom(SLAVE_ADDR, 2);
      if (Wire.available() >= 2) {
        temperature = (int)Wire.read() - 40;     // slave adds +40
        lightsOn = Wire.read();
      }
    }
  }

  // 3) Everything below works only when system is ACTIVE
  if (systemActive) {
    motion = digitalRead(PIR_PIN);
    if (motion != lastMotion) {
      lastMotion = motion;
      Serial.println(motion ? "PIR: MOTION" : "PIR: clear");
    }

    char k = scanKey();
    if (k && k != prevKey) handleKey(k);
    prevKey = k;

    if (doorOpen && millis() - doorTimer > 4000) closeDoor();

    if (alarmOn)      tone(BUZZER, 1000);   // 3 wrong attempts
    else if (motion)  tone(BUZZER, 800);    // PIR motion
    else              noTone(BUZZER);
  }

  // 4) Dashboard
  if (millis() - lastLcd > 250) {
    lastLcd = millis();
    refreshLCD();
  }
}
