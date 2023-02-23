#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "remote_base.h"

namespace esphome {
namespace remote_base {

struct EV1527Data {
  uint32_t address;
  uint8_t command;
  uint8_t repeat;
};

class EV1527Protocol : public RemoteProtocol<EV1527Data> {
 public:
  void encode(RemoteTransmitData *dst, const EV1527Data &data) override;
  optional<EV1527Data> decode(RemoteReceiveData src) override;
  void dump(const EV1527Data &data) override;
};

DECLARE_REMOTE_PROTOCOL(EV1527)

template<typename... Ts> class EV1527Action : public RemoteTransmitterActionBase<Ts...> {
 public:
  TEMPLATABLE_VALUE(uint16_t, address)
  TEMPLATABLE_VALUE(uint8_t, command)

  void encode(RemoteTransmitData *dst, Ts... x) override {
    EV1527Data data{};
    data.address = this->address_.value(x...);
    data.command = this->command_.value(x...);
    data.repeat = this->repeat_.value(x...);
    EV1527Protocol().encode(dst, data);
  }
};

}  // namespace remote_base
}  // namespace esphome
