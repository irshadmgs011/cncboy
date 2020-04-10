#include <SD.h>
#include <U8g2lib.h>
#include "ui.h"
#include "state.h"
#include "keypad.h"
#include "millingCtrl.h"
#include "filesCtrl.h"

#define SD_CS 21
#define DSPY_CS 5
#define DSPY_BCKLIGHT 2 // TODO
#define RXD2 16
#define TXD2 17

#define KEY_FUNCTION 36 // VP
#define KEY_X 39 // VN
#define KEY_Y 34
#define KEY_Z 35

#define HSPI_MISO 15
#define HSPI_MOSI 13
#define HSPI_CLK 14

U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, /* CS=*/ DSPY_CS); // VSPI for display
SPIClass SDSPI(HSPI); // HSPI for SD card

STATE state;
UI ui(&u8g2, &state, DSPY_BCKLIGHT);
KeyPad keypad(KEY_FUNCTION, KEY_X, KEY_Y, KEY_Z);

MillingCtrl millingCtrl(&ui, &keypad);
FilesCtrl filesCtrl(&ui, &keypad, &SDSPI, SD_CS);

File file;

int everySecondTimer = 0;
unsigned long deltaMsRef = 0;

// mode
#define MODE_INIT 0
#define MODE_IDLE 1
#define MODE_SELECT_FILE 2
#define MODE_LOAD_FILE 3
#define MODE_SEND_COMMAND 4
#define MODE_AWAIT_ACK 5
#define MODE_DONE 6
#define MODE_ERROR 99

byte mode = MODE_INIT;


// mode
#define SCREEN_HOME 0
#define SCREEN_FILES 1
#define SCREEN_MILLING 2

byte screen = SCREEN_FILES;

void setup() {
  Serial.begin(115200); // USB
  Serial.setDebugOutput(0);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); // CNC

  // HSPI for SD card
  SDSPI.begin(HSPI_CLK, HSPI_MISO, HSPI_MOSI, -1);

  // modules
  keypad.setup();
  ui.setup();
  filesCtrl.setup();

  mode = MODE_INIT;
  state.milling.elapsedSeconds = 0;

  // initial delay to warm everything up
  delay(1000);
  deltaMsRef = millis();
  everySecondTimer = 1000;

  // init screen
  screen = SCREEN_FILES;
  filesCtrl.start();
}

void loop() {
  int deltaMs = getDeltaMs();
  byte result;

  keypad.update(deltaMs);

  switch (screen) {
    case SCREEN_HOME:
      break;

    case SCREEN_FILES:
      // screen where you can selected files to start milling
      result = filesCtrl.update(deltaMs);
      switch (result) {
        case FILES_RESULT_SELECTED:
          millingCtrl.start(filesCtrl.getSelectedFile());
          screen = SCREEN_MILLING;
          break;
        case FILES_RESULT_BACK:
          //screen = SCREEN_HOME;
          break;
      }
      break;

    case SCREEN_MILLING:
      // running milling program
      result = millingCtrl.update(deltaMs);
      switch (result) {
        case MILLING_RESULT_BACK:
          millingCtrl.stop();
          filesCtrl.start();
          screen = SCREEN_FILES;
          break;
      }
      break;
  }

  delay(1);
}

int getDeltaMs() {
  unsigned long time = millis();
  if (time < deltaMsRef) {
    deltaMsRef = time; // overflow of millis happen, we lost some millis but that does not matter for us, keep moving on
    return time;
  }
  int deltaMs = time - deltaMsRef;
  deltaMsRef += deltaMs;
  return deltaMs;
}
