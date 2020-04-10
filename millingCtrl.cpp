#include "millingCtrl.h"

#define STATE_READY 0
#define STATE_RUNNING 1
#define STATE_PAUSED 2

MillingCtrl::MillingCtrl(UI *ui, KeyPad *keyPad) 
    : ui(*ui), keyPad(*keyPad) {
}

void MillingCtrl::start(File _file) {
  file = _file;
  totalLines = 0;

  String line;
  long loadedBytes = 0;
  float filesize = file.size();
  float progress = 0;
  float newProgress = 0;

  Serial.print("Reading milling ");
  Serial.println(file.name());

  showLoading(progress);

  while (file.available()) {
    line = file.readStringUntil('\n');
    if (isValidCommand(line)) {
      totalLines++;
      loadedBytes += line.length();
      newProgress = loadedBytes / filesize;
      if (newProgress - 0.02 > progress) {
        progress = newProgress;
        showLoading(progress);
      }
    }
  }

  Serial.print(totalLines);
  Serial.println(" total commands to execute");

  // done loading
  sendCommand(String(char(18))); // soft-reset
  reset();
}

void MillingCtrl::stop() {
  if (file != NULL) {
    file.close();
  }
}

byte MillingCtrl::update(int deltaMs) {
  everySecondTimer -= deltaMs;
  if (everySecondTimer <= 0) {
    everySecondTimer += 1000;
    queryStatus = true;

    if (state == STATE_RUNNING) {
      elapsedSeconds++;
    }
  }

  if (keyPad.isKeyPressed(KEYCODE_A)) { // back
    if (state == STATE_READY) {
      return MILLING_RESULT_BACK;
    }
  }
  else if (keyPad.isKeyPressed(KEYCODE_B)) { // stop
    Serial.println("Stopped");
    sendCommand(String(char(18))); // soft-reset
    reset(); // TODO: confirm?
  }
  else if (keyPad.isKeyPressed(KEYCODE_C)) { // pause
    Serial.println("Paused");
    if (state == STATE_RUNNING) {
      state = STATE_PAUSED;
      sendCommand("!"); // hold
      commandAwaitReply = false; // not going to receive resposne for hold command
    }
  }
  else if (keyPad.isKeyPressed(KEYCODE_D)) { // play
    Serial.println("Running milling " + String(file.name()));
    if (state == STATE_READY) {
      reset();
      sendCommand("~"); // resume
      state = STATE_RUNNING;
    }
    else if (state == STATE_PAUSED) {
      error = false; // paused after error condition
      sendCommand("~"); // resume
      state = STATE_RUNNING;
    }
  }

  receiveResponse();

  // TODO: zero out the machine function

  if (!commandAwaitReply && !error) { // can send another command, previous one was ACK
    if (queryStatus) {
      sendCommand("?");
      queryStatus = false;
    }
    else if (state == STATE_RUNNING) {
      sendNextCommand();
    }
  }
  showStatus();

  return MILLING_RESULT_NONE;
}

void MillingCtrl::reset() {
  file.seek(0); // reset file
  everySecondTimer = 0;
  elapsedSeconds = 0;
  currentLine = 0;
  currentCommand = "";
  commandAwaitReply = false;
  error = false;
  state = STATE_READY;
}

void MillingCtrl::sendNextCommand() {
  String line;
  
  if (file.available()) {
    line = file.readStringUntil('\n');
    if (isValidCommand(line)) {
      line.trim();
      sendCommand(line);

      currentCommand = line;
      currentLine++;
    }
  }
  else {
    Serial.println("Done " + String(file.name()));
    currentCommand = "Finished";
    state = STATE_READY;
  }
}

void MillingCtrl::sendCommand(String command) {
  Serial2.print(command);
  Serial2.print('\n');
  Serial2.flush();

  Serial.println(command);
  commandAwaitReply = true;

  delay(10);
}

void MillingCtrl::receiveResponse() {
  String response;

  while (Serial2.available()) {
    response += char(Serial2.read());
    delay(1);
  }

  if (response.length() > 0) {
    response.trim();

    if (response.startsWith("<")) {
      // machine status report
      Serial.println("Report: " + response);
      parseStatusReport(response);
      commandAwaitReply = false;
    }
    else if (response.startsWith("ok")) {
      // ok response
      Serial.println("Ack: " + response);
      commandAwaitReply = false;
    }
    else if (response.startsWith("error")) {
      // error reponse
      Serial.println("Error: " + response);
      currentCommand = response;
      commandAwaitReply = false;
      state = STATE_PAUSED;
      error = true;
    }
    else {
      Serial.println("Unsupported: ->" + response + "<-");
    }
  }
}

void MillingCtrl::parseStatusReport(String report) {
  char token;
  String value;

  byte valueIndex = 0;
  byte coordinateIndex = 0;

  // format: <Run|MPos:-0.950,-4.887,-2.500|FS:1010,1000>

  for (int i = 0; i < report.length(); i++) {
    token = report.charAt(i);
    if (token == '<') {
      continue; // start
      value = "";
    }
    else if (token == '|' || token == ':') {
      if (valueIndex == 0) { // machine status
        machineStatus.state = value;
        value = "";
        valueIndex++;
      }
      else if (valueIndex == 3) { // z coordinate
        machineStatus.z = value.toFloat();
        value = "";
        valueIndex++;
      }
      else {
        value = "";
      }
    }
    else if (token == ',') {
      if (valueIndex == 1) { // x coordinate
        machineStatus.x = value.toFloat();
        value = "";
        valueIndex++;
      }
      else if (valueIndex == 2) { // y coordinate
        machineStatus.y = value.toFloat();
        value = "";
        valueIndex++;
      }
      else {
        value = "";
      }
    }
    else if (token == '>') {
      break; // stop
    }
    else {
      value += token;
    }
  }
}

void MillingCtrl::showStatus() {
  float progress = (float) currentLine / totalLines;

  ui.firstPage();
  do {
    ui.setFont(u8g2_font_5x8_mr);
    ui.drawStr(0, 7, file.name());
    ui.drawProgressBar(9, progress);

    ui.drawAxisIcon(0, 20);
    ui.drawStr(9, 26, "X" + String(machineStatus.x));
    ui.drawStr(49, 26, "Y" + String(machineStatus.y));
    ui.drawStr(90, 26, "Z" + String(machineStatus.z));

    ui.drawClockIcon(0, 28);
    ui.drawStr(9, 35, ui.formatTime(elapsedSeconds));
    byte len = machineStatus.state.length();
    if (len > 0) {
      ui.drawStr(128 - len * 5, 35, machineStatus.state);
    }

    ui.drawStr(0, 46, currentCommand);

    if (state == STATE_READY) {
      ui.drawTextButton(0, "Back");
    }
    else {
      ui.drawStopButton(1);
    }
    if (state == STATE_RUNNING) {
      ui.drawPauseButton(2);
    }
    else {
      ui.drawPlayButton(3);
    }
  } while (ui.nextPage());
}

void MillingCtrl::showLoading(float progress) {
  ui.firstPage();
  do {
    ui.setFont(u8g2_font_5x8_mr);
    ui.drawStr(0, 7, file.name());
    ui.drawStr(0, 20, "Reading file");
    ui.drawProgressBar(22, progress);
  } while (ui.nextPage());
}

boolean MillingCtrl::isValidCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return false;
  }
  if (line.charAt(0) == ';' || line.charAt(0) == '%' || line.charAt(0) == '(') { // skip comments
    return false;
  }
  return true;
}
