#pragma once

#include "esphome/components/usb_host/usb_host.h"
#include "esphome/components/usb_uvc/usb_uvc.h"

namespace esphome {
namespace usb_uvc {

// Thin USBClient that detects UVC devices by interface class (0x0E) and delegates
// to the uvc_host driver. Mirrors the MSCDetector pattern in usb_storage:
//  - setup(): registers USB client + installs esp_video UVC pipeline + installs uvc_host driver
//  - loop(): polls USB events non-blocking (no dedicated task, no transfer buffers needed)
//  - on_connected(): logs VID/PID, then releases the device so uvc_host can open it uncontested
//  - on_removed(): notifies of disconnection
// uvc_host registers its own USB client and owns device open/interface-claim/stream entirely.
class UsbUvcComponent : public usb_host::USBClient {
 public:
  explicit UsbUvcComponent(uint16_t vid, uint16_t pid) : USBClient(vid, pid) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_uvc_dev_num(uint32_t n) { this->uvc_dev_num_ = n; }
  void set_task_stack(uint32_t s) { this->task_stack_ = s; }
  void set_task_priority(uint8_t p) { this->task_priority_ = p; }
  void set_task_affinity(int8_t a) { this->task_affinity_ = a; }

  // Match on USB Video Class (0x0E); VID/PID applied as secondary filter (0/0 = any).
  uint8_t get_interface_class() const override { return 0x0E; }

 protected:
  void on_connected() override;
  void on_removed(usb_device_handle_t handle) override;

  uint32_t uvc_dev_num_{1};
  uint32_t task_stack_{4096};
  uint8_t task_priority_{5};
  int8_t task_affinity_{-1};

  bool driver_installed_{false};
  uint8_t connected_addr_{0};
};

}  // namespace usb_uvc
}  // namespace esphome
