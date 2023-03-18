#pragma once

#include "esphome/core/component.h"
#include "esphome/components/remote_base/remote_base.h"

namespace esphome {
namespace remote_receiver_nf {

struct RemoteReceiverNFComponentStore {
  static void gpio_intr(RemoteReceiverNFComponentStore *arg);

  /// Stores the time (in micros) that the leading/falling edge happened at
  ///  * An even index means a falling edge appeared at the time stored at the index
  ///  * An uneven index means a rising edge appeared at the time stored at the index
  volatile uint32_t *buffer{nullptr};
  /// The position last written to
  volatile uint32_t buffer_write_at;
  /// The position last read from
  uint32_t buffer_read_at{0};
  bool overflow{false};
  // should be buffer_size -1
  uint32_t buffer_size_limit{999};
  uint32_t loop{0};
  ISRInternalGPIOPin pin;
};

class RemoteReceiverNFComponent : public remote_base::RemoteReceiverBase, public Component {
 public:
  RemoteReceiverNFComponent(InternalGPIOPin *pin, uint8_t mem_block_num = 1) : RemoteReceiverBase(pin) {}
  void setup() override;
  void dump_config() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_buffer_size(uint32_t buffer_size) { this->buffer_size_limit_ = buffer_size-1; }
  void set_filter_us(uint8_t filter_us) { this->filter_us_ = filter_us; }
  void set_idle_us(uint32_t idle_us) { this->idle_us_ = idle_us; }
  void set_space_lvl_high(bool space_lvl_high) { this->space_lvl_high_ = space_lvl_high; }
  void set_sync_space_min_us(uint32_t sync_space_min_us) { this->sync_space_min_us_ = sync_space_min_us; }
  void set_sync_space_max_us(uint32_t sync_space_max_us) { this->sync_space_max_us_ = sync_space_max_us; }
  void set_num_edge_min(uint32_t num_edge_min) { this->num_edge_min_ = num_edge_min; }
  void set_num_edge_max(uint32_t num_edge_max) { this->num_edge_max_ = num_edge_max; }

 protected:
  void preprocessing ();
  RemoteReceiverNFComponentStore store_;
  HighFrequencyLoopRequester high_freq_;

  int32_t *	pulse_buffer_ {nullptr};
  uint32_t	pulse_write_at_{0};
  uint32_t	pulse_read_at_{0};
  uint32_t	pulse_process_at_{0};
  bool		recv_data_ {false};

  uint32_t	buffer_size_limit_{999};
  uint8_t	filter_us_{10};
  uint32_t idle_us_{10000};
  bool space_lvl_high_{false};
  uint32_t sync_space_min_us_{8000};
  uint32_t sync_space_max_us_{10000};
  uint32_t num_edge_min_{10};
  uint32_t num_edge_max_{200};
};

}  // namespace remote_receiver_nf
}  // namespace esphome
