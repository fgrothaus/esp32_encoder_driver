#include "Arduino.h"
#include "soc/gpio_reg.h" // HAL-Header für direkten Registerzugriff (schneller)

const int pinCLK = 18; // Taktgeber
const int pinDT  = 19; // Links-/Rechtsdrehung (Data)
const int pinSW  = 32; // Button-Push

// Alle vier Werte sind als volatile markiert, damit der Compiler weiß, dass er immer im RAM nachschauen muss, ob sich ein Wert geändert hat.
// Das liegt daran, dass die folgenden Variablen nicht im Hauptprogramm geändert werden, sondern nur über die ISRs, die allerdings außerhalb der Sicht
// des Compilers sind. ISRs sind vollständig entkoppelt vom Programm und werden über das OS(beim Ardunino gibt es kein OS)/Interrupt-Vektortabelle
// ausgeführt. Da sich im Hauptprogramm die Variablen nicht ändern, würde der Compiler die bestehenden Werte im CPU-Register cachen
// und nicht erneut im RAM gucken, ob sich was geändert hat. Mit volatile weiß der Compiler, dass die Werte volatil sind und über extern geändert werden könnten.
volatile int drehZaehler = 0;
volatile uint8_t alterZustand = 0; // Speichert die letzten Pin-Zustände
volatile unsigned long letzteFlankeSW = 0;
volatile bool buttonGedrueckt = false;

// Lookup-Tabelle für die 16 möglichen Zustandsübergänge
// 0 = Ungültig/Prellen, 1 = Rechts, -1 = Links
// Die encoderTabelle stellt den Zustandsautomaten dar.
// Der Encoder kann bei jeder Drehung folgende Zustände durchlaufen (Gray Code):
// Rechts: 11-10-00-01 (jeder Zustandswechsel wird mit +1 gewertet. Also 11 -> 10 =>  Index = (3 << 2)|2 = 14)
// Links: 11-01-00-10
const int8_t encoderTabelle[] = {
   0,  1, -1,  0,
  -1,  0,  0,  1,
   1,  0,  0, -1,
   0, -1,  1,  0
};

// Diese ISR läuft blitzschnell bei JEDER Änderung an CLK oder DT
// Pro Raste werden alle vier Zustände durchlaufen. D.h. pro Raste wird 4x die encoderISR aufgerufen
void IRAM_ATTR encoderISR() {
  // REG_READ liest das gesamte 32-Bit-Register (Adresse 0x3FF4403C) im GPIO-Controller an der Andresse GPIO_IN_REG, in der die Zustände von Pin 0-31 stehen (32 Bit)
  uint32_t gpio_in = REG_READ(GPIO_IN_REG);
  
  // Aus dem Snapshot werden nun die Zustände von CLK und DT extrahiert und nebeneinander in eine Byte Variable gepackt
  // Es kommen dadurch vier elektrische Zustände in Frage:
  // 000000 00 (0)
  // 000000 01 (1)
  // 000000 10 (2)
  // 000000 11 (3)
  // (gpio_in >> pinCLK) & 0x01) << 1 = 00000010; ((gpio_in >> pinDT) & 0x01) = 00000001 => 00000010 | 00000001 = 00000011 z.B.
  uint8_t neuerZustand = ((gpio_in >> pinCLK) & 0x01) << 1 | ((gpio_in >> pinDT) & 0x01);

  // Die Berechnung des Index' stellt den Zustandswechsel dar, anhand dem inerpretiert wird, ob es sich um eine Links- oder Rechtsdrehung handelt.
  // (encoderTabelle hat 16 Werte => 0x0 bis 0xF Indizes)
  uint8_t index = ((alterZustand & 0x03) << 2) | neuerZustand; // z.B. (00000010 & 00000011) << 2 | 00000011 => 00001000 | 00000011 = 00001011 = 11

  // Roh-Steps. Bei Rechtsdrehung Index 1, 7, 8 und 14 => jeweils +1 und bei Linksdrehung 2, 4, 11 ,13
  drehZaehler += encoderTabelle[index];

  // Zustand speichern, um innerhalb der Raste den Zustandswechsel bestimmen zu können.
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

  // Da ein mechanisches Rasten 4 Zustandsschritten entspricht, 
  // muss durch 4 geteilt werden, um pro Raste genau um 1 hochzuzählen.
  int echteRasten = aktuellerStand / 4; 

  if (echteRasten != letzterStand || wasReset) {
    if (wasReset) {
      Serial.println("-> RESET! Zaehler auf 0.");
      letzterStand = 0;
    } else {
      Serial.printf("Raste: %d (Roh-Steps: %d)\n", echteRasten, aktuellerStand);
      letzterStand = echteRasten;
    }
  }

  delay(10);
}