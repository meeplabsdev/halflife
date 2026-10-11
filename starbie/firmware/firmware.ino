#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <DHT.h>
#include <math.h>
#include <stdint.h>

const int DHT_DAT_PIN = 3;
const int BUT_ONE_PIN = 4;
const int BUT_TWO_PIN = 5;
const int I2C_SDA_PIN = 6;
const int I2C_SCL_PIN = 7;

const uint8_t OLED_ADDRESS = 0x3C;
const uint8_t MPU6050_ADDRESS = 0x68;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const float SCREEN_CENTER_X = 64;
const float SCREEN_CENTER_Y = 32;

const float GYRO_DEADZONE = 0.8f;
const float SHAKE_THRESH = 7.0f;
const float GRAVITY = 9.80665f;

const uint16_t BUTTON_DEBOUNCE_MS = 30;
const uint16_t SHAKE_COOLDOWN_MS = 650;
const uint16_t MPU_READ_INTERVAL_MS = 30;
const uint16_t DHT_READ_INTERVAL_MS = 2200;

struct ButtonState {
  int pin;
  bool stableState;
  bool lastRawState;
  uint32_t lastChangedAt;
};

ButtonState buttonOne = { BUT_ONE_PIN, HIGH, HIGH, 0 };
ButtonState buttonTwo = { BUT_TWO_PIN, HIGH, HIGH, 0 };
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_MPU6050 mpu;
DHT dht(DHT_DAT_PIN, DHT11);

bool mpuFound = false;
bool dhtFound = false;
float accelerationX = 0.0f;
float accelerationY = 0.0f;
float accelerationZ = GRAVITY;
float temperature = NAN;
float humidity = NAN;
float gyroBallX = SCREEN_CENTER_X;
float gyroBallY = SCREEN_CENTER_Y;
float gyroCenterAccelerationX = 0.0f;
float gyroCenterAccelerationY = 0.0f;
uint32_t lastMpuReadAt = 0;
uint32_t lastDhtReadAt = 0;
uint32_t lastShakeAt = 0;
uint8_t buttonOnePresses = 0;
uint8_t buttonTwoPresses = 0;

// button logic
void initButton(ButtonState &button) {
  pinMode(button.pin, INPUT_PULLUP);
  button.stableState = digitalRead(button.pin);
  button.lastRawState = button.stableState;
  button.lastChangedAt = millis();
}

bool wasPressed(ButtonState &button) {
  const bool rawState = digitalRead(button.pin);
  const uint32_t now = millis();

  if (rawState != button.lastRawState) {
    button.lastRawState = rawState;
    button.lastChangedAt = now;
  }

  if (rawState != button.stableState && now - button.lastChangedAt >= BUTTON_DEBOUNCE_MS) {
    button.stableState = rawState;
    return button.stableState == LOW;
  }

  return false;
}

void updateButtons() {
  if (wasPressed(buttonOne)) buttonOnePresses++;
  if (wasPressed(buttonTwo)) buttonTwoPresses++;
}

// mpu logic
void updateMpu() {
  const uint32_t now = millis();
  if (!mpuFound || now - lastMpuReadAt < MPU_READ_INTERVAL_MS) {
    return;
  }

  lastMpuReadAt = now;
  sensors_event_t acceleration;
  sensors_event_t gyro;
  sensors_event_t sensorTemperature;

  mpu.getEvent(&acceleration, &gyro, &sensorTemperature);
  accelerationX = acceleration.acceleration.x;
  accelerationY = acceleration.acceleration.y;
  accelerationZ = acceleration.acceleration.z;
}

// dht logic
void updateDht() {
  const uint32_t now = millis();
  if (!dhtFound || now - lastDhtReadAt < MPU_READ_INTERVAL_MS) {
    return;
  }

  lastDhtReadAt = now;
  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();
  if (!isnan(newHumidity)) humidity = newHumidity;
  if (!isnan(newTemperature)) temperature = newTemperature;
}

// gyro & accelerometer
void updateGyroBall() {
  float xTilt = constrain(accelerationX - gyroCenterAccelerationX, -6.0, 6.0);
  float yTilt = constrain(-(accelerationY - gyroCenterAccelerationY), -6.0, 6.0);
  float targetX = SCREEN_CENTER_X + xTilt * 37.0 / 6.0;
  float targetY = SCREEN_CENTER_Y + yTilt * 22.0 / 6.0;

  gyroBallX += (targetX - gyroBallX) * 0.2;
  gyroBallY += (targetY - gyroBallY) * 0.2;
}

bool wasShaken() {
  if (!mpuFound) return false;

  const uint32_t now = millis();
  float magnitude = sqrtf(accelerationX * accelerationX
                          + accelerationY * accelerationY
                          + accelerationZ * accelerationZ);

  if (fabsf(magnitude - GRAVITY) >= SHAKE_THRESH && now - lastShakeAt >= SHAKE_COOLDOWN_MS) {
    lastShakeAt = now;
    return true;
  }

  return false;
}

void draw() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.drawCircle(SCREEN_CENTER_X, SCREEN_CENTER_Y, 11, SSD1306_WHITE);
  display.fillCircle(static_cast<int>(gyroBallX), static_cast<int>(gyroBallY), 4, SSD1306_WHITE);

  display.setCursor(4, 4);
  if (!isnan(temperature)) {
    display.print("TEMP ");
    display.print(static_cast<int>(temperature));
    display.print("C");
  } else {
    display.print("TEMP --");
  }

  display.setCursor(4, 12);
  if (!isnan(humidity)) {
    display.print("HUMD ");
    display.print(static_cast<int>(humidity));
    display.print("%");
  } else {
    display.print("HUMD --");
  }

  display.setCursor(4, 20);
  display.print("BUT1 ");
  display.print(buttonOnePresses);

  display.setCursor(4, 28);
  display.print("BUT2 ");
  display.print(buttonTwoPresses);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  initButton(buttonOne);
  initButton(buttonTwo);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found.");
    while (true) {
      delay(10);
    }
  }

  mpuFound = mpu.begin(MPU6050_ADDRESS, &Wire);
  if (mpuFound) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("MPU6050 not found.");
  }

  dht.begin();
  dhtFound = true;
}

void loop() {
  updateMpu();
  updateDht();
  updateButtons();
  updateGyroBall();

  draw();
  delay(16);
}