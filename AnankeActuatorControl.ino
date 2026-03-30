/*
 *
 * HARDWARE:
 *   Teensy 4.1
 *   2x PA- Linear Actuators (each with quadrature encoder)
 *   2x BTS7960 Motor Drivers
 *
 * PIN CONNECTIONS:
 *   Actuator X:
 *     D2  → Encoder A (pinA)
 *     D3  → Encoder B (pinB)
 *     D5  → BTS7960 RPWM (extend)
 *     D6  → BTS7960 LPWM (retract)
 *
 *   Actuator Y:
 *     D7  → Encoder A (pinA)
 *     D8  → Encoder B (pinB)
 *     D9  → BTS7960 RPWM (extend)
 *     D10 → BTS7960 LPWM (retract)
 */

// Actuator X encoder pins
const int X_pinA = 2;
const int X_pinB = 3;

// Actuator X motor driver pins
const int X_RPWM = 5;  // extend
const int X_LPWM = 6;  // retract

// Actuator Y encoder pins
const int Y_pinA = 7;
const int Y_pinB = 8;

// Actuator Y motor driver pins
const int Y_RPWM = 9;   // extend
const int Y_LPWM = 10;  // retract

volatile long X_count = 0;
volatile long Y_count = 0;


//  PULSE SCALE - 4-inch stroke = 16,644 pulses  ->  4,161 pulses per inch
const long PULSES_PER_INCH = 4161;

// Pre-calculated target positions in pulses
const long POS_0_IN = 0;                     //     0 pulses
const long POS_2_IN = 2L * PULSES_PER_INCH;  //  8,322 pulses
const long POS_4_IN = 4L * PULSES_PER_INCH;  // 16,644 pulses


//  TUNING CONSTANTS  (adjust these to fine-tune behaviour)
const int DEADBAND = 20;     // How close is "close enough" (pulses, ≈ 0.005 in)
const int PWM_FAST = 220;    // Speed when far from target  (0–255)
const int PWM_SLOW = 90;     // Speed when near target      (0–255)
const long SLOW_ZONE = 500;  // Switch to slow speed within this many pulses
const int DWELL_MS = 2000;   // How long to wait at each position (ms)

const int HOME_SPEED = 150;         // Speed during homing retract
const long HOME_TIMEOUT_MS = 8000;  // Max time allowed to home (ms)
const long STALL_TIME_MS = 400;     // No movement for this long = at hard limit

//  SEQUENCE
const long SEQUENCE[] = { POS_0_IN, POS_2_IN, POS_4_IN, POS_2_IN };
const int SEQ_LEN = 4;
int seqIndex = 0;  // which step we are on
int sequenceHighs[10] = {0};
int sequenceLows[10] = {0};
int currentWrite = 0;

//  HEARTBEAT
unsigned long lastBlink = 0;
bool ledState = false;


void setup() {
  Serial.begin(115200);

  // X encoder
  pinMode(X_pinA, INPUT_PULLUP);
  pinMode(X_pinB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(X_pinA), X_A_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(X_pinB), X_B_ISR, CHANGE);

  // Y encoder
  pinMode(Y_pinA, INPUT_PULLUP);
  pinMode(Y_pinB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(Y_pinA), Y_A_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(Y_pinB), Y_B_ISR, CHANGE);

  // Motor pins
  pinMode(X_RPWM, OUTPUT);
  pinMode(X_LPWM, OUTPUT);
  pinMode(Y_RPWM, OUTPUT);
  pinMode(Y_LPWM, OUTPUT);

  // PWM frequency
  analogWriteFrequency(X_RPWM, 1000);
  analogWriteFrequency(X_LPWM, 1000);
  analogWriteFrequency(Y_RPWM, 1000);
  analogWriteFrequency(Y_LPWM, 1000);
  analogWriteResolution(8);  // 8-bit = values 0–255

  // Make sure both actuators start stopped
  X_stop();
  Y_stop();

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println("=== Dual Actuator XY Controller ===");

  // Home both actuators
  Serial.println("Homing X...");
  homeActuator('X');
  Serial.println("Homing Y...");
  homeActuator('Y');
  Serial.println("Both actuators homed.");

  //Move both to 2 inches
  Serial.println("Moving to start position: 2 inches...");
  moveToPosition('X', POS_2_IN);
  moveToPosition('Y', POS_2_IN);
  dwellAt(POS_2_IN);

  Serial.println("Setup done. Starting main loop.");
}

void loop() {
  heartbeat();

  long target = SEQUENCE[seqIndex];

  // Save the high and low values
  if (seqIndex == 0) {
    sequenceHighs[currentWrite % 10] = X_count;
    currentWrite++;
  } else if (seqIndex == 2) {
    sequenceLows[currentWrite % 10] = X_count;
    currentWrite++;
  }

  Serial.print("Step ");
  Serial.print(seqIndex);
  Serial.print(" — moving to ");
  Serial.print((float)target / PULSES_PER_INCH, 1);
  Serial.println(" inches");
  Serial.print("Current Lows: "); Serial.println(sequenceLows);
  Serial.print("Current Highs: "); Serial.println(sequenceHighs);

  // Move X and Y simultaneously
  unsigned long timeout = millis() + 15000;
  while (true) {
    heartbeat();

    long X_error = target - X_count;
    long Y_error = target - Y_count;

    // Drive X
    if (abs(X_error) > DEADBAND) {
      int speed = (abs(X_error) < SLOW_ZONE) ? PWM_SLOW : PWM_FAST;
      if (X_error > 0) X_extend(speed);
      else             X_retract(speed);
    } else {
      X_stop();
    }

    // Drive Y
    if (abs(Y_error) > DEADBAND) {
      int speed = (abs(Y_error) < SLOW_ZONE) ? PWM_SLOW : PWM_FAST;
      if (Y_error > 0) Y_extend(speed);
      else             Y_retract(speed);
    } else {
      Y_stop();
    }

    // Both arrived — exit
    if (abs(X_error) <= DEADBAND && abs(Y_error) <= DEADBAND) break;

    // Safety timeout
    if (millis() > timeout) {
      X_stop();
      Y_stop();
      Serial.println("WARNING: Move timed out!");
      break;
    }

    delay(5);
  }

  dwellAt(target);

  seqIndex = (seqIndex + 1) % SEQ_LEN;
}


//  HOME ACTUATOR
void homeActuator(char axis) {
  long prevCount = getCount(axis);
  unsigned long startTime = millis();
  unsigned long lastMoveTime = millis();

  // Start retracting
  if (axis == 'X') X_retract(HOME_SPEED);
  else Y_retract(HOME_SPEED);

  while (true) {
    heartbeat();

    long currentCount = getCount(axis);

    // If encoder moved more than 10 pulses since last check, it's still moving
    if (abs(currentCount - prevCount) > 10) {
      lastMoveTime = millis();
    }
    prevCount = currentCount;

    // No movement for STALL_TIME_MS = hit the hard limit
    if (millis() - lastMoveTime > STALL_TIME_MS) {
      if (axis == 'X') {
        X_stop();
        noInterrupts();
        X_count = 0;
        interrupts();
      } else {
        Y_stop();
        noInterrupts();
        Y_count = 0;
        interrupts();
      }
      Serial.print(axis); Serial.println(" homed (stall detected).");
      return;
    }

    // Safety: if it takes too long, stop anyway and zero
    if (millis() - startTime > HOME_TIMEOUT_MS) {
      if (axis == 'X') {
        X_stop();
        noInterrupts();
        X_count = 0;
        interrupts();
      } else {
        Y_stop();
        noInterrupts();
        Y_count = 0;
        interrupts();
      }
      Serial.print(axis); Serial.println(" homing timeout — zeroed at current position.");
      return;
    }

    delay(20);
  }
}

//  MOVE TO POSITION  (blocking — waits until arrived)
void moveToPosition(char axis, long target) {
  unsigned long timeout = millis() + 15000;  // 15 second safety cutoff

  while (true) {
    heartbeat();

    long pos = getCount(axis);  // where we are now
    long error = target - pos;  // how far we still need to go

    // Close enough — stop and return
    if (abs(error) <= DEADBAND) {
      if (axis == 'X') X_stop();
      else Y_stop();
      return;
    }

    // Safety: if move takes too long, give up
    if (millis() > timeout) {
      if (axis == 'X') X_stop();
      else Y_stop();
      Serial.print(axis); Serial.println(" move timed out!");
      return;
    }

    // Use slow speed when close, fast speed when far away
    int speed = (abs(error) < SLOW_ZONE) ? PWM_SLOW : PWM_FAST;

    // Drive in the correct direction
    if (error > 0) {
      // Need to extend (go forward)
      if (axis == 'X') X_extend(speed);
      else Y_extend(speed);
    } else {
      // Need to retract (go backward)
      if (axis == 'X') X_retract(speed);
      else Y_retract(speed);
    }

    delay(5);
  }
}

//  DWELL AT POSITION
void dwellAt(long target) {
  unsigned long endTime = millis() + DWELL_MS;

  while (millis() < endTime) {
    heartbeat();

    // Check and correct X
    long X_error = target - X_count;
    if (abs(X_error) <= DEADBAND) X_stop();
    else if (X_error > 0) X_extend(PWM_SLOW);
    else X_retract(PWM_SLOW);

    // Check and correct Y
    long Y_error = target - Y_count;
    if (abs(Y_error) <= DEADBAND) Y_stop();
    else if (Y_error > 0) Y_extend(PWM_SLOW);
    else Y_retract(PWM_SLOW);

    delay(10);
  }

  X_stop();
  Y_stop();
}

//  HELPER — returns the current encoder count for an axis
long getCount(char axis) {
  return (axis == 'X') ? X_count : Y_count;
}

//  MOTOR CONTROL — X axis
void X_extend(int speed) {
  analogWrite(X_RPWM, constrain(speed, 0, 255));
  analogWrite(X_LPWM, 0);
}
void X_retract(int speed) {
  analogWrite(X_RPWM, 0);
  analogWrite(X_LPWM, constrain(speed, 0, 255));
}
void X_stop() {
  analogWrite(X_RPWM, 0);
  analogWrite(X_LPWM, 0);
}

//  MOTOR CONTROL — Y axis
void Y_extend(int speed) {
  analogWrite(Y_RPWM, constrain(speed, 0, 255));
  analogWrite(Y_LPWM, 0);
}
void Y_retract(int speed) {
  analogWrite(Y_RPWM, 0);
  analogWrite(Y_LPWM, constrain(speed, 0, 255));
}
void Y_stop() {
  analogWrite(Y_RPWM, 0);
  analogWrite(Y_LPWM, 0);
}

// X Encoder — Pin A changed
void X_A_ISR() {
  if (digitalRead(X_pinA) != digitalRead(X_pinB)) {
    X_count++;
  } else {
    X_count--;
  }
}

// X Encoder — Pin B changed
void X_B_ISR() {
  if (digitalRead(X_pinA) == digitalRead(X_pinB)) {
    X_count++;
  } else {
    X_count--;
  }
}

// Y Encoder — Pin A changed
void Y_A_ISR() {
  if (digitalRead(Y_pinA) != digitalRead(Y_pinB)) {
    Y_count++;
  } else {
    Y_count--;
  }
}

// Y Encoder — Pin B changed
void Y_B_ISR() {
  if (digitalRead(Y_pinA) == digitalRead(Y_pinB)) {
    Y_count++;
  } else {
    Y_count--;
  }
}

//  HEARTBEAT
void heartbeat() {
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);
  }
}