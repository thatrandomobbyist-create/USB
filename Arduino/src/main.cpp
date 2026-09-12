#include <Arduino.h>

// Protocol: A5, version, command, payload length, sequence, payload, CRC16 low, CRC16 high.
const uint8_t START_BYTE = 0xA5;
const uint8_t PROTOCOL_VERSION = 1;
const uint8_t CMD_WRITE_ALL = 0x01;
const uint8_t CMD_READ_ALL = 0x02;
const uint8_t RESP_ACK = 0x80;
const uint8_t RESP_READ_ALL = 0x81;
const uint8_t RESP_ERROR = 0xE0;
const uint8_t ERROR_INVALID_COMMAND = 1;
const uint8_t ERROR_INVALID_PACKET = 2;
const uint8_t MAX_PAYLOAD = 8;
const unsigned long WATCHDOG_TIMEOUT_MS = 500; // Adjust for the application, if needed.

uint8_t frame[5 + MAX_PAYLOAD + 2];
uint8_t frameIndex = 0;
uint8_t expectedLength = 0;
uint16_t outputMask = 0;
uint16_t outputValues = 0;
unsigned long lastValidCommand = 0;

uint16_t crc16(const uint8_t* data, uint8_t length) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < length; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

void sendFrame(uint8_t command, uint8_t sequence, const uint8_t* payload, uint8_t length) {
  uint8_t packet[5 + MAX_PAYLOAD + 2];
  packet[0] = START_BYTE;
  packet[1] = PROTOCOL_VERSION;
  packet[2] = command;
  packet[3] = length;
  packet[4] = sequence;
  for (uint8_t i = 0; i < length; ++i) packet[5 + i] = payload[i];
  uint16_t checksum = crc16(&packet[1], 4 + length);
  packet[5 + length] = (uint8_t)(checksum & 0xFF);
  packet[6 + length] = (uint8_t)(checksum >> 8);
  Serial.write(packet, 7 + length);
}

void sendError(uint8_t sequence, uint8_t error) {
  sendFrame(RESP_ERROR, sequence, &error, 1);
}

void applyOutputs(uint16_t newMask, uint16_t newValues) {
  // Configure removed outputs as inputs first; preload values before enabling outputs.
  for (uint8_t pin = 2; pin <= 13; ++pin) {
    uint16_t bit = (uint16_t)1 << (pin - 2);
    if ((outputMask & bit) && !(newMask & bit)) pinMode(pin, INPUT);
  }
  for (uint8_t pin = 2; pin <= 13; ++pin) {
    uint16_t bit = (uint16_t)1 << (pin - 2);
    if (newMask & bit) digitalWrite(pin, (newValues & bit) ? HIGH : LOW);
  }
  for (uint8_t pin = 2; pin <= 13; ++pin) {
    uint16_t bit = (uint16_t)1 << (pin - 2);
    if (newMask & bit) pinMode(pin, OUTPUT);
  }
  outputMask = newMask;
  outputValues = newValues & newMask;
}

void processFrame() {
  uint8_t length = frame[3];
  uint16_t received = (uint16_t)frame[5 + length] | ((uint16_t)frame[6 + length] << 8);
  if (frame[1] != PROTOCOL_VERSION || length > MAX_PAYLOAD ||
      crc16(&frame[1], 4 + length) != received) {
    frameIndex = 0;
    return;
  }

  uint8_t command = frame[2];
  uint8_t sequence = frame[4];
  if (command == CMD_WRITE_ALL && length == 4) {
    uint16_t newMask = (uint16_t)frame[5] | ((uint16_t)frame[6] << 8);
    uint16_t newValues = (uint16_t)frame[7] | ((uint16_t)frame[8] << 8);
    if ((newMask | newValues) & 0xF000) {
      sendError(sequence, ERROR_INVALID_PACKET);
    } else {
      applyOutputs(newMask, newValues);
      lastValidCommand = millis();
      sendFrame(RESP_ACK, sequence, 0, 0);
    }
  } else if (command == CMD_READ_ALL && length == 0) {
    uint16_t value = 0;
    for (uint8_t pin = 2; pin <= 13; ++pin) {
      if (digitalRead(pin) == HIGH) value |= (uint16_t)1 << (pin - 2);
    }
    uint8_t payload[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    lastValidCommand = millis();
    sendFrame(RESP_READ_ALL, sequence, payload, 2);
  } else {
    sendError(sequence, command == CMD_WRITE_ALL || command == CMD_READ_ALL
                         ? ERROR_INVALID_PACKET : ERROR_INVALID_COMMAND);
  }
  frameIndex = 0;
}

void receiveSerial() {
  while (Serial.available() > 0) {
    uint8_t byte = (uint8_t)Serial.read();
    if (frameIndex == 0) {
      if (byte == START_BYTE) frame[frameIndex++] = byte;
      continue;
    }
    frame[frameIndex++] = byte;
    if (frameIndex == 4) {
      expectedLength = frame[3];
      if (expectedLength > MAX_PAYLOAD) frameIndex = 0;
    }
    if (frameIndex >= 5 && frameIndex == (uint8_t)(7 + expectedLength)) processFrame();
    if (frameIndex >= sizeof(frame)) frameIndex = 0;
  }
}

void setup() {
  for (uint8_t pin = 2; pin <= 13; ++pin) pinMode(pin, INPUT);
  Serial.begin(115200);
  lastValidCommand = millis();
}

void loop() {
  receiveSerial();
  if (outputMask != 0 && millis() - lastValidCommand >= WATCHDOG_TIMEOUT_MS) {
    for (uint8_t pin = 2; pin <= 13; ++pin) {
      uint16_t bit = (uint16_t)1 << (pin - 2);
      if (outputMask & bit) {
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
      }
    }
    outputValues = 0;
    lastValidCommand = millis();
    frameIndex = 0;
  }
}
