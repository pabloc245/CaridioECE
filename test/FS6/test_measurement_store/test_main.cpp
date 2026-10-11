#include <BpmAverager.h>
#include <MeasurementStore.h>
#include <unity.h>

class FakeEeprom {
 public:
  enum { kSize = 64 };

  FakeEeprom() {
    for (uint16_t i = 0; i < kSize; ++i) bytes_[i] = 0xFF;
  }

  uint8_t read(uint16_t address) const {
    return address < kSize ? bytes_[address] : 0xFF;
  }

  void update(uint16_t address, uint8_t value) {
    if (address < kSize) bytes_[address] = value;
  }

  uint16_t length() const { return kSize; }

  void corrupt(uint16_t address, uint8_t value) {
    update(address, value);
  }

 private:
  uint8_t bytes_[kSize];
};

void test_records_and_language_survive_store_reinitialization() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  store.setLanguage(1);

  const MeasurementRecord expected = {72, 2026, 10, 11, 18, 25, 9};
  TEST_ASSERT_TRUE(store.save(expected));

  MeasurementStore<FakeEeprom> restartedStore(eeprom);
  restartedStore.initialize();
  TEST_ASSERT_EQUAL_UINT8(1, restartedStore.language());
  TEST_ASSERT_EQUAL_UINT16(1, restartedStore.count());

  MeasurementRecord actual = {};
  TEST_ASSERT_TRUE(restartedStore.read(0, actual));
  TEST_ASSERT_EQUAL_UINT16(expected.bpm, actual.bpm);
  TEST_ASSERT_EQUAL_UINT16(expected.year, actual.year);
  TEST_ASSERT_EQUAL_UINT8(expected.month, actual.month);
  TEST_ASSERT_EQUAL_UINT8(expected.day, actual.day);
  TEST_ASSERT_EQUAL_UINT8(expected.hour, actual.hour);
  TEST_ASSERT_EQUAL_UINT8(expected.minute, actual.minute);
  TEST_ASSERT_EQUAL_UINT8(expected.second, actual.second);
}

void test_full_eeprom_rejects_new_records_without_overwriting_old_data() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  const MeasurementRecord record = {60, 2026, 1, 1, 0, 0, 0};

  for (uint16_t i = 0; i < store.capacity(); ++i) {
    TEST_ASSERT_TRUE(store.save(record));
  }
  TEST_ASSERT_FALSE(store.save(record));
  TEST_ASSERT_EQUAL_UINT16(store.capacity(), store.count());

  MeasurementRecord oldest = {};
  TEST_ASSERT_TRUE(store.read(0, oldest));
  TEST_ASSERT_EQUAL_UINT16(record.bpm, oldest.bpm);
}

void test_clear_removes_records_but_preserves_language() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  store.setLanguage(1);
  const MeasurementRecord record = {88, 2026, 12, 31, 23, 59, 59};
  TEST_ASSERT_TRUE(store.save(record));

  store.clear();
  TEST_ASSERT_EQUAL_UINT16(0, store.count());
  TEST_ASSERT_EQUAL_UINT8(1, store.language());
}

void test_startup_finishes_an_interrupted_clear() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  const MeasurementRecord record = {72, 2026, 10, 11, 18, 25, 9};
  TEST_ASSERT_TRUE(store.save(record));

  eeprom.corrupt(4, 0xC3);
  eeprom.corrupt(5, 0x3C);
  MeasurementStore<FakeEeprom> restartedStore(eeprom);
  restartedStore.initialize();
  TEST_ASSERT_EQUAL_UINT16(0, restartedStore.count());
  TEST_ASSERT_EQUAL_UINT8(0, eeprom.read(4));
  TEST_ASSERT_EQUAL_UINT8(0, eeprom.read(5));
}

void test_startup_finishes_clear_if_power_fails_after_first_marker() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  const MeasurementRecord record = {72, 2026, 10, 11, 18, 25, 9};
  TEST_ASSERT_TRUE(store.save(record));

  eeprom.corrupt(4, 0xC3);
  MeasurementStore<FakeEeprom> restartedStore(eeprom);
  restartedStore.initialize();
  TEST_ASSERT_EQUAL_UINT16(0, restartedStore.count());
  TEST_ASSERT_EQUAL_UINT8(0, eeprom.read(4));
  TEST_ASSERT_EQUAL_UINT8(0, eeprom.read(5));
}

void test_corrupted_checksum_is_not_returned_as_a_record() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  const MeasurementRecord record = {90, 2026, 2, 28, 12, 30, 45};
  TEST_ASSERT_TRUE(store.save(record));

  eeprom.corrupt(4 + 5, 1);
  TEST_ASSERT_EQUAL_UINT16(0, store.count());
  MeasurementRecord actual = {};
  TEST_ASSERT_FALSE(store.read(0, actual));
}

void test_invalid_date_or_bpm_is_rejected() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();

  const MeasurementRecord invalidBpm = {0, 2026, 1, 1, 0, 0, 0};
  const MeasurementRecord invalidDate = {75, 2026, 13, 1, 0, 0, 0};
  const MeasurementRecord invalidYear = {75, 1999, 1, 1, 0, 0, 0};
  TEST_ASSERT_FALSE(store.save(invalidBpm));
  TEST_ASSERT_FALSE(store.save(invalidDate));
  TEST_ASSERT_FALSE(store.save(invalidYear));
  TEST_ASSERT_EQUAL_UINT16(0, store.count());
}

void test_read_rejects_indexes_outside_available_records() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  MeasurementRecord record = {};
  TEST_ASSERT_FALSE(store.read(0, record));
  TEST_ASSERT_TRUE(store.save({72, 2026, 10, 11, 18, 25, 9}));
  TEST_ASSERT_FALSE(store.read(1, record));
}

void test_invalid_language_is_ignored_and_invalid_metadata_resets_language() {
  FakeEeprom eeprom;
  MeasurementStore<FakeEeprom> store(eeprom);
  store.initialize();
  store.setLanguage(1);
  store.setLanguage(2);
  TEST_ASSERT_EQUAL_UINT8(1, store.language());

  eeprom.corrupt(3, 0);
  MeasurementStore<FakeEeprom> restartedStore(eeprom);
  restartedStore.initialize();
  TEST_ASSERT_EQUAL_UINT8(0, restartedStore.language());
}

void test_bpm_averager_emits_mean_after_ten_valid_readings() {
  BpmAverager averager;
  for (uint16_t bpm = 70; bpm < 79; ++bpm) {
    TEST_ASSERT_FALSE(averager.add(bpm, true));
  }
  TEST_ASSERT_EQUAL_UINT8(9, averager.count());
  TEST_ASSERT_TRUE(averager.add(79, true));
  TEST_ASSERT_EQUAL_UINT16(74, averager.average());
  TEST_ASSERT_EQUAL_UINT8(0, averager.count());
}

void test_invalid_reading_restarts_consecutive_measurement_average() {
  BpmAverager averager;
  for (uint8_t i = 0; i < 6; ++i) {
    TEST_ASSERT_FALSE(averager.add(70, true));
  }
  TEST_ASSERT_FALSE(averager.add(0, false));
  TEST_ASSERT_EQUAL_UINT8(0, averager.count());
  for (uint8_t i = 0; i < 9; ++i) {
    TEST_ASSERT_FALSE(averager.add(80, true));
  }
  TEST_ASSERT_TRUE(averager.add(80, true));
  TEST_ASSERT_EQUAL_UINT16(80, averager.average());
}

void test_explicit_average_reset_clears_progress_and_previous_result() {
  BpmAverager averager;
  TEST_ASSERT_FALSE(averager.add(72, true));
  TEST_ASSERT_EQUAL_UINT8(1, averager.count());
  averager.reset();
  TEST_ASSERT_EQUAL_UINT8(0, averager.count());
  TEST_ASSERT_EQUAL_UINT16(0, averager.average());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_records_and_language_survive_store_reinitialization);
  RUN_TEST(test_full_eeprom_rejects_new_records_without_overwriting_old_data);
  RUN_TEST(test_clear_removes_records_but_preserves_language);
  RUN_TEST(test_startup_finishes_an_interrupted_clear);
  RUN_TEST(test_startup_finishes_clear_if_power_fails_after_first_marker);
  RUN_TEST(test_corrupted_checksum_is_not_returned_as_a_record);
  RUN_TEST(test_invalid_date_or_bpm_is_rejected);
  RUN_TEST(test_read_rejects_indexes_outside_available_records);
  RUN_TEST(test_invalid_language_is_ignored_and_invalid_metadata_resets_language);
  RUN_TEST(test_bpm_averager_emits_mean_after_ten_valid_readings);
  RUN_TEST(test_invalid_reading_restarts_consecutive_measurement_average);
  RUN_TEST(test_explicit_average_reset_clears_progress_and_previous_result);
  return UNITY_END();
}
