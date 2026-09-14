#ifndef IS25LP128_H
#define IS25LP128_H

#include <SPI.h>

#define WRITE_ENABLE 0x06
#define WRITE_DISABLE 0x04
#define READ_STATUS_REGISTER 0x05
#define WRITE_STATUS_REGISTER 0x01
#define READ_DATA 0x03
#define FAST_READ 0x0B
#define PAGE_PROGRAM 0x02
#define SECTOR_ERASE 0x20
#define BLOCK_ERASE_32K 0x52
#define BLOCK_ERASE_64K 0xD8
#define CHIP_ERASE 0xC7
#define POWER_DOWN 0xB9
#define RELEASE_POWER_DOWN 0xAB
#define DEVICE_ID 0xAB
#define MANUFACTURER_ID 0x90
#define JEDEC_ID 0x9F
#define ENABLE_RESET 0x66
#define RESET_DEVICE 0x99

class IS25LP128 {
  public:
    IS25LP128(int csPin);
    void begin();
    void enterQPI();
    void exitQPI();
    void writeEnable();
    void writeDisable();
    uint8_t readStatusRegister();
    void writeStatusRegister(uint8_t status);
    void writeByte(uint32_t addr, uint8_t data);
    uint8_t readByte(uint32_t addr);
    void eraseSector(uint32_t addr);
    void eraseBlock32K(uint32_t addr);
    void eraseBlock64K(uint32_t addr);
    void eraseChip();
    uint8_t readDeviceID();
  private:
    int csPin;
    void waitUntilNotBusy();
    bool checkError();
    void resetDevice();
};

#endif