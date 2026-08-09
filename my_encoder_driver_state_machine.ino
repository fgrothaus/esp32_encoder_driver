#include "Arduino.h"
#include "soc/gpio_reg.h" // HAL-Header für direkten Registerzugriff

const int pinCLK = 18;
const int pinDT  = 19;
const int pinSW  = 32;

volatile int drehZaehler = 0;
volatile uint8_t alterZustand = 0; // Speichert die letzten Pin-Zustände
volatile unsigned long letzteFlankeSW = 0;
volatile bool buttonGedrueckt = false;

// Lookup-Tabelle für die 16 möglichen Zustandsübergänge (Gray Code)
// 0 = Ungültig/Prellen, 1 = Rechts, -1 = Links
const int8_t encoderTabelle[] = {
   0,  1, -1,  0,
  -1,  0,  0,  1,
   1,  0,  0, -1,
   0, -1,  1,  0
};

// Diese ISR läuft blitzschnell bei JEDER Änderung an CLK oder DT
void IRAM_ATTR encoderISR() {
  // Liest den gesamten GPIO-Eingangs-Bus (Pins 0-31) in einem einzigen Maschinencode-Befehl aus:
  uint32_t gpio_in = REG_READ(GPIO_IN_REG);
  
  // Wir holen uns die Bits für Pin 18 (CLK) und Pin 19 (DT)
  uint8_t neuerZustand = ((gpio_in >> pinCLK) & 0x01) << 1 | ((gpio_in >> pinDT) & 0x01);

  // 2. Alten und neuen Zustand verknüpfen (4-Bit-Index für die Tabelle)
  uint8_t index = ((alterZustand & 0x03) << 2) | neuerZustand;

  // 3. Tabelle abfragen & addieren
  drehZaehler += encoderTabelle[index];

  // 4. Zustand speichern
  alterZustand = neuerZustand;
}

void IRAM_ATTR buttonISR() {
  unsigned long jetzt = millis();
  if (jetzt - letzteFlankeSW > 200) { // 200ms Entprellen für den Taster
    drehZaehler = 0;
    buttonGedrueckt = true;
    letzteFlankeSW = jetzt;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(pinCLK, INPUT_PULLUP);
  pinMode(pinDT, INPUT_PULLUP);
  pinMode(pinSW, INPUT_PULLUP);

  // Initialen Zustand einlesen
  alterZustand = (digitalRead(pinCLK) << 1) | digitalRead(pinDT);

  // Interrupts auf BEIDEN Pins aktivieren – reagiert auf JEDEN Wechsel (CHANGE)
  attachInterrupt(digitalPinToInterrupt(pinCLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinDT),  encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinSW),  buttonISR,  FALLING);
}

void loop() {
  static int letzterStand = 0;

  noInterrupts();
  int aktuellerStand = drehZaehler;
  bool wasReset = buttonGedrueckt;
  buttonGedrueckt = false;
  interrupts();

  // Da ein mechanisches Rasten meist 4 Zustandsschritte entspricht, 
  // teilen wir durch 4, um exakt 1 Zähler pro Raste zu bekommen!
  int echeRasten = aktuellerStand / 4; 

  if (echeRasten != letzterStand || wasReset) {
    if (wasReset) {
      Serial.println("-> RESET! Zaehler auf 0.");
      letzterStand = 0;
    } else {
      Serial.printf("Raste: %d (Roh-Steps: %d)\n", echeRasten, aktuellerStand);
      letzterStand = echeRasten;
    }
  }

  delay(10);
}