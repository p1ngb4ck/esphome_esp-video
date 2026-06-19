#pragma once

#include "esphome/core/component.h"
#include "esphome/components/usb_host/usb_host.h"
#include "usb_uvc.h"

namespace esphome {
namespace usb_uvc {

class UsbUvcComponent : public usb_host::USBClient {
 public:
  explicit UsbUvcComponent(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_uvc_dev_num(uint32_t n) { this->uvc_dev_num_ = n; }
  void set_task_stack(uint32_t s) { this->task_stack_ = s; }
  void set_task_priority(uint8_t p) { this->task_priority_ = p; }
  void set_task_affinity(int8_t a) { this->task_affinity_ = a; }

  // Match on USB Video Class (0x0E); VID/PID then applied as secondary filter.
  uint8_t get_interface_class() const override { return 0x0E; }

 protected:
  void on_connected() override;
  void on_disconnected() override;

  // Driver config
  uint32_t uvc_dev_num_{1};
  uint32_t task_stack_{4096};
  uint8_t task_priority_{5};
  int8_t task_affinity_{-1};

  // Runtime state
  bool driver_installed_{false};
  bool camera_connected_{false};
  uint8_t vc_intf_{0xFF};  // claimed VideoControl interface number
};

}  // namespace usb_uvc
}  // namespace esphome
