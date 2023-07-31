#include "esphome/core/log.h"
#include "ld2410_number.h"

namespace esphome {
namespace ld2410 {

static const char *const TAG = "ld2410.number";

void LD2410Number::setup() {
  if ( parent_ == nullptr ) return;
  this->publish_state(parent_->get_threshold(this->gate_num_, this->gate_type_));
/*
  this->parent_->register_listener(this->number_id_, [this](const LD2410Datapoint &datapoint) {
    if (datapoint.type == LD2410DatapointType::INTEGER) {
      ESP_LOGV(TAG, "MCU reported number %u is: %d", datapoint.id, datapoint.value_int);
      this->publish_state(datapoint.value_int);
    } else if (datapoint.type == LD2410DatapointType::ENUM) {
      ESP_LOGV(TAG, "MCU reported number %u is: %u", datapoint.id, datapoint.value_enum);
      this->publish_state(datapoint.value_enum);
    }
    this->type_ = datapoint.type;
  });
  */
}

void LD2410Number::control(float value) {
  if ( parent_ == nullptr ) return;
  parent_->set_threshold(this->gate_num_, this->gate_type_ , (int) value );

  ESP_LOGV(TAG, "Setting number to gate %u, type %u: %f", this->gate_num_, this->gate_type_, value);
  this->publish_state(value);
}

void LD2410Number::dump_config() {
  LOG_NUMBER(TAG, "LD2410 Number", this);
  ESP_LOGCONFIG(TAG, "  Number is for gate %u, type %u", this->gate_num_, gate_type_);
}

}  // namespace tuya
}  // namespace esphome
