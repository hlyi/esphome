#include "remote_receiver_nf.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#define INC_BUFFER_PTR(var) ((var) == buffer_size_limit ? 0 : ((var) + 1))
static const uint8_t MIN_NONIDLE_DIST = 6;
;

namespace esphome {
namespace remote_receiver_nf {

static const char *const TAG = "remote_receiver_nf";

void IRAM_ATTR HOT RemoteReceiverNFComponentStore::gpio_intr(RemoteReceiverNFComponentStore *arg) {
  const uint32_t now = micros();
  const bool level = arg->pin.digital_read();
  const uint32_t read_at = arg->buffer_read_at;
  uint32_t time_since_change = now - arg->last_edge_time;
  uint32_t next;
  bool lvl_high;
  arg->last_edge_time = now;
  uint32_t buffer_size_limit;
  // filter glitch
  //  if (time_since_change <= arg->filter_us)
  //    return;
  buffer_size_limit = arg->buffer_size - 1;  // avoid % operation as it is costly
  lvl_high = arg->space_lvl_high;
  switch (arg->sync_stage) {
    case WAIT_SYNC_MARK:
      // ignore unwanted the edge
      if (level == lvl_high)
        return;
      // rising edge when space_lvl_high is low or falling edge when space_lvl_high is high)
      arg->sync_mark_time = now;
      arg->sync_stage = WAIT_SYNC_SPACE;
      return;
      break;
    case WAIT_SYNC_SPACE:
      // ignore unwanted the edge
      if (level != lvl_high) {
        arg->sync_stage = WAIT_SYNC_MARK;
        return;
      }
      arg->sync_space_time = now;
      arg->sync_stage = WAIT_DATA_MARK;
      return;
      break;
    case WAIT_DATA_MARK:
      // ignore unwanted the edge
      if (level == lvl_high) {
        arg->sync_stage = WAIT_SYNC_MARK;
        return;
      }
      time_since_change = now - arg->sync_space_time;
      if ((time_since_change < arg->sync_space_min_us) || (time_since_change > arg->sync_space_max_us)) {
        // this is not sync pattern, assuming it could be begining of sync pattern
        arg->sync_stage = WAIT_SYNC_SPACE;
        arg->sync_mark_time = now;
        return;
      }
      // sync detected, need push two previous timestamps into ring buffer
      next = INC_BUFFER_PTR(arg->buffer_write_at);
      if (next == read_at)
        return;  // fifo full skip
      if (level != (next & 1)) {
        // noisy signal could park idle at wrong polarity
        // fill in previous value to crete 0 delta then filter by main loop
        arg->buffer[next] =
            arg->sync_mark_time;  // duplicate mark time to create zero delay, will be filter in main loop
        arg->buffer_write_at = next;
        next = INC_BUFFER_PTR(arg->buffer_write_at);
        if (next == read_at)
          return;
      }
      arg->buffer[arg->buffer_write_at = next] = arg->sync_mark_time;
      next = INC_BUFFER_PTR(arg->buffer_write_at);
      if (next == read_at)
        return;
      arg->buffer[arg->buffer_write_at = next] = arg->sync_space_time;
      // common code to write current edge
      arg->sync_stage = WAIT_REP_MARK;
      break;
    case WAIT_REP_MARK:
      if (time_since_change > arg->idle_us) {
        if (level == lvl_high) {
          arg->sync_stage = WAIT_SYNC_MARK;
        } else {
          arg->sync_mark_time = now;
          arg->sync_stage = WAIT_SYNC_SPACE;
        }
        return;
      }
      if (level != lvl_high) {
        // mark detected
        // if find another sync, keep as is
        if ((time_since_change > arg->sync_space_min_us) && (time_since_change < arg->sync_space_max_us))
          break;
        if ((time_since_change > arg->rep_space_min_us)) {
          // start of repeat pattern
          // insert special marker
          next = INC_BUFFER_PTR(arg->buffer_write_at);
          if (next == read_at)
            return;

          if (level != (next & 1)) {
            // in case misaligned due to fifo full
            arg->buffer[next] = arg->buffer[arg->buffer_write_at];  // duplicate mark time to create zero delay, will be
                                                                    // filter in main loop
            arg->buffer_write_at = next;
            next = INC_BUFFER_PTR(arg->buffer_write_at);
            if (next == read_at)
              return;
          }

          arg->buffer[arg->buffer_write_at = next] = -1;
          next = INC_BUFFER_PTR(arg->buffer_write_at);
          if (next == read_at)
            return;
          arg->buffer[arg->buffer_write_at = next] = -1;
          arg->sync_mark_time = now;
          arg->sync_stage = WAIT_SYNC_SPACE;
          return;
        }
      }
      break;
    default:
      arg->sync_mark_time = WAIT_SYNC_MARK;
      return;
      break;
  }
  next = INC_BUFFER_PTR(arg->buffer_write_at);
  if (next == read_at)
    return;
  if (level != (next & 1)) {
    // in case misaligned due to fifo full
    arg->buffer[next] =
        arg->buffer[arg->buffer_write_at];  // duplicate mark time to create zero delay, will be filter in main loop
    arg->buffer_write_at = next;
    next = INC_BUFFER_PTR(arg->buffer_write_at);
    if (next == read_at)
      return;
  }
  arg->buffer[arg->buffer_write_at = next] = now;
}

void RemoteReceiverNFComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Remote Receiver With Noise Filtering...");
  this->pin_->setup();
  auto &s = this->store_;
  s.filter_us = this->filter_us_;
  s.pin = this->pin_->to_isr();
  s.buffer_size = this->buffer_size_;
  s.space_lvl_high = this->space_lvl_high_;
  s.sync_space_min_us = this->sync_space_min_us_;
  s.sync_space_max_us = this->sync_space_max_us_;
  s.rep_space_min_us = this->rep_space_min_us_;
  s.idle_us = this->idle_us_;

  this->high_freq_.start();
  if (s.buffer_size & 1) {
    // Make sure divisible by two. This way, we know that every 0bxxx0 index is a space and every 0bxxx1 index is a mark
    s.buffer_size++;
  }

  s.buffer = new uint32_t[s.buffer_size];
  void *buf = (void *) s.buffer;
  memset(buf, 0, s.buffer_size * sizeof(uint32_t));

  // First index is a space.
  if (this->space_lvl_high_) {
    s.buffer_write_at = s.buffer_read_at = 1;
  } else {
    s.buffer_write_at = s.buffer_read_at = 0;
  }
  this->pin_->attach_interrupt(RemoteReceiverNFComponentStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);
}
void RemoteReceiverNFComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Remote Receiver NF:");
  LOG_PIN("  Pin: ", this->pin_);
  if (this->pin_->digital_read()) {
    ESP_LOGW(TAG, "Remote Receiver Signal starts with a HIGH value. Usually this means you have to "
                  "invert the signal using 'inverted: True' in the pin schema!");
  }
  ESP_LOGCONFIG(TAG, "  Buffer Size: %u", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Tolerance: %u%%", this->tolerance_);
  ESP_LOGCONFIG(TAG, "  Filter out pulses shorter than: %u us", this->filter_us_);
  ESP_LOGCONFIG(TAG, "  Signal is done after %u us of no changes", this->idle_us_);
  ESP_LOGCONFIG(TAG, "  Level of signal space is %s", this->space_lvl_high_ ? "high" : "low");
  ESP_LOGCONFIG(TAG, "  Sync space is between %u us and %u us", this->sync_space_min_us_, this->sync_space_max_us_);
  ESP_LOGCONFIG(TAG, "  Repeat signal has space between %u us and %u us", this->rep_space_min_us_, this->idle_us_);
  ESP_LOGCONFIG(TAG, "  Early check threshold: %d (0 - disable)", this->early_check_thres_);
  ESP_LOGCONFIG(TAG, "  Minimum number of edges for the protocol: %d (0 - disable)", this->num_edge_min_);
}

void RemoteReceiverNFComponent::loop() {
  auto &s = this->store_;

  // copy write at to local variables, as it's volatile
  const uint32_t write_at = s.buffer_write_at;
  const uint32_t dist = (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  const uint32_t buffer_size_limit = s.buffer_size - 1;
  // signals must at least one rising and one leading edge
  if (dist <= 1)
    return;
  const uint32_t now = micros();
  const bool idle_cond = ((now - s.buffer[write_at]) > this->idle_us_) && (s.buffer[write_at] != -1);
  if (!idle_cond && ((this->early_check_thres_ == 0) || (this->early_check_thres_ > dist))) {
    // The last change was fewer than the configured idle time ago.
    return;
  }

  if (idle_cond && (dist < this->num_edge_min_)) {
    // message is too short to be a valid control signal ignore it.
    s.buffer_read_at = write_at;
    return;
  }

  /*
    ESP_LOGD(TAG, "read_at=%u write_at=%u dist=%u now=%u end=%u", s.buffer_read_at, write_at, dist, now,
              s.buffer[write_at]);
  */

  if (!idle_cond) {
    bool early_proc = false;
    if (dist < MIN_NONIDLE_DIST)
      return;
    // because preview, need search for repeat marks or sync pattern
    uint32_t prev_time = s.buffer[(s.buffer_read_at + MIN_NONIDLE_DIST - 1) % s.buffer_size];
    for (int j = (s.buffer_read_at + MIN_NONIDLE_DIST) % s.buffer_size; j != write_at; j = INC_BUFFER_PTR(j)) {
      uint32_t chk_time = s.buffer[j];
      if ((chk_time == -1) && (j != write_at) && s.buffer[INC_BUFFER_PTR(j)] == -1) {
        early_proc = true;
        //         ESP_LOGD(TAG, "Found Special break at %d, %d,%d",j, prev_time, chk_time );
        break;
      }
      uint32_t pulse_width = chk_time - prev_time;
      prev_time = chk_time;
      if (((j & 1) != this->space_lvl_high_) && (pulse_width > this->sync_space_min_us_) &&
          (pulse_width < this->sync_space_max_us_)) {
        early_proc = true;
        //         ESP_LOGD(TAG, "Found Sync");
        break;
      }
    }
    //    ESP_LOGD(TAG, "Dist=%d, found=%d", dist, early_proc);
    // if line is too noisy, after fill out half of the buffer, force to process
    if ((!early_proc) && dist < (s.buffer_size / 2))
      return;
  }

  ESP_LOGVV(TAG, "read_at=%u write_at=%u dist=%u now=%u end=%u", s.buffer_read_at, write_at, dist, now,
            s.buffer[write_at]);
  // Skip first value, it's from the previous idle level
  uint32_t prev = INC_BUFFER_PTR(s.buffer_read_at);
  s.buffer_read_at = INC_BUFFER_PTR(prev);
  if ((prev & 1) == s.space_lvl_high) {
    // idle parked at wrong polarity, skip a sample
    prev = s.buffer_read_at;
    s.buffer_read_at = INC_BUFFER_PTR(prev);
  }
  const uint32_t reserve_size = 1 + (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  int32_t multiplier = s.buffer_read_at & 1 ? -1 : 1;
  std::vector<int32_t> buf(reserve_size);

  for (uint32_t i = 0; prev != write_at; i++) {
    int32_t read_at_val = s.buffer[s.buffer_read_at];
    int32_t delta = read_at_val - s.buffer[prev];
    int32_t nxt_read_at = INC_BUFFER_PTR(s.buffer_read_at);
    int32_t nxt_read_at_val = s.buffer[nxt_read_at];
    int32_t nxt2_read_at = INC_BUFFER_PTR(nxt_read_at);

    // find special repeat pattern marks
    if (read_at_val == -1 && s.buffer_read_at != write_at && nxt_read_at_val == -1) {
      s.buffer_read_at = INC_BUFFER_PTR(nxt_read_at);
      break;
    }
    // find sync space
    int32_t delta2 = s.buffer[nxt2_read_at] - nxt_read_at_val;
    if (((nxt_read_at & 1) == this->space_lvl_high_) && (nxt_read_at != write_at) &&
        (delta2 > this->sync_space_min_us_) && (delta2 < this->sync_space_max_us_))
      break;

    if (uint32_t(delta) >= this->idle_us_) {
      // already found a space longer than idle. There must have been two pulses
      break;
    }

    ESP_LOGVV(TAG, "  i=%u buffer[%u]=%u - buffer[%u]=%u -> %d", i, s.buffer_read_at, s.buffer[s.buffer_read_at], prev,
              s.buffer[prev], multiplier * delta);
    buf.push_back(multiplier * delta);
    prev = s.buffer_read_at;
    s.buffer_read_at = INC_BUFFER_PTR(prev);
    multiplier *= -1;
  }
  s.buffer_read_at = (s.buffer_size + s.buffer_read_at - 1) % s.buffer_size;
  buf.push_back(this->idle_us_ * multiplier);

  // now do glitch filtering
  this->temp_.clear();
  this->temp_.reserve(buf.size());
  int cnt = 0;
  for (auto it = buf.begin(); it != buf.end(); ++it) {
    int32_t val = *it;
    if (abs(val) < this->filter_us_) {
      it++;
      if (it != buf.end()) {
        if (cnt == 0) {
          this->temp_.push_back(*it - val);
          cnt++;
        } else {
          this->temp_[cnt - 1] += *it - val;
        }
      } else {
        // single pulse, just drop it.
        if (cnt != 0) {
          this->temp_[cnt - 1] -= val;
        }
      }
    } else {
      this->temp_.push_back(val);
      cnt++;
    }
  }

  this->call_listeners_dumpers_();
}

}  // namespace remote_receiver_nf
}  // namespace esphome