#include "millingCtrl.h"

byte Grbl::update(unsigned long nowMillis) {
  if (nowMillis >= millisRef + 1000) { // everysecond timer
    millisRef = nowMillis;
    queryStatus = true;
  }

  receiveResponse();

  if (queryStatus && !awaitingReply && !error) {
	  sendCommand("?");
	  queryStatus = false;
  }
}

void Grbl::restart() {
  awaitingReply = false;
  error = false;
}

bool Grbl::canSendCommand() {
  return !awaitingReply && !error;
}

bool Grbl::isError() {
  return error;
}

String Grbl::getLastCommandError() {
  return errorMessage;
}

MachineStatus Grbl::getMachineStatus() {
  return machineStatus;
}

void Grbl::sendHold() {
  sendCommand("!");
  awaitingReply = false; // no reply on hold
}

void Grbl::sendResume() {
  sendCommand("~");
}

void Grbl::sendReset() {
  sendCommand(String(char(18))); // soft-reset
  awaitingReply = false; // no reply on hold
}

void Grbl::sendCommand(String command) {
  Serial2.print(command);
  Serial2.print('\n');
  Serial2.flush();

  Serial.println(command);
  awaitingReply = true;
  error = false;
  errorMessage = "";
  partialResponse = "";

  //delay(10);
}

void Grbl::receiveResponse() {
  String data;

  while (Serial2.available()) {
    data += char(Serial2.read());
    //delay(1);
  }

  if (data.length() > 0) {
    String response = String(data);
    response.trim();

    if (partialResponse.length() > 0) {
      response = partialResponse + response;
    }

    if (response.endsWith("ok")) {
      if (response.startsWith("<")) {
        // machine status report
        Serial.println("Report: " + response);
        parseStatusReport(response);
      }
      else {
        Serial.println("Ack: " + response);
      }
      partialResponse = "";
      awaitingReply = false;
    }
    else if (response.startsWith("error")) {
      // error reponse
      Serial.println("Error: " + response);
      errorMessage = response;
      partialResponse = "";
      awaitingReply = false;
      error = true;
    }
    else {
      // partial response, wait for another data
      partialResponse += data;
      Serial.println("Partial response: ->" + response + "<-");
    }
  }
}

void Grbl::parseStatusReport(String report) {
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
