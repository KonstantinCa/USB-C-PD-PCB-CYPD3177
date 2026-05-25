// -----------------------------------------------------------------------------
// Finite State Machine (FSM) zur Steuerung der USB-PD-Kommunikation
// INIT -> IDLE -> SENDEN -> WAIT -> EMPFANGEN -> IDLE
// -----------------------------------------------------------------------------

// Interrupt-Pin des CYPD3177 (aktiv LOW)
#define CYPD_INT_PIN 43

#include <Wire.h>   // I2C-Kommunikation
#include <math.h>   // NAN für Fehlerfälle bei Messwerten

// I2C-Adresse des CYPD3177
#define CYPD3177_I2C_ADDR 0x08

// I2C-Pins des ESP32
#define SDA_PIN 15
#define SCL_PIN 7

// Index für das ausgewählte PDO
size_t i = 0;

// Gemessene VBUS-Spannung
float v = 0.0;

// Wichtige Register- und Kommandoadressen des CYPD3177
#define CYPD_PD_STATUS_REG          0x1008
#define CYPD_DATA_MEMORY_START_ADDR 0x1800
#define CYPD_SELECT_SINK_PDO_CMD    0x1005
#define CYPD_PD_RESPONSE_CMD        0x1400

// Definition der FSM-Zustände
enum State {
  INIT,
  IDLE,
  SENDEN,
  WAIT,
  EMPFANGEN
};

// Unterstützte Sink-PDOs (Fixed Supply PDOs, USB-PD-konform codiert)
const uint32_t pdos[] = {
  0x000190C8,  // 5 V, 2 A
  0x0002D0C8,  // 9 V, 2 A
  0x0003C0E1,  // 12 V, 2.25 A
  0x0004B0B4,  // 15 V, 1.8 A
  0x00064064   // 20 V, 1.5 A
};

// Klartext-Ausgabe für serielle Konsole
String text[] = {
  "5V, 2A",
  "9V, 2A",
  "12V, 2.25A",
  "15V, 1.8A",
  "20V, 1.5A"
};

// Aktueller und vorheriger FSM-Zustand
State state = INIT;
State lastState = INIT;

// Steuer-Flags
bool cmdReceived = false;   // Serielle Eingabe empfangen
bool txDone = false;        // PDO erfolgreich gesendet
bool dataReady = false;     // Reserviert (nicht aktiv genutzt)
bool errorFlag = false;     // Fehlerstatus

// Funktionsprototypen
bool checkPDStatus();
void selectPDO(uint32_t PDO);
float readVbusVoltage_V();
bool waitIntrAndReadResponses(uint32_t timeoutMs,
                              uint8_t *intrStatus,
                              uint8_t pdResp[4],
                              uint8_t devResp[2]);

// -----------------------------------------------------------------------------
// Initialisierung
// -----------------------------------------------------------------------------
void setup() {
  pinMode(CYPD_INT_PIN, INPUT_PULLUP); // INT# ist aktiv LOW

  Wire.begin(SDA_PIN, SCL_PIN);        // I2C initialisieren
  Serial.begin(115200);                // Serielle Konsole
  delay(1000);

  Serial.println("Setup Complete");
  state = INIT;
}

// -----------------------------------------------------------------------------
// Hauptschleife mit FSM
// -----------------------------------------------------------------------------
void loop() {

  // Zustandswechsel debuggen
  if (state != lastState) {
    Serial.print("STATE -> ");
    switch (state) {
      case INIT: Serial.println("INIT"); break;
      case IDLE: Serial.println("IDLE"); break;
      case SENDEN: Serial.println("SENDEN"); break;
      case WAIT: Serial.println("WAIT"); break;
      case EMPFANGEN: Serial.println("EMPFANGEN"); break;
    }
    lastState = state;
  }

  switch (state) {

    // -------------------------------------------------------------------------
    // INIT: Rücksetzen aller Status-Flags
    // -------------------------------------------------------------------------
    case INIT:
      cmdReceived = false;
      txDone = false;
      dataReady = false;
      errorFlag = false;
      state = IDLE;
      break;

    // -------------------------------------------------------------------------
    // IDLE: Warten auf serielle Benutzereingabe
    // -------------------------------------------------------------------------
    case IDLE:
      if (!Serial.available()) {
        delay(10);
        break;
      }
      cmdReceived = true;
      state = SENDEN;
      break;

    // -------------------------------------------------------------------------
    // SENDEN: Kommando auswerten und ggf. PDO senden
    // -------------------------------------------------------------------------
    case SENDEN: {
      if (!cmdReceived) {
        state = IDLE;
        break;
      }
      cmdReceived = false;

      // Prüfen, ob bereits ein PD-Vertrag besteht
      if (!checkPDStatus()) {
        Serial.println("PD Status does not indicate an established contract.");
        state = IDLE;
        break;
      }

      Serial.println("PD established contract.");

      // Kommando einlesen
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      txDone = false;

      // STATUS-Kommando: nur Antworten auslesen
      if (cmd == "STATUS") {
        state = EMPFANGEN;
        break;
      }
      // PDO-Auswahl
      else if (cmd == "5V2A")        { i = 0; txDone = true; }
      else if (cmd == "9V2A")        { i = 1; txDone = true; }
      else if (cmd == "12V2.25A")    { i = 2; txDone = true; }
      else if (cmd == "15V1.8A")     { i = 3; txDone = true; }
      else if (cmd == "20V1.5A")     { i = 4; txDone = true; }
      else {
        Serial.println("Ungueltiges Kommando");
        state = IDLE;
        break;
      }

      // PDO an CYPD3177 übertragen
      Serial.println("Voltage: " + text[i]);
      selectPDO(pdos[i]);

      state = WAIT;
      break;
    }

    // -------------------------------------------------------------------------
    // WAIT: Warten auf Interrupt des CYPD3177
    // -------------------------------------------------------------------------
    case WAIT:
      if (digitalRead(CYPD_INT_PIN) == LOW) {
        state = EMPFANGEN;
      }
      break;

    // -------------------------------------------------------------------------
    // EMPFANGEN: Interrupt-Status und Responses auslesen
    // -------------------------------------------------------------------------
    case EMPFANGEN: {
      uint8_t intr = 0;
      uint8_t pd[4];
      uint8_t dev[2];

      // Warten auf INT# und Auslesen der Response-Register
      if (!waitIntrAndReadResponses(500, &intr, pd, dev)) {
        Serial.println("INTR timeout");
        state = IDLE;
        break;
      }

      Serial.print("INTERRUPT=0x");
      Serial.println(intr, HEX);

      // VBUS-Spannung nach kurzer Stabilisierung messen
      delay(400);
      v = readVbusVoltage_V();

      if (!isnan(v)) {
        Serial.print("VBUS = ");
        Serial.print(v, 1);
        Serial.println(" V");
      }

      state = IDLE;
      break;
    }
  }
}
bool checkPDStatus() {
  uint8_t status[4];

  Wire.beginTransmission(CYPD3177_I2C_ADDR);
  Wire.write(CYPD_PD_STATUS_REG & 0xFF);
  Wire.write((CYPD_PD_STATUS_REG >> 8) & 0xFF);
  Wire.endTransmission();

  if (Wire.requestFrom(CYPD3177_I2C_ADDR, 4) != 4) {
    Serial.println("Error: Could not read the complete PD status.");
    return false;
  }

  status[0] = Wire.read();
  status[1] = Wire.read();
  status[2] = Wire.read();
  status[3] = Wire.read();

  // Check if status is 00 A4 05 00
  return (status[0] == 0x00 && status[1] == 0xA4 && status[2] == 0x05 && status[3] == 0x00);
}

void selectPDO(uint32_t PDO) {
  const uint8_t snkp[4] = { 0x50, 0x4B, 0x4E, 0x53 };

  Wire.beginTransmission(CYPD3177_I2C_ADDR);
  Wire.write(CYPD_DATA_MEMORY_START_ADDR & 0xFF);
  Wire.write((CYPD_DATA_MEMORY_START_ADDR >> 8) & 0xFF);

  Wire.write(snkp, 4);

  uint32_t onepdo[] = { PDO };
  for (int k = 0; k < 7; k++) {
    if (k < (int)(sizeof(onepdo) / sizeof(onepdo[0]))) {
      Wire.write((uint8_t*)&onepdo[k], 4);
    } else {
      Wire.write(0x00); Wire.write(0x00); Wire.write(0x00); Wire.write(0x00);
    }
  }

  Wire.endTransmission();

  Wire.beginTransmission(CYPD3177_I2C_ADDR);
  Wire.write(CYPD_SELECT_SINK_PDO_CMD & 0xFF);
  Wire.write((CYPD_SELECT_SINK_PDO_CMD >> 8) & 0xFF);
  Wire.write(1);
  Wire.endTransmission();
}

float readVbusVoltage_V() {
  const uint16_t BUS_VOLTAGE_REG = 0x100D;

  Wire.beginTransmission(CYPD3177_I2C_ADDR);
  Wire.write(BUS_VOLTAGE_REG & 0xFF);
  Wire.write((BUS_VOLTAGE_REG >> 8) & 0xFF);

  if (Wire.endTransmission(false) != 0) {
    return NAN;
  }

  if (Wire.requestFrom(CYPD3177_I2C_ADDR, (uint8_t)1) != 1) {
    return NAN;
  }

  uint8_t raw = Wire.read();
  return raw * 0.1f;  // 100 mV Schritte -> Volt
}

bool waitIntrAndReadResponses(uint32_t timeoutMs,
                              uint8_t *intrStatus,
                              uint8_t pdResp[4],
                              uint8_t devResp[2]) {
  const uint16_t REG_INTERRUPT    = 0x0006; // R/W1C
  const uint16_t REG_DEV_RESPONSE = 0x007E; // 2 bytes
  const uint16_t REG_PD_RESPONSE  = 0x1400; // 4 bytes

  auto readBytes = [&](uint16_t reg, uint8_t *buf, uint8_t len) -> bool {
    Wire.beginTransmission(CYPD3177_I2C_ADDR);
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write((uint8_t)((reg >> 8) & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(CYPD3177_I2C_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
  };

  auto write8 = [&](uint16_t reg, uint8_t val) -> bool {
    Wire.beginTransmission(CYPD3177_I2C_ADDR);
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write((uint8_t)((reg >> 8) & 0xFF));
    Wire.write(val);
    return (Wire.endTransmission(true) == 0);
  };

  // 1) Auf INTR# warten (aktiv LOW)
  uint32_t t0 = millis();
  while (digitalRead(CYPD_INT_PIN) != LOW) {
    if (timeoutMs && (millis() - t0 >= timeoutMs)) return false;
    delay(1);
  }

  // 2) INTERRUPT lesen
  uint8_t intr = 0;
  if (!readBytes(REG_INTERRUPT, &intr, 1)) return false;
  if (intrStatus) *intrStatus = intr;

  if (pdResp)  for (int i = 0; i < 4; i++) pdResp[i] = 0;
  if (devResp) for (int i = 0; i < 2; i++) devResp[i] = 0;

  bool anyRead = false;

  if ((intr & 0x01) && devResp) {
    if (!readBytes(REG_DEV_RESPONSE, devResp, 2)) return false;
    anyRead = true;
  }

  if ((intr & 0x02) && pdResp) {
    if (!readBytes(REG_PD_RESPONSE, pdResp, 4)) return false;
    anyRead = true;
  }

  // 3) W1C quittieren
  uint8_t clearMask = intr & 0x03;
  if (clearMask) {
    if (!write8(REG_INTERRUPT, clearMask)) return false;
  }

  return anyRead;
}