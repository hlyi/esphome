#include "ev1527_protocol.h"
#include "esphome/core/log.h"

namespace esphome {
namespace remote_base {

static const char *const TAG = "remote.ev1527";

static const uint32_t HEADER_HIGH_US = 281;
static const uint32_t HEADER_LOW_US = 8719;
static const uint32_t BIT_TOTAL_US = 1200;
static const uint32_t BIT_ONE_LOW_US = 300;
static const uint32_t BIT_ZERO_LOW_US = 900;
static const uint32_t REPEAT_DETECTION_WINDOW_MILLIS = 1000;  // one seconds

void EV1527Protocol::encode(RemoteTransmitData *dst, const EV1527Data &data) {
  dst->reserve(data.repeat * 50);

  uint32_t raw_code;

  raw_code = (data.address & 0xfffff) <<20;
  raw_code |= (uint32_t)data.command & 0xf ;

  for (int i = 0; i < data.repeat; i++) {
    dst->item(HEADER_HIGH_US, HEADER_LOW_US);

    for (uint32_t mask = 1 << 23; mask > 1; mask >>= 1) {
      if (raw_code & mask) {
        dst->item(BIT_TOTAL_US - BIT_ONE_LOW_US, BIT_ONE_LOW_US);
      } else {
        dst->item(BIT_TOTAL_US - BIT_ZERO_LOW_US, BIT_ZERO_LOW_US);
      }
    }

    if (raw_code & 1) {
      dst->item(BIT_TOTAL_US - BIT_ONE_LOW_US, BIT_ONE_LOW_US + BIT_TOTAL_US);
    } else {
      dst->item(BIT_TOTAL_US - BIT_ZERO_LOW_US, BIT_ZERO_LOW_US + BIT_TOTAL_US);
    }
  }
}
optional<EV1527Data> EV1527Protocol::decode(RemoteReceiveData src) {
  static uint32_t last_decoded_millis = 0;
  static uint64_t last_decoded_raw = 0;
  EV1527Data data{
      .address = 0,
      .command = 0,
      .repeat = 6,
  };

  uint32_t size = src.size();
  ESP_LOGD(TAG, "Data size = %d", size );

  if ( (size < 47 ) || (size > 60) ) return {};
  if (!src.expect_item(HEADER_HIGH_US, HEADER_LOW_US))
    return {};

  // receive 24bit data
  uint32_t raw_code = 0;
  for (uint64_t mask = 1 << 23; mask > 1; mask >>= 1) {
    if (src.expect_item(BIT_TOTAL_US - BIT_ONE_LOW_US, BIT_ONE_LOW_US)) {
      raw_code |= mask;
    } else if (src.expect_item(BIT_TOTAL_US - BIT_ZERO_LOW_US, BIT_ZERO_LOW_US) == 0) {
      return {};
    }
  }
  if (src.expect_mark(BIT_TOTAL_US - BIT_ONE_LOW_US)) {
    raw_code |= 1;
  } else if (src.expect_mark(BIT_TOTAL_US - BIT_ZERO_LOW_US) == 0) {
    return {};
  }

  ESP_LOGD(TAG, "Data Received = %06x", raw_code );
  data.address = (raw_code >> 4) & 0xfffff;
  data.command = raw_code & 0xf;

  // EV1527 sensors send an event multiple times, repeatition should be filtered.
  uint32_t now = millis();
  if (((now - last_decoded_millis) < REPEAT_DETECTION_WINDOW_MILLIS) && (last_decoded_raw == raw_code)) {
    // repeatition detected,ignore the code
    return {};
  }
  last_decoded_millis = now;
  last_decoded_raw = raw_code;
  return data;
}
void EV1527Protocol::dump(const EV1527Data &data) {
  ESP_LOGD(TAG, "Received EV1527: address=0x%04X, command=0x%02X", data.address, data.command);
}

}  // namespace remote_base
}  // namespace esphome
