#include "usb_uvc_component.h"
#include "esphome/core/log.h"
#include "usb/uvc_host.h"
#include "usb/usb_host.h"

namespace esphome {
namespace usb_uvc {

static const char *const TAG = "usb_uvc";

void UsbUvcComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up USB UVC...");

  // Register as a USB host client — skip transfer buffer pool and dedicated task.
  // UsbUvcComponent never does transfers; it only needs connect/disconnect events,
  // which are polled non-blocking in loop(). Mirrors MSCDetector::setup().
  usb_host_client_config_t config{
      .is_synchronous = false,
      .max_num_event_msg = 5,
      .async = {.client_event_callback = usb_host::USBClient::client_event_cb, .callback_arg = this}};
  auto err = usb_host_client_register(&config, &this->handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "USB client register failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  // Install the esp_video V4L2 UVC pipeline slots (/dev/video4x).
  // Fills in event_cb + event_cb_ctx that uvc_host must use to signal
  // the pipeline when a camera connects.
  esp_video_uvc_driver_config_t video_cfg = {};
  video_cfg.uvc_dev_num   = this->uvc_dev_num_;
  video_cfg.task_stack    = this->task_stack_;
  video_cfg.task_priority = this->task_priority_;
  video_cfg.task_affinity = this->task_affinity_;

  err = esp_video_install_usb_uvc_driver(&video_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_install_usb_uvc_driver failed: %s", esp_err_to_name(err));
    usb_host_client_deregister(this->handle_);
    this->mark_failed();
    return;
  }

  // Install the uvc_host driver on top of the already-running USB host
  // (usb_host component called usb_host_install()). Wire its event_cb directly
  // to the esp_video pipeline callback — no indirection needed.
  const uvc_host_driver_config_t uvc_cfg = {
      .driver_task_stack_size = this->task_stack_,
      .driver_task_priority   = this->task_priority_,
      .xCoreID                = this->task_affinity_ >= 0 ? (BaseType_t) this->task_affinity_ : tskNO_AFFINITY,
      .create_background_task = true,
      .event_cb               = video_cfg.event_cb,
      .user_ctx               = video_cfg.event_cb_ctx,
  };
  err = uvc_host_install(&uvc_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "uvc_host_install failed: %s", esp_err_to_name(err));
    esp_video_uninstall_usb_uvc_driver();
    usb_host_client_deregister(this->handle_);
    this->mark_failed();
    return;
  }

  this->driver_installed_ = true;
  ESP_LOGI(TAG, "USB UVC ready — waiting for camera");
}

void UsbUvcComponent::loop() {
  // Poll events non-blocking instead of using a dedicated task.
  // Mirrors MSCDetector::loop().
  usb_host_client_handle_events(this->handle_, 0);
  this->process_usb_events_();
}

void UsbUvcComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "USB UVC:");
  ESP_LOGCONFIG(TAG, "  VID: 0x%04X  PID: 0x%04X%s", this->vid_, this->pid_,
                (this->vid_ == 0 && this->pid_ == 0) ? " (any)" : "");
  ESP_LOGCONFIG(TAG, "  UVC slots: %" PRIu32, this->uvc_dev_num_);
  ESP_LOGCONFIG(TAG, "  Driver installed: %s", this->driver_installed_ ? "YES" : "NO");
}

// Called by USBClient base after VID/PID check and interface class 0x0E confirmed.
// We only log the device identity then immediately release it — uvc_host registers
// its own USB client and owns device open / interface claim / stream entirely.
// Mirrors MSCDetector::on_connected().
void UsbUvcComponent::on_connected() {
  const usb_device_desc_t *desc;
  if (usb_host_get_device_descriptor(this->device_handle_, &desc) == ESP_OK) {
    this->connected_addr_ = static_cast<uint8_t>(this->device_addr_);
    ESP_LOGI(TAG, "UVC camera detected: addr=%d VID=0x%04X PID=0x%04X",
             this->connected_addr_, desc->idVendor, desc->idProduct);
  }
  // Release our handle so uvc_host can open the device uncontested.
  this->disconnect();
}

// Called when the device is removed.
// Mirrors MSCDetector::on_removed().
void UsbUvcComponent::on_removed(usb_device_handle_t handle) {
  if (this->connected_addr_ != 0) {
    ESP_LOGI(TAG, "UVC camera removed: addr=%d", this->connected_addr_);
    this->connected_addr_ = 0;
  }
  USBClient::on_removed(handle);
}

}  // namespace usb_uvc
}  // namespace esphome
