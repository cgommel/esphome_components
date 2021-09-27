#pragma once

#include "esphome/components/sml/sml.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace sml {

class SmlTextSensor : public SmlListener, public text_sensor::TextSensor, public Component {
 public:
  SmlTextSensor(std::string server_id, std::string obis, std::string format);
  void publish_val(const ObisInfo &obis_info) override;
  void dump_config() override;

 protected:
  std::string format_;
};

}  // namespace sml
}  // namespace esphome
