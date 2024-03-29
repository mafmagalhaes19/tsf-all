#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <EEPROM.h>

BLECharacteristic *pCharacteristic;

bool deviceConnected = false;

const int ledPinSAFE = 2; //safe mode pin
const int pin13 = 13; // safety digital output pin 
const int pin34 = 34; // input pin that checks when the motor is running
const int pin12 = 12; // motor output pin

int activationCount = 0; // current number of motor ativations
int maxActivations = 0; // maximum number of motor activations
int timeSafeMode = 120; // safe mode timer (OPTIONS - change timeSafeMode according to the wanted time for safe mode; the variable is in seconds)

unsigned long startTime12 = 0; // starting time of pin 12
unsigned long startTime34 = 0; // starting time of pin 34
unsigned long duration12 = 0; // duration of pin 12
unsigned long intervalDuration = 0; // time between starts set by command B in milliseconds
unsigned long previousActivationTime = 0; // last activation of pin 12
unsigned long previousTimeLPS = 0; 
unsigned long currentTimeLPS = 0;
const long intervalLPS = 1000;

bool pin12state = false;
bool pin34Activated = false;
bool safeModeActive = false;

String lastAValue = "";
String lastBValue = "";

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define ADDR_PREVIOUS_A_VALUE 0
#define ADDR_PREVIOUS_B_VALUE (ADDR_PREVIOUS_A_VALUE + sizeof(lastAValue))

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


void setup() {
  Serial.begin(115200);
  pinMode(pin13, OUTPUT);
  pinMode(pin34, INPUT);
  pinMode(pin12, OUTPUT);
  pinMode(ledPinSAFE, OUTPUT);
  digitalWrite(pin13, LOW);

  //OPTIONS - change the name of the board here
  BLEDevice::init("TheSmartFeeder");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID); 

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE
                    );

  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->setMinPreferred(0x00); 
  pAdvertising->setMaxPreferred(0x00);
  BLEDevice::startAdvertising();
  Serial.println("BLE device is ready to be connected");

  EEPROM.begin(512);

  EEPROM.get(ADDR_PREVIOUS_A_VALUE, lastAValue);
  EEPROM.get(ADDR_PREVIOUS_B_VALUE, lastBValue);
  Serial.println("lastAValue: " + lastAValue);
  Serial.println("lastBValue: " + lastBValue);

  if (lastAValue.startsWith("A")) {
    duration12 = lastAValue.substring(1).toInt(); // stores the duration given by the user
    startTime12 = millis(); // start time of activation of pin 12
    digitalWrite(pin12, HIGH); // turns the pin 12 on
    pin12state = true;
    activationCount = 1; // reboots the counter of daily activations

    Serial.println("Pin 12 is ON - setup");
  }        
  if (lastBValue.startsWith("B")) {
    maxActivations = lastBValue.substring(1).toInt();
    intervalDuration = 24 * 60 * 60 * 1000 / maxActivations;
    previousActivationTime = millis(); 

    Serial.println("Time set to activate the motor: " + String(duration12));
    Serial.println("Maximum number of daily activations: " + String(maxActivations));
  }
}

void loop() {
  if (!deviceConnected) {
    BLEDevice::startAdvertising(); // Restart advertising
    delay(1000); // Delay to allow advertising to start
  }

  // Ensures that the safe mode is off
  if (digitalRead(pin13) == LOW) { 
    if (deviceConnected) {
      if (pCharacteristic->getValue().length() > 0) {
        String value = pCharacteristic->getValue().c_str();

        // Command A
        if (value.startsWith("A") && value != lastAValue) {
          lastAValue = value;
          EEPROM.put(ADDR_PREVIOUS_A_VALUE, lastAValue);
          EEPROM.commit();

          duration12 = value.substring(1).toInt(); // stores the duration given by the user
          startTime12 = millis(); // start time of activation of pin 12
          digitalWrite(pin12, HIGH); // turns the pin 12 on
          pin12state = true;
          activationCount = 1; // reboots the counter of daily activations

          Serial.println("Pin 12 is ON - new command");
        }        
        else if (value.startsWith("B") && value != lastBValue) {
          lastBValue = value;
          EEPROM.put(ADDR_PREVIOUS_B_VALUE, lastBValue);
          EEPROM.commit();

          maxActivations = value.substring(1).toInt();
          intervalDuration = 24 * 60 * 60 * 1000 / maxActivations;
          //intervalDuration = 1000 * maxActivations; // for tests only
          previousActivationTime = millis(); 

          Serial.println("Time set to activate the motor: " + String(duration12));
          Serial.println("Maximum number of daily activations: " + String(maxActivations));
        }
      }
    }


    // Check if the specified time in A for pin 12 has already passed
    if (pin12state && millis() - startTime12 >= duration12 * 1000) {

      digitalWrite(pin12, LOW); // turns off pin 12
      pin12state = false;

      Serial.println("Pin 12 is OFF");
      Serial.print("Current number of daily motor activations: ");
      Serial.println(activationCount);
    }

    // Check if the specified time in B has already passed
    if (maxActivations > 0 && millis() - previousActivationTime >= intervalDuration) {
      // Turns pin 12 on for the next activation
      startTime12 = millis();
      digitalWrite(pin12, HIGH);
      pin12state = true;
      activationCount++;
      previousActivationTime = millis();

      Serial.println("Pin 12 is ON");
    }

    // Check if the counter has reached the daily activations
    if (maxActivations == activationCount) {
      activationCount = 0;
    }
  }


  // SAFE MODE
  // TODO - Fix and test Safe mode -> pin34 is always with value 0

  // Check if pin34 is receiving energy
  if (analogRead(pin34) > 50 && !pin34Activated) {
    pin34Activated = true;
    startTime34 = millis();
  }

  // Check if pin 34 has been on for more than 10 seconds
  else if (pin34Activated && millis() - startTime34 >= timeSafeMode * 1000) {
    digitalWrite(pin13, HIGH);

    Serial.println("SAFE MODE ON");
  }

  if (analogRead(pin34) < 50) {
    pin34Activated = false;
  }

  // Safe Mode LED blinks
  if (digitalRead(pin13) == HIGH) {
    currentTimeLPS = millis();
    if (currentTimeLPS - previousTimeLPS >= intervalLPS) {
      previousTimeLPS = currentTimeLPS;
      if (digitalRead(ledPinSAFE) == LOW) {
        digitalWrite(ledPinSAFE, HIGH);
      } else {
        digitalWrite(ledPinSAFE, LOW);
      }
    }
  }

}
