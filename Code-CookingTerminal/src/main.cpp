/*
 * Cooking Terminal - Smart Kitchen Guide
 * Version: 2.0 (Fixed & Complete)
 * 
 * Hardware:
 * - Xiao ESP32-C3 (Main MCU)
 * - SH1106 OLED 1.3" (I2C)
 * - X27.168 Stepper Motor (4-pin, direct GPIO)
 * - WS2812B RGB LED (1 LED)
 * - EC11 Rotary Encoder (5-pin)
 * - Piezo Buzzer (2-pin)
 * 
 * Pin Assignment:
 * - D0  (GPIO2)  -> X27.168 Pin 1 (Coil A+)
 * - D1  (GPIO3)  -> X27.168 Pin 2 (Coil A-)
 * - D2  (GPIO4)  -> X27.168 Pin 3 (Coil B+)
 * - D3  (GPIO5)  -> WS2812B DIN
 * - D4  (GPIO6)  -> OLED SDA
 * - D5  (GPIO7)  -> OLED SCL
 * - D6  (GPIO21) -> Encoder OUT_A
 * - D7  (GPIO20) -> Buzzer (+)
 * - D8  (GPIO8)  -> Encoder OUT_B
 * - D9  (GPIO9)  -> Encoder SW
 * - D10 (GPIO10) -> X27.168 Pin 4 (Coil B-)
 * 
 * Features:
 * - Temperature gauge display (0-300°C)
 * - OLED display with current/target temp
 * - Rotary encoder for target temp adjustment
 * - LED color status indicator
 * - Buzzer alerts when target reached
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>

// ============== Pin Definitions ==============
// X27.168 Stepper Motor (4-pin)
#define STEPPER_PIN1  2    // D0 - GPIO2 - Coil A+
#define STEPPER_PIN2  3    // D1 - GPIO3 - Coil A-
#define STEPPER_PIN3  4    // D2 - GPIO4 - Coil B+
#define STEPPER_PIN4  10   // D10 - GPIO10 - Coil B-

// WS2812B LED
#define LED_PIN       5    // D3 - GPIO5
#define LED_COUNT     1

// OLED Display (I2C)
#define OLED_SDA      6    // D4 - GPIO6
#define OLED_SCL      7    // D5 - GPIO7

// Rotary Encoder
#define ENCODER_A     21   // D6 - GPIO21
#define ENCODER_B     8    // D8 - GPIO8
#define ENCODER_SW    9    // D9 - GPIO9

// Buzzer
#define BUZZER_PIN    20   // D7 - GPIO20

// ============== Objects ==============
Adafruit_SH1106G oled(128, 64, &Wire, -1);
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ============== Function Prototypes ==============
void stepMotor(int step);
void homeStepper();
void updateStepper();
void readEncoder();
void checkButton();
void simulateTemperature();
void updateLED();
void beep(int duration);
void alertBeep();
void updateDisplay();
void checkTargetReached();

// ============== Variables ==============
// Stepper motor
int currentStep = 0;
int targetStep = 0;
const int STEPS_PER_REV = 600;    // X27.168 steps per revolution
const int MAX_STEPS = 450;         // 270 degrees (3/4 of 600)
unsigned long lastStepTime = 0;
const int STEP_DELAY_US = 1500;    // Microseconds between steps

// Encoder
volatile int encoderPos = 0;
int lastEncoderA = HIGH;
int lastEncoderB = HIGH;
bool buttonPressed = false;
unsigned long lastButtonTime = 0;
const int DEBOUNCE_DELAY = 200;

// Temperature
float currentTemp = 25.0;          // Current measured temperature
float targetTemp = 150.0;          // User-set target temperature
bool targetReached = false;        // Flag for target reached alert
bool alertPlayed = false;          // Flag to prevent repeated alerts

// Display update
unsigned long lastDisplayUpdate = 0;
const int DISPLAY_UPDATE_INTERVAL = 100;  // ms

// Stepper sequence for X27.168 (modified for correct rotation)
const int stepSequence[4][4] = {
  {1, 0, 0, 1},  // Step 0
  {1, 0, 1, 0},  // Step 1
  {0, 1, 1, 0},  // Step 2
  {0, 1, 0, 1}   // Step 3
};

// ============== Setup ==============
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n================================");
  Serial.println("Cooking Terminal v2.0 Starting...");
  Serial.println("================================\n");

  // Initialize I2C for OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  Serial.println("[INIT] I2C initialized");
  
  // Initialize OLED
  Serial.print("[INIT] OLED... ");
  if (!oled.begin(0x3C, true)) {
    Serial.println("FAILED! Check wiring.");
  } else {
    Serial.println("OK");
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SH110X_WHITE);
    oled.setCursor(20, 20);
    oled.println("Cooking Terminal");
    oled.setCursor(35, 35);
    oled.println("Starting...");
    oled.display();
  }

  // Initialize LED
  Serial.print("[INIT] LED... ");
  led.begin();
  led.setBrightness(50);
  led.setPixelColor(0, led.Color(0, 0, 255));  // Blue during startup
  led.show();
  Serial.println("OK");

  // Initialize Stepper pins
  Serial.print("[INIT] Stepper... ");
  pinMode(STEPPER_PIN1, OUTPUT);
  pinMode(STEPPER_PIN2, OUTPUT);
  pinMode(STEPPER_PIN3, OUTPUT);
  pinMode(STEPPER_PIN4, OUTPUT);
  digitalWrite(STEPPER_PIN1, LOW);
  digitalWrite(STEPPER_PIN2, LOW);
  digitalWrite(STEPPER_PIN3, LOW);
  digitalWrite(STEPPER_PIN4, LOW);
  Serial.println("OK");

  // Initialize Encoder pins
  Serial.print("[INIT] Encoder... ");
  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  lastEncoderA = digitalRead(ENCODER_A);
  lastEncoderB = digitalRead(ENCODER_B);
  Serial.println("OK");

  // Initialize Buzzer
  Serial.print("[INIT] Buzzer... ");
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("OK");
  
  // Startup beeps
  beep(100);
  delay(100);
  beep(100);
  delay(100);
  beep(200);

  // Home the stepper
  Serial.println("[INIT] Homing stepper...");
  homeStepper();
  Serial.println("[INIT] Stepper homed");

  // Set LED to green (ready)
  led.setPixelColor(0, led.Color(0, 255, 0));
  led.show();

  Serial.println("\n================================");
  Serial.println("System Ready!");
  Serial.println("================================");
  Serial.println("Controls:");
  Serial.println("  Rotate encoder: Adjust target temp");
  Serial.println("  Press button: Reset to 25C");
  Serial.println("================================\n");
  
  delay(500);
}

// ============== Main Loop ==============
void loop() {
  readEncoder();
  checkButton();
  simulateTemperature();
  checkTargetReached();
  updateLED();
  updateStepper();
  
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(1);
}

// ============== Stepper Functions ==============
void stepMotor(int step) {
  int idx = step % 4;
  if (idx < 0) idx += 4;
  
  digitalWrite(STEPPER_PIN1, stepSequence[idx][0]);
  digitalWrite(STEPPER_PIN2, stepSequence[idx][1]);
  digitalWrite(STEPPER_PIN3, stepSequence[idx][2]);
  digitalWrite(STEPPER_PIN4, stepSequence[idx][3]);
}

void homeStepper() {
  Serial.println("  Sweeping forward...");
  for (int i = 0; i <= 100; i++) {
    stepMotor(i);
    delay(3);
  }
  
  Serial.println("  Sweeping back to zero...");
  for (int i = 100; i >= 0; i--) {
    stepMotor(i);
    delay(3);
  }
  
  digitalWrite(STEPPER_PIN1, LOW);
  digitalWrite(STEPPER_PIN2, LOW);
  digitalWrite(STEPPER_PIN3, LOW);
  digitalWrite(STEPPER_PIN4, LOW);
  
  currentStep = 0;
}

void updateStepper() {
  targetStep = (int)((currentTemp / 300.0) * MAX_STEPS);
  targetStep = constrain(targetStep, 0, MAX_STEPS);
  
  if (micros() - lastStepTime >= STEP_DELAY_US) {
    if (currentStep < targetStep) {
      currentStep++;
      stepMotor(currentStep);
      lastStepTime = micros();
    } else if (currentStep > targetStep) {
      currentStep--;
      stepMotor(currentStep);
      lastStepTime = micros();
    }
  }
}

// ============== Encoder Functions ==============
void readEncoder() {
  int currentA = digitalRead(ENCODER_A);
  int currentB = digitalRead(ENCODER_B);
  
  if (currentA != lastEncoderA && currentA == LOW) {
    if (currentB == HIGH) {
      encoderPos++;
      targetTemp += 5.0;
      beep(10);
    } else {
      encoderPos--;
      targetTemp -= 5.0;
      beep(10);
    }
    
    targetTemp = constrain(targetTemp, 0.0, 300.0);
    alertPlayed = false;
    
    Serial.print("[ENCODER] Target: ");
    Serial.print(targetTemp);
    Serial.println(" C");
  }
  
  lastEncoderA = currentA;
  lastEncoderB = currentB;
}

void checkButton() {
  if (digitalRead(ENCODER_SW) == LOW) {
    if (millis() - lastButtonTime > DEBOUNCE_DELAY) {
      buttonPressed = true;
      lastButtonTime = millis();
      
      currentTemp = 25.0;
      alertPlayed = false;
      
      Serial.println("[BUTTON] Reset to 25C");
      
      beep(50);
      delay(50);
      beep(50);
    }
  }
}

// ============== Temperature Functions ==============
void simulateTemperature() {
  static unsigned long lastSimUpdate = 0;
  const int SIM_INTERVAL = 100;
  
  if (millis() - lastSimUpdate >= SIM_INTERVAL) {
    if (currentTemp < targetTemp - 0.5) {
      currentTemp += 0.3;
    } else if (currentTemp > targetTemp + 0.5) {
      currentTemp -= 0.3;
    }
    lastSimUpdate = millis();
  }
}

void checkTargetReached() {
  if (abs(currentTemp - targetTemp) < 5.0 && !alertPlayed) {
    Serial.println("[ALERT] Target temperature reached!");
    alertBeep();
    alertPlayed = true;
  }
}

// ============== LED Functions ==============
void updateLED() {
  uint32_t color;
  
  if (currentTemp >= 250) {
    static bool flashState = false;
    static unsigned long lastFlash = 0;
    if (millis() - lastFlash > 200) {
      flashState = !flashState;
      lastFlash = millis();
    }
    color = flashState ? led.Color(255, 0, 0) : led.Color(0, 0, 0);
  } else if (currentTemp >= 200) {
    color = led.Color(255, 0, 0);       // Red
  } else if (currentTemp >= 150) {
    color = led.Color(255, 200, 0);     // Yellow
  } else if (currentTemp >= 100) {
    color = led.Color(255, 100, 0);     // Orange
  } else if (currentTemp >= 50) {
    color = led.Color(100, 255, 0);     // Light green
  } else {
    color = led.Color(0, 255, 0);       // Green
  }
  
  led.setPixelColor(0, color);
  led.show();
}

// ============== Buzzer Functions ==============
void beep(int duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void alertBeep() {
  for (int i = 0; i < 3; i++) {
    beep(150);
    delay(100);
  }
}

// ============== Display Functions ==============
void updateDisplay() {
  oled.clearDisplay();
  
  // Title
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Cooking Terminal");
  oled.drawLine(0, 10, 128, 10, SH110X_WHITE);
  
  // Current Temperature
  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print(currentTemp, 1);
  oled.setTextSize(1);
  oled.print(" C");
  
  // Temperature bar
  int barWidth = map((int)currentTemp, 0, 300, 0, 100);
  barWidth = constrain(barWidth, 0, 100);
  oled.drawRect(0, 32, 102, 8, SH110X_WHITE);
  oled.fillRect(1, 33, barWidth, 6, SH110X_WHITE);
  
  // Target marker on bar
  int targetX = map((int)targetTemp, 0, 300, 0, 100);
  targetX = constrain(targetX, 0, 100);
  oled.drawLine(targetX, 30, targetX, 42, SH110X_WHITE);
  
  // Target Temperature
  oled.setTextSize(1);
  oled.setCursor(0, 44);
  oled.print("Target: ");
  oled.print(targetTemp, 0);
  oled.print(" C");
  
  // Status
  oled.setCursor(0, 55);
  if (currentTemp >= 250) {
    oled.print("!! DANGER !!");
  } else if (currentTemp >= 200) {
    oled.print("Status: VERY HOT");
  } else if (abs(currentTemp - targetTemp) < 5.0) {
    oled.print("Status: READY!");
  } else if (currentTemp < targetTemp) {
    oled.print("Status: Heating...");
  } else {
    oled.print("Status: Cooling...");
  }
  
  oled.display();
}
