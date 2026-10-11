#pragma once

#include <stdint.h>

struct MeasurementRecord {
  uint16_t bpm;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

template <typename Eeprom>
class MeasurementStore {
 public:
  explicit MeasurementStore(Eeprom &eeprom) : eeprom_(eeprom) {}

  void initialize() {
    if (eeprom_.read(kClearStateAddress) == kClearPending ||
        eeprom_.read(kClearStateCheckAddress) == kClearStateCheck) {
      eraseRecordMarkers();
      eeprom_.update(kClearStateAddress, kClearComplete);
      eeprom_.update(kClearStateCheckAddress, kClearComplete);
    }

    const uint8_t language = eeprom_.read(kLanguageAddress);
    const uint8_t expectedCheck =
        metadataCheck(eeprom_.read(kMagicAddress), eeprom_.read(kVersionAddress),
                      language);
    if (eeprom_.read(kMagicAddress) == kMagic &&
        eeprom_.read(kVersionAddress) == kVersion &&
        eeprom_.read(kMetadataCheckAddress) == expectedCheck &&
        language <= kEnglish) {
      return;
    }

    eeprom_.update(kMagicAddress, kMagic);
    eeprom_.update(kVersionAddress, kVersion);
    eeprom_.update(kLanguageAddress, kFrench);
    eeprom_.update(kMetadataCheckAddress,
                   metadataCheck(kMagic, kVersion, kFrench));
  }

  uint8_t language() const {
    const uint8_t language = eeprom_.read(kLanguageAddress);
    return language <= kEnglish ? language : (uint8_t)kFrench;
  }

  void setLanguage(uint8_t language) {
    if (language > kEnglish) return;
    eeprom_.update(kLanguageAddress, language);
    eeprom_.update(kMetadataCheckAddress,
                   metadataCheck(kMagic, kVersion, language));
  }

  uint16_t capacity() const {
    const uint16_t length = eeprom_.length();
    return length > kDataAddress ? (length - kDataAddress) / kRecordSize : 0;
  }

  uint16_t count() const {
    uint16_t validCount = 0;
    for (uint16_t slot = 0; slot < capacity(); ++slot) {
      MeasurementRecord ignored;
      if (readSlot(slot, ignored)) ++validCount;
    }
    return validCount;
  }

  bool save(const MeasurementRecord &record) {
    if (!isValidRecord(record)) return false;

    const uint16_t slots = capacity();
    for (uint16_t slot = 0; slot < slots; ++slot) {
      MeasurementRecord ignored;
      if (!readSlot(slot, ignored)) {
        writeSlot(slot, record);
        return readSlot(slot, ignored);
      }
    }
    return false;
  }

  bool read(uint16_t index, MeasurementRecord &record) const {
    uint16_t validIndex = 0;
    for (uint16_t slot = 0; slot < capacity(); ++slot) {
      MeasurementRecord current;
      if (!readSlot(slot, current)) continue;
      if (validIndex == index) {
        record = current;
        return true;
      }
      ++validIndex;
    }
    return false;
  }

  void clear() {
    eeprom_.update(kClearStateAddress, kClearPending);
    eeprom_.update(kClearStateCheckAddress, kClearStateCheck);
    eraseRecordMarkers();
    eeprom_.update(kClearStateAddress, kClearComplete);
    eeprom_.update(kClearStateCheckAddress, kClearComplete);
  }

 private:
  enum : uint8_t {
    kMagicAddress = 0,
    kVersionAddress = 1,
    kLanguageAddress = 2,
    kMetadataCheckAddress = 3,
    kDataAddress = 6,
    kRecordSize = 10,
    kMagic = 0xC6,
    kVersion = 1,
    kFrench = 0,
    kEnglish = 1,
    kValidMarker = 0xA5,
    kInvalidMarker = 0,
    kClearStateAddress = 4,
    kClearStateCheckAddress = 5,
    kClearComplete = 0,
    kClearPending = 0xC3,
    kClearStateCheck = 0x3C
  };

  static uint8_t metadataCheck(uint8_t magic, uint8_t version,
                               uint8_t language) {
    return magic ^ version ^ language ^ 0x5A;
  }

  static uint8_t recordCheck(const uint8_t *bytes) {
    uint8_t crc = 0;
    for (uint8_t i = 1; i <= 8; ++i) {
      crc ^= bytes[i];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07)
                           : (uint8_t)(crc << 1);
      }
    }
    return crc;
  }

  static bool isValidRecord(const MeasurementRecord &record) {
    return record.bpm > 0 && record.bpm <= 255 && record.year >= 2000 &&
           record.year <= 9999 &&
           record.month >= 1 && record.month <= 12 && record.day >= 1 &&
           record.day <= 31 && record.hour <= 23 && record.minute <= 59 &&
           record.second <= 59;
  }

  uint16_t slotAddress(uint16_t slot) const {
    return kDataAddress + slot * kRecordSize;
  }

  void eraseRecordMarkers() {
    for (uint16_t slot = 0; slot < capacity(); ++slot) {
      eeprom_.update(slotAddress(slot), kInvalidMarker);
    }
  }

  bool readSlot(uint16_t slot, MeasurementRecord &record) const {
    if (slot >= capacity()) return false;
    const uint16_t address = slotAddress(slot);
    uint8_t bytes[kRecordSize];
    for (uint8_t i = 0; i < kRecordSize; ++i) {
      bytes[i] = eeprom_.read(address + i);
    }
    if (bytes[0] != kValidMarker || recordCheck(bytes) != bytes[9]) {
      return false;
    }

    record.bpm = bytes[1];
    record.year = (uint16_t)bytes[2] | ((uint16_t)bytes[3] << 8);
    record.month = bytes[4];
    record.day = bytes[5];
    record.hour = bytes[6];
    record.minute = bytes[7];
    record.second = bytes[8];
    return isValidRecord(record);
  }

  void writeSlot(uint16_t slot, const MeasurementRecord &record) {
    const uint16_t address = slotAddress(slot);
    uint8_t bytes[kRecordSize] = {};
    bytes[1] = (uint8_t)record.bpm;
    bytes[2] = (uint8_t)record.year;
    bytes[3] = (uint8_t)(record.year >> 8);
    bytes[4] = record.month;
    bytes[5] = record.day;
    bytes[6] = record.hour;
    bytes[7] = record.minute;
    bytes[8] = record.second;
    bytes[9] = recordCheck(bytes);

    eeprom_.update(address, kInvalidMarker);
    for (uint8_t i = 1; i < kRecordSize; ++i) {
      eeprom_.update(address + i, bytes[i]);
    }
    eeprom_.update(address, kValidMarker);
  }

  Eeprom &eeprom_;
};
