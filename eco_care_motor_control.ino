/*
  =====================================================
  ECO CARE SYSTEMS - MOTOR ROTATION CONTROL
  =====================================================

  HARDWARE:
    - Arduino UNO
    - 20x4 I2C LCD (address 0x27)
    - 4x4 Keypad
    - 2x Relay Module (Active HIGH - LOW=OFF, HIGH=ON)
    - Hall Sensor 49E on A0 (Analog)
    - Emergency Push Button on Pin 12 (NO, Active LOW)
    - Motor Start Push Button on Pin 13 (NO, Active LOW)

  RELAY LOGIC:
    LOW  = OFF
    HIGH = ON
    BOTH RELAYS ARE NEVER ON SIMULTANEOUSLY - EVER

  PIN MAP:
    Keypad Rows      : 4, 5, 6, 7
    Keypad Cols      : 8, 9, 10, 11
    CW  Relay        : 2
    CCW Relay        : 3
    Hall Sensor      : A0 (Analog)
    LCD I2C          : SDA=A4, SCL=A5 (default UNO I2C)
    E-Stop Button    : 12 (NO, INPUT_PULLUP — LOW = pressed)
    Start Button     : 13 (NO, INPUT_PULLUP — LOW = pressed)

  WIRING FOR PHYSICAL BUTTONS:
    Each button connects between its pin and GND.
    No external resistor needed (internal pull-up used).
    Pin 12: One leg to GND, other leg to Pin 12
    Pin 13: One leg to GND, other leg to Pin 13

  BUTTON FUNCTIONS:
    [PASSWORD SCREEN]
    0-9            = Enter password digit
    A              = Confirm password
    B              = Backspace password digit

    [OPERATION]
    1              = Select Clockwise mode
    2              = Select Anti-Clockwise mode
    3              = Select CW + CCW mode
    0-9            = Rotation digit input (up to 4 digits)
    B              = Backspace — delete last digit entered
    C x1           = Set rotation input to 0
    C x2           = Clear rotation input completely
    C x3           = EMERGENCY STOP (works during motor run)
    A              = Confirm & Start motor (STATE_MODE_CONFIRM)
    [Pin 13 btn]   = Start Motor (same as A)
    [Pin 12 btn]   = EMERGENCY STOP (instant single-press)
    (after completion / emergency)
    1              = Return to mode select
    2              = Repeat last operation

  STATES:
    STATE_STARTUP     -> Password screen
    STATE_PASSWORD    -> Password entry
    STATE_MODE_SELECT -> Mode menu
    STATE_MODE_CONFIRM-> Enter rotations
    STATE_RUNNING     -> Motor active
    STATE_DONE        -> Finished
    STATE_DANGER      -> Safety check failed, halted
    STATE_EMERGENCY   -> Emergency stop triggered
  =====================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// =====================================================
// LCD
// =====================================================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// =====================================================
// KEYPAD
// =====================================================
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'D','0','B','*'},
  {'#','9','6','3'},
  {'C','8','5','2'},
  {'A','7','4','1'}
};

byte rowPins[ROWS] = {4, 5, 6, 7};
byte colPins[COLS]  = {8, 9, 10, 11};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// =====================================================
// RELAY PINS  (Active HIGH: LOW=OFF  HIGH=ON)
// =====================================================
#define CW_RELAY   2
#define CCW_RELAY  3

// =====================================================
// HALL SENSOR  (Analog on A0)
// =====================================================
#define HALL_PIN               A0
#define HALL_DETECT_THRESHOLD  600
#define HALL_RESET_THRESHOLD   550

// =====================================================
// PHYSICAL PUSH BUTTONS
// =====================================================
#define ESTOP_BTN_PIN  12
#define START_BTN_PIN  13
#define DEBOUNCE_MS    50

bool          estopLastState    = HIGH;
unsigned long estopLastChangeMs = 0;

bool          startLastState    = HIGH;
unsigned long startLastChangeMs = 0;

// =====================================================
// PASSWORD
// =====================================================
const String CORRECT_PASSWORD = "13579";
String       pwdInputStr      = "";

// =====================================================
// SYSTEM STATES
// =====================================================
enum SystemState {
  STATE_STARTUP,
  STATE_PASSWORD,
  STATE_MODE_SELECT,
  STATE_MODE_CONFIRM,
  STATE_RUNNING,
  STATE_DONE,
  STATE_DANGER,
  STATE_EMERGENCY
};

SystemState currentState = STATE_STARTUP;

// =====================================================
// MOTOR MODES
// =====================================================
enum MotorMode {
  MODE_NONE = 0,
  MODE_CW   = 1,
  MODE_CCW  = 2,
  MODE_BOTH = 3
};

MotorMode selectedMode  = MODE_NONE;
MotorMode lastMode      = MODE_NONE;
int       lastRotations = 0;

// =====================================================
// ROTATION TRACKING
// =====================================================
String rotInputStr     = "";
int    targetRotations = 0;
int    hallRotations   = 0;
bool   magnetDetected  = false;

// =====================================================
// C-KEY MULTI-PRESS TRACKING
// =====================================================
unsigned long lastCPressTime = 0;
int           cPressCount    = 0;

const unsigned long MULTI_PRESS_WINDOW = 500;

// =====================================================
// SAFETY CHECK TIMEOUT
// =====================================================
const unsigned long SAFETY_TIMEOUT_MS = 10000;

// =====================================================
// RELAY HELPERS
// =====================================================
void allRelaysOFF() {
  digitalWrite(CW_RELAY,  LOW);
  delayMicroseconds(500);
  digitalWrite(CCW_RELAY, LOW);
}

void cwON() {
  digitalWrite(CCW_RELAY, LOW);
  delayMicroseconds(500);
  digitalWrite(CW_RELAY,  HIGH);
}

void ccwON() {
  digitalWrite(CW_RELAY,  LOW);
  delayMicroseconds(500);
  digitalWrite(CCW_RELAY, HIGH);
}

// =====================================================
// HALL SENSOR READ
// =====================================================
bool hallTick() {
  int sensorValue = analogRead(HALL_PIN);

  if (sensorValue > HALL_DETECT_THRESHOLD && !magnetDetected) {
    hallRotations++;
    magnetDetected = true;
    return true;
  }

  if (sensorValue < HALL_RESET_THRESHOLD) {
    magnetDetected = false;
  }

  return false;
}

// =====================================================
// LCD HELPER
// =====================================================
void lcdLine(int row, const char* text) {
  char buf[21];
  snprintf(buf, sizeof(buf), "%-20s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

// =====================================================
// PHYSICAL BUTTON READERS  (debounced)
// =====================================================
bool readEStopBtn() {
  bool pinState = digitalRead(ESTOP_BTN_PIN);
  unsigned long now = millis();

  if (pinState != estopLastState) {
    estopLastChangeMs = now;
    estopLastState    = pinState;
  }

  if ((now - estopLastChangeMs >= DEBOUNCE_MS) && (pinState == LOW)) {
    estopLastChangeMs = now + 60000UL;
    return true;
  }
  return false;
}

bool readStartBtn() {
  bool pinState = digitalRead(START_BTN_PIN);
  unsigned long now = millis();

  if (pinState != startLastState) {
    startLastChangeMs = now;
    startLastState    = pinState;
  }

  if ((now - startLastChangeMs >= DEBOUNCE_MS) && (pinState == LOW)) {
    startLastChangeMs = now + 60000UL;
    return true;
  }
  return false;
}

// =====================================================
// PASSWORD SCREEN
// =====================================================
void showPasswordScreen() {
  currentState = STATE_PASSWORD;
  pwdInputStr  = "";

  lcd.clear();
  lcdLine(0, " ECO CARE SYSTEMS   ");
  lcdLine(1, "--------------------");
  lcdLine(2, "Enter Password: [O] ");
  lcdLine(3, "PWD: _              ");
}

// Update row 3 only — mask digits as *
void updatePasswordDisplay() {
  char masked[6] = {0};
  int len = pwdInputStr.length();
  for (int i = 0; i < len && i < 5; i++) masked[i] = '*';
  masked[len] = '\0';

  char line[21];
  if (len == 0) {
    snprintf(line, sizeof(line), "PWD: _              ");
  } else {
    snprintf(line, sizeof(line), "PWD: %-15s", masked);
  }
  lcdLine(3, line);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  digitalWrite(CW_RELAY,  LOW);
  digitalWrite(CCW_RELAY, LOW);
  pinMode(CW_RELAY,  OUTPUT);
  pinMode(CCW_RELAY, OUTPUT);
  allRelaysOFF();

  pinMode(ESTOP_BTN_PIN, INPUT_PULLUP);
  pinMode(START_BTN_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();

  Serial.begin(9600);

  // Show password screen immediately on boot
  showPasswordScreen();
}

// =====================================================
// SAFETY CHECK
// =====================================================
bool runSafetyCheck() {

  // --- CCW test ---
  hallRotations  = 0;
  magnetDetected = false;

  lcd.clear();
  lcdLine(0, " ECO CARE SYSTEMS   ");
  lcdLine(1, "--------------------");
  lcdLine(2, " Testing : CCW...   ");
  lcdLine(3, "                    ");

  ccwON();
  unsigned long t = millis();
  bool ccwOk = false;

  while (millis() - t < SAFETY_TIMEOUT_MS) {
    hallTick();
    if (hallRotations >= 1) { ccwOk = true; break; }
    delay(10);
  }

  allRelaysOFF();
  delay(500);
  if (!ccwOk) return false;

  // --- CW test ---
  hallRotations  = 0;
  magnetDetected = false;

  lcdLine(2, " Testing : CW...    ");

  cwON();
  t = millis();
  bool cwOk = false;

  while (millis() - t < SAFETY_TIMEOUT_MS) {
    hallTick();
    if (hallRotations >= 1) { cwOk = true; break; }
    delay(10);
  }

  allRelaysOFF();
  delay(500);

  return cwOk;
}

// =====================================================
// SHOW MODE SELECT SCREEN
// =====================================================
void showModeSelect() {
  lcd.clear();
  lcdLine(0, "-- SELECT  MODE --  ");
  lcdLine(1, " [1] Clockwise      ");
  lcdLine(2, " [2] Anti-Clockwise ");
  lcdLine(3, " [3] CW then CCW    ");
}

// =====================================================
// SHOW MODE CONFIRM SCREEN
// =====================================================
void showModeConfirm() {
  lcd.clear();

  char line0[21];
  const char* modeName = "";
  if      (selectedMode == MODE_CW)   modeName = "Clockwise";
  else if (selectedMode == MODE_CCW)  modeName = "Anti-CW  ";
  else if (selectedMode == MODE_BOTH) modeName = "CW + CCW ";

  snprintf(line0, sizeof(line0), "Mode : %-13s", modeName);
  lcdLine(0, line0);
  lcdLine(1, "                    ");
  lcdLine(2, " [F]=delet");
  lcdLine(3, "Rotations: _        ");
}

// =====================================================
// SHOW ROTATION INPUT  (Row 3 only — live update)
// =====================================================
void showRotationInput() {
  char line[21];
  if (rotInputStr.length() == 0) {
    snprintf(line, sizeof(line), "Rotations: _        ");
  } else {
    snprintf(line, sizeof(line), "Rotations: %-9s", rotInputStr.c_str());
  }
  lcdLine(3, line);
}

// =====================================================
// ATTEMPT START
// =====================================================
void attemptStart() {
  if (rotInputStr.length() == 0 || rotInputStr.toInt() == 0) {
    lcdLine(2, "[B]=Del [C]=0 [A]=GO");
    lcdLine(3, "!! Enter value > 0 !!");
  } else {
    targetRotations = rotInputStr.toInt();
    lastMode        = selectedMode;
    lastRotations   = targetRotations;
    startMotor();
  }
}

// =====================================================
// START MOTOR
// =====================================================
void startMotor() {
  currentState = STATE_RUNNING;

  if (selectedMode == MODE_CW) {

    runCWRotations(targetRotations);

  } else if (selectedMode == MODE_CCW) {

    runCCWRotations(targetRotations);

  } else if (selectedMode == MODE_BOTH) {

    runCWRotations(targetRotations);
    if (currentState == STATE_EMERGENCY) return;

    allRelaysOFF();

    lcd.clear();
    lcdLine(0, "-- CW COMPLETE --   ");
    lcdLine(1, " Switching to CCW   ");
    lcdLine(2, " Please wait 2s...  ");
    lcdLine(3, "[ESTOP-BTN]=E-Stop  ");

    for (int i = 0; i < 20; i++) {
      delay(100);
      if (checkEmergencyStop()) return;
    }

    runCCWRotations(targetRotations);
    if (currentState == STATE_EMERGENCY) return;
  }

  allRelaysOFF();
  showDoneScreen();
  currentState = STATE_DONE;
}

// =====================================================
// RUN CW ROTATIONS
// =====================================================
void runCWRotations(int target) {
  hallRotations  = 0;
  magnetDetected = false;

  cwON();

  lcd.clear();
  lcdLine(0, ">> RUNNING : CW     ");
  lcdLine(1, "                    ");
  lcdLine(2, "                    ");
  lcdLine(3, "[ESTOP-BTN] = STOP  ");

  while (hallRotations < target) {
    if (checkEmergencyStop()) return;
    hallTick();
    updateRunningDisplay(hallRotations, target, "CW ");
    delay(10);
  }

  allRelaysOFF();
}

// =====================================================
// RUN CCW ROTATIONS
// =====================================================
void runCCWRotations(int target) {
  hallRotations  = 0;
  magnetDetected = false;

  ccwON();

  lcd.clear();
  lcdLine(0, ">> RUNNING : CCW    ");
  lcdLine(1, "                    ");
  lcdLine(2, "                    ");
  lcdLine(3, "[ESTOP-BTN] = STOP  ");

  while (hallRotations < target) {
    if (checkEmergencyStop()) return;
    hallTick();
    updateRunningDisplay(hallRotations, target, "CCW");
    delay(10);
  }

  allRelaysOFF();
}

// =====================================================
// UPDATE RUNNING DISPLAY
// =====================================================
void updateRunningDisplay(int done, int total, const char* dir) {
  char line[21];

  snprintf(line, sizeof(line), "Dir:%-3s  %4d / %-4d", dir, done, total);
  lcdLine(1, line);

  int pct = (total > 0) ? ((long)done * 100 / total) : 0;

  char bar[11];
  int filled = pct / 10;
  for (int i = 0; i < 10; i++) bar[i] = (i < filled) ? '#' : '-';
  bar[10] = '\0';

  snprintf(line, sizeof(line), "[%s] %3d%%  ", bar, pct);
  lcdLine(2, line);
}

// =====================================================
// SHOW DONE SCREEN
// =====================================================
void showDoneScreen() {
  lcd.clear();
  lcdLine(0, "-- OPERATION DONE --");
  lcdLine(1, "--------------------");
  lcdLine(2, " [1] Back to Menu   ");
  lcdLine(3, " [2] Repeat Last Op ");
}

// =====================================================
// EMERGENCY STOP CHECK
// =====================================================
bool checkEmergencyStop() {

  if (readEStopBtn()) {
    triggerEmergencyStop();
    return true;
  }

  char key = keypad.getKey();

  if (key == 'C') {
    unsigned long now = millis();

    if (now - lastCPressTime < MULTI_PRESS_WINDOW) {
      cPressCount++;
    } else {
      cPressCount = 1;
    }
    lastCPressTime = now;

    if (cPressCount >= 3) {
      triggerEmergencyStop();
      return true;
    }
  }

  return false;
}

// =====================================================
// TRIGGER EMERGENCY STOP
// =====================================================
void triggerEmergencyStop() {
  allRelaysOFF();
  currentState = STATE_EMERGENCY;
  cPressCount  = 0;

  lcd.clear();
  lcdLine(0, "!! EMERGENCY  STOP !");
  lcdLine(1, "  All Relays : OFF  ");
  lcdLine(2, "--------------------");
  lcdLine(3, " [1] Menu  [2] Rpt  ");
}

// =====================================================
// CONFIRM MODE
// =====================================================
void confirmMode() {
  rotInputStr  = "";
  cPressCount  = 0;
  currentState = STATE_MODE_CONFIRM;
  showModeConfirm();
}

// =====================================================
// RESET TO MODE SELECT
// =====================================================
void resetToModeSelect() {
  allRelaysOFF();
  selectedMode   = MODE_NONE;
  rotInputStr    = "";
  cPressCount    = 0;
  hallRotations  = 0;
  magnetDetected = false;
  currentState   = STATE_MODE_SELECT;
  showModeSelect();
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {

  // Physical E-Stop checked in all non-running states
  if (currentState != STATE_RUNNING) {
    if (readEStopBtn()) {
      triggerEmergencyStop();
      return;
    }
  }

  if (currentState == STATE_RUNNING) return;

  char key = keypad.getKey();

  // ---------------------------------------------------
  // PASSWORD STATE
  //   [0-9] = enter digit (max 5)
  //   [B]   = backspace
  //   [A]   = confirm — if correct, jump straight to
  //           splash + self-test with no extra keypress
  // ---------------------------------------------------
  if (currentState == STATE_PASSWORD) {
    if (!key) return;

    if (key >= '0' && key <= '9') {
      if (pwdInputStr.length() < 5) {
        pwdInputStr += key;
        updatePasswordDisplay();
      }
    }

    else if (key == 'B') {
      if (pwdInputStr.length() > 0) {
        pwdInputStr.remove(pwdInputStr.length() - 1);
        updatePasswordDisplay();
      }
    }

    else if (key == 'A') {
      if (pwdInputStr == CORRECT_PASSWORD) {

        // ✅ Correct — splash screen immediately
        lcd.clear();
        lcdLine(0, " ECO CARE SYSTEMS   ");
        lcdLine(1, "  Motor Controller  ");
        lcdLine(2, "   Version  1.0     ");
        lcdLine(3, "                    ");
        delay(2500);

        // Self-test notice
        lcd.clear();
        lcdLine(0, " ECO CARE SYSTEMS   ");
        lcdLine(1, "--------------------");
        lcdLine(2, "  Running Self-Test ");
        lcdLine(3, "  Please wait...    ");
        delay(1000);

        bool passed = runSafetyCheck();

        if (!passed) {
          allRelaysOFF();
          currentState = STATE_DANGER;
          lcd.clear();
          lcdLine(0, "!!  SYSTEM  FAULT  !!");
          lcdLine(1, "--------------------");
          lcdLine(2, " Motor fault found  ");
          lcdLine(3, "  Reset to restart  ");
          while (true) { delay(1000); }
        }

        lcd.clear();
        lcdLine(0, " ECO CARE SYSTEMS   ");
        lcdLine(1, "--------------------");
        lcdLine(2, "  Self-Test : PASS  ");
        lcdLine(3, "  System is Ready   ");
        delay(2000);

        currentState = STATE_MODE_SELECT;
        showModeSelect();

      } else {
        // ❌ Wrong password
        lcd.clear();
        lcdLine(0, " ECO CARE SYSTEMS   ");
        lcdLine(1, "--------------------");
        lcdLine(2, " !! Wrong Password !");
        lcdLine(3, "  Try again...      ");
        delay(1500);
        showPasswordScreen();
      }
    }
    return;
  }

  // ---------------------------------------------------
  // MODE SELECT
  //   [1] Clockwise
  //   [2] Anti-Clockwise
  //   [3] CW + CCW
  // ---------------------------------------------------
  if (currentState == STATE_MODE_SELECT) {
    if (!key) return;
    if      (key == '1') { selectedMode = MODE_CW;   confirmMode(); }
    else if (key == '2') { selectedMode = MODE_CCW;  confirmMode(); }
    else if (key == '3') { selectedMode = MODE_BOTH; confirmMode(); }
  }

  // ---------------------------------------------------
  // MODE CONFIRM  (entering rotation count)
  //   [0-9]  = digit input (max 4 digits)
  //   [B]    = backspace
  //   [C]x1  = set to 0
  //   [C]x2  = clear all
  //   [A]    = confirm and start
  //   [Pin13]= confirm and start
  // ---------------------------------------------------
  else if (currentState == STATE_MODE_CONFIRM) {

    // Physical Start button (Pin 13)
    if (readStartBtn()) {
      cPressCount = 0;
      attemptStart();
      return;
    }

    if (!key) return;

    // Any non-C key resets the C counter
    if (key != 'C') {
      cPressCount    = 0;
      lastCPressTime = 0;
    }

    // --- Digit input ---
    if (key >= '0' && key <= '9') {
      if (rotInputStr.length() < 4) {
        rotInputStr += key;
        showRotationInput();
        lcdLine(2, "F = delete rotations");
      } else {
        lcdLine(2, "!! Max 4 digits !!  ");
        delay(600);
        lcdLine(2, "[B]=Del [C]=0 [A]=GO");
      }
    }

    // --- B = Backspace ---
    else if (key == 'B') {
      if (rotInputStr.length() > 0) {
        rotInputStr.remove(rotInputStr.length() - 1);
        showRotationInput();
        lcdLine(2, "F = delete rotations");
      } else {
        lcdLine(3, "Rotations: _        ");
      }
    }

    // --- A = Confirm & Start ---
    else if (key == 'A') {
      attemptStart();
    }

    // --- C button (multi-press, isolated) ---
    else if (key == 'C') {
      unsigned long now = millis();

      if (now - lastCPressTime < MULTI_PRESS_WINDOW) {
        cPressCount++;
      } else {
        cPressCount = 1;
      }
      lastCPressTime = now;

      if (cPressCount == 1) {
        // Single C: set to 0
        rotInputStr = "0";
        showRotationInput();
        lcdLine(2, "F = delete rotations");
      }
      else if (cPressCount == 2) {
        // Double C: clear everything
        rotInputStr = "";
        cPressCount = 0;
        showRotationInput();
        lcdLine(2, "F = delete rotations");
      }
    }
  }

  // ---------------------------------------------------
  // DONE or EMERGENCY
  //   [1] Return to mode select
  //   [2] Repeat last operation
  // ---------------------------------------------------
  else if (currentState == STATE_DONE || currentState == STATE_EMERGENCY) {
    if (!key) return;

    if (key == '1') {
      resetToModeSelect();
    }
    else if (key == '2') {
      if (lastMode != MODE_NONE && lastRotations > 0) {
        selectedMode    = lastMode;
        targetRotations = lastRotations;
        rotInputStr     = String(lastRotations);

        lcd.clear();
        lcdLine(0, "-- REPEATING OP --  ");
        lcdLine(1, "                    ");

        char line[21];
        const char* mn = (lastMode == MODE_CW)  ? "Clockwise" :
                         (lastMode == MODE_CCW)  ? "Anti-CW  " : "CW + CCW ";
        snprintf(line, sizeof(line), " Mode : %-12s", mn);
        lcdLine(2, line);
        snprintf(line, sizeof(line), " Rots : %-12d", lastRotations);
        lcdLine(3, line);

        delay(1500);
        startMotor();
      } else {
        lcdLine(3, " No previous op!    ");
      }
    }
  }
}