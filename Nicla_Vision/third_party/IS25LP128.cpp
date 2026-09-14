#include "IS25LP128.h"

IS25LP128::IS25LP128(int csPin) : csPin(csPin) {}

void IS25LP128::begin() {
  SPI.begin();
  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);
}

void IS25LP128::enterQPI() {
  digitalWrite(csPin, LOW);
  SPI.transfer(0x35);
  digitalWrite(csPin, HIGH);
  checkError();
}

void IS25LP128::exitQPI() {
  digitalWrite(csPin, LOW);
  SPI.transfer(0xF5);
  digitalWrite(csPin, HIGH);
  checkError();
}

void IS25LP128::writeEnable() {
  digitalWrite(csPin, LOW);
  SPI.transfer(WRITE_ENABLE);
  digitalWrite(csPin, HIGH);
}

void IS25LP128::writeDisable() {
  digitalWrite(csPin, LOW);
  SPI.transfer(WRITE_DISABLE);
  digitalWrite(csPin, HIGH);
}

uint8_t IS25LP128::readStatusRegister() {
  digitalWrite(csPin, LOW);
  SPI.transfer(READ_STATUS_REGISTER);
  uint8_t status = SPI.transfer(0);
  digitalWrite(csPin, HIGH);
  return status;
}

void IS25LP128::writeStatusRegister(uint8_t status) {
  digitalWrite(csPin, LOW);
  SPI.transfer(WRITE_STATUS_REGISTER);
  SPI.transfer(status);
  digitalWrite(csPin, HIGH);
  checkError();
}

void IS25LP128::writeByte(uint32_t addr, uint8_t data) {
  for (int i = 0; i < 3; i++) { // Retry up to 3 times
    writeEnable();
    digitalWrite(csPin, LOW);
    SPI.transfer(PAGE_PROGRAM);
    SPI.transfer(addr >> 16);
    SPI.transfer(addr >> 8);
    SPI.transfer(addr);
    SPI.transfer(data);
    digitalWrite(csPin, HIGH);
    waitUntilNotBusy();
    if (checkError()) {
      continue; // If there was an error, retry the operation
    }
    uint8_t readBack = readByte(addr);
    if (readBack == data) {
      writeDisable();
      return; // If the data matches, the operation was successful
    }
  }
  // If we reach this point, the operation failed after 3 attempts
  // Handle the error according to your application's requirements
}

uint8_t IS25LP128::readByte(uint32_t addr) {
  digitalWrite(csPin, LOW);
  SPI.transfer(READ_DATA);
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  uint8_t data = SPI.transfer(0);
  digitalWrite(csPin, HIGH);
  checkError();
  return data;
}

void IS25LP128::eraseSector(uint32_t addr) {
  writeEnable();
  digitalWrite(csPin, LOW);
  SPI.transfer(SECTOR_ERASE);
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  digitalWrite(csPin, HIGH);
  waitUntilNotBusy();
  checkError();
  writeDisable();
}

void IS25LP128::eraseBlock32K(uint32_t addr) {
  writeEnable();
  digitalWrite(csPin, LOW);
  SPI.transfer(BLOCK_ERASE_32K);
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  digitalWrite(csPin, HIGH);
  waitUntilNotBusy();
  checkError();
  writeDisable();
}

void IS25LP128::eraseBlock64K(uint32_t addr) {
  writeEnable();
  digitalWrite(csPin, LOW);
  SPI.transfer(BLOCK_ERASE_64K);
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  digitalWrite(csPin, HIGH);
  waitUntilNotBusy();
  checkError();
  writeDisable();
}

void IS25LP128::eraseChip() {
  writeEnable();
  digitalWrite(csPin, LOW);
  SPI.transfer(CHIP_ERASE);
  digitalWrite(csPin, HIGH);
  waitUntilNotBusy();
  checkError();
  writeDisable();
}

uint8_t IS25LP128::readDeviceID() {
  digitalWrite(csPin, LOW);
  SPI.transfer(DEVICE_ID);
  uint8_t id = SPI.transfer(0);
  digitalWrite(csPin, HIGH);
  checkError();
  return id;
}

void IS25LP128::waitUntilNotBusy() {
  while (readStatusRegister() & 0x01); // Wait for the operation to finish
}

bool IS25LP128::checkError() {
  uint8_t status = readStatusRegister();
  if (status & 0x20) {
    resetDevice();
    return true; // An error occurred
  }
  return false; // No error
}

void IS25LP128::resetDevice() {
  digitalWrite(csPin, LOW);
  SPI.transfer(ENABLE_RESET);
  digitalWrite(csPin, HIGH);
  digitalWrite(csPin, LOW);
  SPI.transfer(RESET_DEVICE);
  digitalWrite(csPin, HIGH);
}
