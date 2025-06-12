#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SoftwareSerial.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Keypad
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {30, 31, 32, 33};
byte colPins[COLS] = {34, 35, 36, 37};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Motor control
#define stepPin 9
#define dirPin 8
#define enablePin 10

// Buttons & Indicators
#define pauseBtn 42
#define ledRun 40
#define ledPause 41
#define buzzer 43

// Wi-Fi (ESP8266 on Serial3)
SoftwareSerial ESP(14, 15); // RX, TX

// Variables
String inputBuffer = "";
float flowRate = 0;
float totalVolume = 0;
float deliveredVolume = 0;
float stepsPerMl = 200;
float stepsPerSecond = 0;
bool isRunning = false;
bool isPaused = false;

unsigned long infusionStartTime = 0;
unsigned long lastStepTime = 0;
unsigned long lastUpdate = 0;

String ssid = "", password = "";

enum Screen { HOME, SET_FLOW, SET_VOLUME, CONFIRM, RUNNING, COMPLETED, WIFI_MENU, RETURN_PROMPT, RETURNING };
Screen currentScreen = HOME;

void setup() {
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("DoseMate Pump"); delay(2000); lcd.clear();

  pinMode(stepPin, OUTPUT); pinMode(dirPin, OUTPUT); pinMode(enablePin, OUTPUT);
  pinMode(pauseBtn, INPUT_PULLUP); pinMode(ledRun, OUTPUT);
  pinMode(ledPause, OUTPUT); pinMode(buzzer, OUTPUT);
  digitalWrite(enablePin, HIGH); digitalWrite(dirPin, HIGH);

  ESP.begin(9600); Serial.begin(9600);
  lcd.setCursor(0, 0); lcd.print("System Ready..."); delay(1000); lcd.clear();
}

void loop() {
  if (digitalRead(pauseBtn) == LOW && isRunning) {
    isPaused = !isPaused;
    digitalWrite(ledPause, isPaused ? HIGH : LOW);
    delay(500);
  }

  char key = keypad.getKey();
  switch (currentScreen) {
    case HOME:
      lcd.setCursor(0, 0); lcd.print("A:Input B:WiFi ");
      if (key == 'A') { inputBuffer = ""; lcd.clear(); lcd.print("Flow Rate(ml/hr):"); currentScreen = SET_FLOW; }
      if (key == 'B') { currentScreen = WIFI_MENU; scanWiFi(); }
      break;

    case SET_FLOW:
      if (handleInput(key)) { flowRate = inputBuffer.toFloat(); inputBuffer = ""; lcd.clear(); lcd.print("Volume (ml):"); currentScreen = SET_VOLUME; }
      break;

    case SET_VOLUME:
      if (handleInput(key)) { totalVolume = inputBuffer.toFloat(); inputBuffer = ""; lcd.clear(); lcd.print("Start Infuse?"); lcd.setCursor(0,1); lcd.print("A:Yes B:No"); currentScreen = CONFIRM; }
      break;

    case CONFIRM:
      if (key == 'A') startInfusion();
      if (key == 'B') { lcd.clear(); currentScreen = HOME; }
      break;

    case RUNNING:
      if (key == 'C') {
        lcd.clear(); lcd.print("Are you sure?"); lcd.setCursor(0, 1); lcd.print("A:Yes  B:No");
        while (true) {
          char k = keypad.getKey();
          if (k == 'A') { stopInfusion(); break; }
          else if (k == 'B') { currentScreen = RUNNING; break; }
        }
      }
      updateInfusion();
      break;

    case COMPLETED:
      lcd.clear(); lcd.print("Infusion Done!");
      digitalWrite(buzzer, HIGH); delay(3000); digitalWrite(buzzer, LOW);
      sendWhatsAppNotification();
      lcd.clear(); lcd.print("Return syringe?"); lcd.setCursor(0, 1); lcd.print("D:Yes");
      currentScreen = RETURN_PROMPT;
      break;

    case RETURN_PROMPT:
      if (key == 'D') {
        lcd.clear(); lcd.print("Returning...");
        currentScreen = RETURNING;
        returnToInitialPosition();
      }
      break;

    case RETURNING:
      break;

    default: break;
  }
}

bool handleInput(char key) {
  if (key) {
    if (key >= '0' && key <= '9') {
      inputBuffer += key;
      lcd.setCursor(0, 1); lcd.print(inputBuffer);
    }
    if (key == 'A') return true;
    if (key == 'C') { inputBuffer = ""; lcd.setCursor(0, 1); lcd.print("                "); }
  }
  return false;
}

void startInfusion() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Starting..."); delay(1000);
  isRunning = true; isPaused = false;
  deliveredVolume = 0;
  infusionStartTime = millis();
  stepsPerSecond = (flowRate / 3600.0) * stepsPerMl;
  digitalWrite(ledRun, HIGH); digitalWrite(enablePin, LOW);
  currentScreen = RUNNING;
  lcd.clear();
}

void updateInfusion() {
  unsigned long now = millis();
  if (!isPaused && isRunning && deliveredVolume < totalVolume) {
    if ((now - lastStepTime) >= (1000.0 / stepsPerSecond)) {
      digitalWrite(stepPin, HIGH); delayMicroseconds(500); digitalWrite(stepPin, LOW);
      lastStepTime = now;
      deliveredVolume += (1.0 / stepsPerMl);
    }
    if ((now - lastUpdate) >= 1000) {
      lastUpdate = now; displayStatus();
    }
  }
  if (deliveredVolume >= totalVolume) {
    isRunning = false; currentScreen = COMPLETED;
    digitalWrite(ledRun, LOW); digitalWrite(enablePin, HIGH);
  }
}

void displayStatus() {
  lcd.clear();
  float elapsedSec = (millis() - infusionStartTime) / 1000.0;
  float progress = (deliveredVolume / totalVolume) * 100.0;
  lcd.setCursor(0, 0); lcd.print("Infuse:"); lcd.print(deliveredVolume, 1); lcd.print("/"); lcd.print(totalVolume, 0);
  lcd.setCursor(0, 1); lcd.print("Done: "); lcd.print(progress); lcd.print("%");
}

void stopInfusion() {
  isRunning = false; isPaused = false;
  digitalWrite(ledRun, LOW); digitalWrite(ledPause, LOW);
  digitalWrite(enablePin, HIGH);
  lcd.clear(); lcd.print("Infusion Stopped"); delay(2000);
  currentScreen = HOME;
}

void scanWiFi() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("Scanning WiFi...");
  ESP.println("AT+CWLAP"); delay(3000); ESP.flush();
  lcd.clear(); lcd.print("Enter SSID:"); inputBuffer = "";
  while (true) {
    char key = keypad.getKey();
    if (handleInput(key)) { ssid = inputBuffer; inputBuffer = ""; lcd.clear(); lcd.print("Enter Pass:"); break; }
  }
  while (true) {
    char key = keypad.getKey();
    if (handleInput(key)) { password = inputBuffer; inputBuffer = ""; connectWiFi(); break; }
  }
}

void connectWiFi() {
  ESP.println("AT+CWQAP"); delay(1000);
  ESP.println("AT+CWMODE=1"); delay(1000);
  String cmd = "AT+CWJAP=\"" + ssid + "\",\"" + password + "\"";
  ESP.println(cmd); delay(5000);
  ESP.println("AT+CIPSTATUS"); delay(2000);
  String response = "";
  while (ESP.available()) response += char(ESP.read());
  if (response.indexOf("STATUS:2") >= 0 || response.indexOf("STATUS:3") >= 0 || response.indexOf("STATUS:4") >= 0) {
    lcd.clear(); lcd.print("WiFi Connected!");
  } else {
    lcd.clear(); lcd.print("Conn Failed!");
  }
  delay(2000);
  currentScreen = HOME;
}

String urlEncode(String str) {
  String encoded = "";
  char c, code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) encoded += c;
    else {
      code1 = (c & 0xf) + '0'; if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf; code0 = c + '0'; if (c > 9) code0 = c - 10 + 'A';
      encoded += '%'; encoded += code0; encoded += code1;
    }
  }
  return encoded;
}

//void sendWhatsAppNotification() {
  String phone = "<YOUR_PHONE_NUMBER_WITH_COUNTRY_CODE>";
  String apiKey = "<YOUR_CALLMEBOT_API_KEY>";
  unsigned long infusionEndTime = millis();
  unsigned long totalMillis = infusionEndTime - infusionStartTime;
  int seconds = (totalMillis / 1000) % 60;
  int minutes = (totalMillis / (1000 * 60)) % 60;
  int hours = (totalMillis / (1000 * 60 * 60));
  char timeBuffer[10]; sprintf(timeBuffer, "%02d:%02d:%02d", hours, minutes, seconds);
  String message = "\xF0\x9F\x92\x89 *DoseMate Pump - Infusion Complete!*\n";
  message += "\xE2\x9C\x85 Flow Rate: " + String(flowRate, 1) + " ml/hr\n";
  message += "\xE2\x9C\x85 Volume: " + String(totalVolume, 1) + " ml\n";
  message += "\xE2\x8F\xB1 Time Taken: " + String(timeBuffer) + "\n";
  message += "\n\xF0\x9F\x8E\x89 Thank you for trusting DoseMate!\n";
  message += "Precision delivered, care assured. \xF0\x9F\x92\x9A";
  ESP.println("AT+CIPSTART=\"TCP\",\"api.callmebot.com\",80"); delay(2000);
  String httpRequest = "GET /whatsapp.php?phone=" + phone + "&text=" + urlEncode(message) + "&apikey=" + apiKey + " HTTP/1.1\r\nHost: api.callmebot.com\r\n\r\n";
  ESP.print("AT+CIPSEND="); ESP.println(httpRequest.length()); delay(1000);
  ESP.print(httpRequest); delay(3000);//
}

void returnToInitialPosition() {
  digitalWrite(enablePin, LOW);
  digitalWrite(dirPin, LOW);
  float returnRate = 8000.0;
  float stepsPerSecondReturn = (returnRate / 3600.0) * stepsPerMl;
  unsigned long stepInterval = 1000.0 / stepsPerSecondReturn;
  unsigned long returnSteps = totalVolume * stepsPerMl;
  for (unsigned long i = 0; i < returnSteps; i++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(500);
    digitalWrite(stepPin, LOW);
    delay(stepInterval);
  }
  digitalWrite(enablePin, HIGH);
  digitalWrite(dirPin, HIGH);
  lcd.clear(); lcd.print("Syringe Reset!"); delay(2000);
  currentScreen = HOME;
}
