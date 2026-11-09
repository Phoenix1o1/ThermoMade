#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DHT.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Function prototypes ---
void updatePwmControl(float temp, float setpoint, bool forceEvaluate = false);
void handleSerialInput();
void updateLCD();

// --- Pins and constants ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define RELAY_PIN 5
#define LED_PIN 48
#define NUMPIXELS 1
#define SDA_pin 1
#define SCL_pin 2

// --- WiFi and Preferences ---
const char* ssid = "ESP32-Thermostat";
const char* password = "thermo123";
WebServer server(80);
Preferences preferences;

// --- Sensors and LEDs ---
DHT dht(DHTPIN, DHTTYPE);
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Adjust I2C address if needed

// --- PID parameters (as tuned) ---
float Kp = 10000.0; // ms/°C
float Ki = 3000.0;  // ms/°C·s
float Kd = 2500.0;  // ms/°C/s

const unsigned long ON_MIN  = 180000;    // 3 min
const unsigned long ON_MAX  = 1800000;   // 30 min
const unsigned long OFF_MIN = 600000;    // 10 min
const unsigned long OFF_MAX = 3600000;   // 60 min

// --- State variables ---
float setTemp = 20.0;
int ledBrightness = 80;
bool heating_on = false;
float dhtTemp = NAN, dhtHumidity = NAN;
unsigned long pwmStartTime = 0;
bool pwmOnState = false;
unsigned long currentOnTime = 0;
unsigned long currentOffTime = 0;
unsigned long nextActionMillis = 0;
float lastPIDOutput = 0.0f;

struct LogEntry {
  uint32_t timestamp;
  float tempDHT;
  float humidityDHT;
  float setpoint;
  bool heating;
};
#define LOG_SIZE 360
LogEntry logBuffer[LOG_SIZE];
int logIndex = 0;

unsigned long flashStart = 0;
int flashCount = 0;
bool flashOn = false;
const int maxFlashes = 3;
const unsigned long flashInterval = 150;

unsigned long lastDHT = 0;
unsigned long lastFlash = 0;
unsigned long lastLogTime = 0;
unsigned long lastPrint = 0;

char serialBuffer[5] = {0};
int serialIndex = 0;

enum LedState {
  LED_GLOW_RED, LED_STATIC_RED, LED_GLOW_GREEN, LED_GLOW_BLUE,
  LED_STATIC_BLUE, LED_FLASH_PURPLE, LED_OFF
};
LedState ledState = LED_OFF;

// --- NeoPixel non-blocking state ---
unsigned long lastPixelUpdate = 0;
const unsigned long pixelUpdateInterval = 40; // update NeoPixel max every 40ms (~25 FPS)

uint32_t lastPixelColor = 0;

// Internal state for flashing and blinking
unsigned long lastBlink = 0;
bool blinkOn = false;

// Helper to combine color and brightness to 32-bit color with brightness applied
uint32_t getColorWithBrightness(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  uint8_t br = brightness;
  return pixels.Color((r * br) / 255, (g * br) / 255, (b * br) / 255);
}

// --- LED control functions with non-blocking update ---
void updatePixel(uint8_t r, uint8_t g, uint8_t b, bool breathing = false, uint16_t flashPeriod = 0) {
  unsigned long now = millis();
  if (now - lastPixelUpdate < pixelUpdateInterval) {
    // Limit update rate
    return;
  }
  lastPixelUpdate = now;

  uint32_t color = 0;
  if (breathing) {
    float intensity = (sin(now * 0.001f) + 1.0f) / 2.0f; // 0..1 sine wave
    uint8_t br = (uint8_t)(ledBrightness * intensity);
    color = getColorWithBrightness(r, g, b, br);
  } else if (flashPeriod > 0) {
    if (now - lastBlink > flashPeriod) {
      lastBlink = now;
      blinkOn = !blinkOn;
    }
    color = blinkOn ? getColorWithBrightness(r, g, b, ledBrightness) : 0;
  } else {
    color = getColorWithBrightness(r, g, b, ledBrightness);
  }

  // Only update if color or brightness changed
  if (color != lastPixelColor) {
    pixels.setPixelColor(0, color);
    pixels.show();
    lastPixelColor = color;
  }
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  updatePixel(r, g, b, false, 0);
}

void setBreathing(uint8_t r, uint8_t g, uint8_t b) {
  updatePixel(r, g, b, true, 0);
}

void setFlashing(uint8_t r, uint8_t g, uint8_t b, uint16_t period) {
  updatePixel(r, g, b, false, period);
}


// Flashing used when heating on or off, non-blocking
void flashLedNonBlocking(uint8_t r, uint8_t g, uint8_t b) {
  unsigned long now = millis();
  static unsigned long lastFlashToggle = 0;
  static bool flashOnLocal = false;
  static int flashCountLocal = 0;

  if (flashCountLocal < maxFlashes) {
    if (now - lastFlashToggle >= flashInterval) {
      lastFlashToggle = now;
      flashOnLocal = !flashOnLocal;
      if (!flashOnLocal) {
        flashCountLocal++;
        // Turn off LED briefly
        setColor(0, 0, 0);
      } else {
        setColor(r, g, b);
      }
    }
  } else {
    setColor(0, 0, 0);
  }
}


String getHtml() {
  String html = "<!DOCTYPE html><html><head><title>Thermostat</title></head><body>";
  html += "<h2>ESP32 Thermostat Settings</h2>";
  html += "<form action='/set' method='POST'>";
  html += "Set Temp (&deg;C): <input type='number' step='0.1' name='setTemp' value='" + String(setTemp) + "'><br>";
  html += "LED Brightness (1-255): <input type='range' min='1' max='255' name='brightness' value='" + String(ledBrightness) + "'><br>";
  html += "<input type='submit'></form>";
  html += "<h3>DHT22 Temperature: ";
  html += isnan(dhtTemp) ? "No sensor or invalid data" : String(dhtTemp, 2) + " &deg;C";
  html += "</h3>";
  html += "<h3>DHT22 Humidity: ";
  html += isnan(dhtHumidity) ? "No sensor or invalid data" : String(dhtHumidity, 2) + "%";
  html += "</h3>";
  html += "<h3>Heating: ";
  html += (heating_on ? "ON" : "OFF");
  html += "</h3>";
  html += "<a href='/log'>Download 24h Log (CSV)</a><br><br>";
  html += "<h3>Clients: " + String(WiFi.softAPgetStationNum()) + "</h3>";
  html += "</body></html>";
  return html;
}


String getLogCsv() {
  String csv = "timestamp,tempDHT,humidity,setTemp,heating\n";
  for (int i = 0; i < LOG_SIZE; i++) {
    int idx = (logIndex + i) % LOG_SIZE;
    auto& e = logBuffer[idx];
    csv += String(e.timestamp) + ",";
    csv += String(e.tempDHT, 2) + ",";
    csv += String(e.humidityDHT, 2) + ",";
    csv += String(e.setpoint, 2) + ",";
    csv += (e.heating ? "1" : "0");
    csv += "\n";
  }
  return csv;
}


void handleRoot() { server.send(200, "text/html", getHtml()); }
void handleSet() {
  float prevSetTemp = setTemp;
  if (server.hasArg("setTemp")) setTemp = server.arg("setTemp").toFloat();
  if (server.hasArg("brightness")) ledBrightness = server.arg("brightness").toInt();

  preferences.putFloat("setTemp", setTemp);
  preferences.putInt("ledBrightness", ledBrightness);

  // Update NeoPixel brightness immediately
  pixels.setBrightness(ledBrightness);

  Serial.printf("Settings updated - SetTemp: %.2f °C, LED Brightness: %d\n", setTemp, ledBrightness);
  if (setTemp != prevSetTemp && !isnan(dhtTemp)) {
    updatePwmControl(dhtTemp, setTemp, true);
  }
  handleRoot();
}

void handleLog() {
  String csv = getLogCsv();
  server.sendHeader("Content-Disposition", "attachment; filename=thermostat_log.csv");
  server.send(200, "text/csv", csv);
}

void updateLCD() {
  unsigned long now = millis();

  // Switch page every 10 seconds
  static unsigned long lastPageSwitch = 0;
  static int page = 0;
  if (now - lastPageSwitch > 10000) {
    lastPageSwitch = now;
    page = (page + 1) % 2;
    lcd.clear();
  }

  if (page == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Set:");
    lcd.print(setTemp, 2);
    lcd.print("C ");

    lcd.setCursor(0, 1);
    lcd.print("Cur:");
    if (isnan(dhtTemp)) lcd.print("--.- ");
    else lcd.print(dhtTemp, 2);
    lcd.print("C ");

    lcd.setCursor(0, 2);
    lcd.print("Hum:");
    if (isnan(dhtHumidity)) lcd.print("--.-%");
    else lcd.print(dhtHumidity, 2);
    lcd.print("% ");

    lcd.setCursor(0, 3);
    lcd.print("T:");
    lcd.print(now / 1000);

    lcd.print(" nxt:");
    unsigned long nxt = (nextActionMillis > now) ? (nextActionMillis - now) : 0;
    lcd.print(nxt / 1000);
    lcd.print("s");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("ON:");   lcd.print(currentOnTime / 1000);   lcd.print("s ");
    lcd.setCursor(0, 1);
    lcd.print("OFF:");  lcd.print(currentOffTime / 1000);  lcd.print("s ");
    lcd.setCursor(0, 2);
    lcd.print("PID:");  lcd.print(lastPIDOutput, 0);
    lcd.setCursor(0, 3);
    lcd.print("Heater:"); lcd.print(heating_on ? "ON " : "OFF");
  }
}

// --- The critical, robust state machine with fallback for stale timer ---
void updatePwmControl(float temp, float setpoint, bool forceEvaluate) {
  static float integral = 0.0;
  static float lastError = 0.0;

  float error = setpoint - temp;
  unsigned long now = millis();
  unsigned long elapsed = (pwmStartTime == 0) ? ON_MIN : now - pwmStartTime;
  if (elapsed == 0) elapsed = 1;

  integral += error * (elapsed / 1000.0);
  integral = constrain(integral, -110, 110);
  float derivative = (error - lastError) / (elapsed / 1000.0);
  float output = Kp * error + Ki * integral + Kd * derivative;
  lastError = error;

  // PID Cannot be negative for timing, clip at zero
  if (output < 0) output = 0;
  lastPIDOutput = output;

  unsigned long onTime = constrain((unsigned long)output, ON_MIN, ON_MAX);
  unsigned long offTime = constrain(ON_MAX + OFF_MIN - onTime, OFF_MIN, OFF_MAX);

  currentOnTime = onTime;
  currentOffTime = offTime;

  bool largeError = (error > 2.5);

  bool transitioned = false;
  if (!pwmOnState && (elapsed >= offTime || forceEvaluate || (largeError && elapsed >= OFF_MIN))) {
    relayOn();
    pwmOnState = true;
    pwmStartTime = now;
    nextActionMillis = now + onTime;
    transitioned = true;
    Serial.printf("PWM ON for %lu ms (%.2f min). Error: %.2f °C | Next OFF in %lu ms (%.2f min)\n",
      onTime, onTime / 60000.0, error, onTime, onTime / 60000.0);
    flashCount = 0; flashOn = false; flashStart = now;
  } else if (pwmOnState && (elapsed >= onTime || forceEvaluate)) {
    relayOff();
    pwmOnState = false;
    pwmStartTime = now;
    nextActionMillis = now + offTime;
    transitioned = true;
    Serial.printf("PWM OFF for %lu ms (%.2f min). Error: %.2f °C | Next ON in %lu ms (%.2f min)\n",
      offTime, offTime / 60000.0, error, offTime, offTime / 60000.0);
    flashCount = 0; flashOn = false; flashStart = now;
  }
  // Fallback to always keep nextActionMillis alive and not stuck
  if (!transitioned && (nextActionMillis <= now || nextActionMillis == 0)) {
    nextActionMillis = now + (pwmOnState ? onTime : offTime);
  }
}

void relayOn()  { digitalWrite(RELAY_PIN, HIGH); heating_on = true; }
void relayOff() { digitalWrite(RELAY_PIN, LOW);  heating_on = false; }

void handleSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      if (serialIndex < 4) {
        serialBuffer[serialIndex++] = c;
        if (serialIndex == 4) {
          serialBuffer[4] = '\0';
          int intVal = atoi(serialBuffer);
          float newSetpoint = intVal / 100.0;
          Serial.printf("Received new setpoint via Serial: %s -> %.2f °C\n", serialBuffer, newSetpoint);
          if (setTemp != newSetpoint) {
            setTemp = newSetpoint;
            preferences.putFloat("setTemp", setTemp);
            if (!isnan(dhtTemp))
              updatePwmControl(dhtTemp, setTemp, true);
          }
          serialIndex = 0;
        }
      }
    } else if (c == '\n' || c == '\r') {
      serialIndex = 0;
    } else {
      serialIndex = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_pin, SCL_pin);
  preferences.begin("thermo", false);
  pinMode(RELAY_PIN, OUTPUT);
  relayOff();
  dht.begin();
  WiFi.softAP(ssid, password);
  pixels.begin();

  lcd.init();
  lcd.backlight();

  setTemp = preferences.getFloat("setTemp", 20.0);
  ledBrightness = preferences.getInt("ledBrightness", 80);
  pixels.setBrightness(ledBrightness);

  pwmStartTime = 0;
  nextActionMillis = 0;

  dhtTemp = dht.readTemperature();
  dhtHumidity = dht.readHumidity();

  // Only force heating if actual temp < setTemp at boot
  if (!isnan(dhtTemp) && dhtTemp < setTemp) {
    updatePwmControl(dhtTemp, setTemp, true); // Only force ON if needed
  }

  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/log", handleLog);
  server.begin();

  for (int i = 0; i < LOG_SIZE; i++) {
    logBuffer[i] = {0, NAN, NAN, 0, false};
  }

  Serial.println("Setup complete");
  Serial.printf("Setpoint: %.2f °C\n", setTemp);
}

void loop() {
  server.handleClient();
  handleSerialInput();

  unsigned long nowMillis = millis();
  if (nowMillis - lastDHT > 5000) {
    lastDHT = nowMillis;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
      Serial.println("Warning: Failed to read from DHT sensor");
      dhtTemp = NAN;
      dhtHumidity = NAN;
    } else {
      dhtTemp = t;
      dhtHumidity = h;
    }
  }

  if (!isnan(dhtTemp)) {
    updatePwmControl(dhtTemp, setTemp);
  } else {
    relayOff();
  }

  float delta = !isnan(dhtTemp) ? (dhtTemp - setTemp) : 0;

  if (isnan(dhtTemp))
    ledState = LED_FLASH_PURPLE;
  else if (delta > 3)
    ledState = LED_STATIC_RED;
  else if (delta > 0)
    ledState = LED_GLOW_RED;
  else if (delta < -3)
    ledState = LED_STATIC_BLUE;
  else if (delta < 0)
    ledState = LED_GLOW_BLUE;
  else if (abs(delta) <= 1)
    ledState = LED_GLOW_GREEN;

  switch (ledState) {
    case LED_GLOW_RED: setBreathing(255, 0, 0); break;
    case LED_STATIC_RED: setColor(255, 0, 0); break;
    case LED_GLOW_GREEN: setBreathing(0, 255, 0); break;
    case LED_GLOW_BLUE: setBreathing(50, 180, 255); break;
    case LED_STATIC_BLUE: setColor(50, 180, 255); break;
    case LED_FLASH_PURPLE: setFlashing(180, 0, 255, 500); break;
    case LED_OFF: setColor(0, 0, 0); break;
  }

  if (nowMillis - lastFlash > 10000) {
    lastFlash = nowMillis;
    flashCount = 0;
    flashOn = false;
    flashStart = nowMillis;
  }

  if (heating_on) flashLedNonBlocking(255, 140, 0);
  else flashLedNonBlocking(0, 0, 255);

  updateLCD();

  if (nowMillis - lastPrint > 5000) {
    lastPrint = nowMillis;
    Serial.printf("Status - Temp: %.2f °C, Humidity: %.2f %%, Setpoint: %.2f °C, Heating: %s\n",
                  dhtTemp, dhtHumidity, setTemp, heating_on ? "ON" : "OFF");
  }

  if (nowMillis - lastLogTime > 240000) {
    lastLogTime = nowMillis;
    uint32_t ts = (uint32_t)(millis() / 1000);
    logBuffer[logIndex] = {ts, dhtTemp, dhtHumidity, setTemp, heating_on};
    logIndex = (logIndex + 1) % LOG_SIZE;
  }
  yield();
  delay(1);
}
