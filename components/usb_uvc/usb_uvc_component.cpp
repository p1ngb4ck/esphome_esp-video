#include "usb_uvc_component.h"
#include "esphome/core/log.h"
#include "usb/uvc_host.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

namespace esphome {
namespace usb_uvc {

static const char *const TAG = "usb_uvc";

static constexpr uint8_t UVC_SC_VIDEOCONTROL  = 0x01;
static constexpr uint8_t UVC_SC_VIDEOSTREAMING = 0x02;

void UsbUvcComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up USB UVC...");

  // Step 1: install the esp_video V4L2 UVC pipeline slots (/dev/video4x).
  // This also gives us back the event_cb + context that uvc_host must use
  // to signal the pipeline when a camera connects.
  esp_video_uvc_driver_config_t video_cfg = {};
  video_cfg.uvc_dev_num   = this->uvc_dev_num_;
  video_cfg.task_stack    = this->task_stack_;
  video_cfg.task_priority = this->task_priority_;
  video_cfg.task_affinity = this->task_affinity_;

  esp_err_t ret = esp_video_install_usb_uvc_driver(&video_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_install_usb_uvc_driver failed: %s", esp_err_to_name(ret));
    this->mark_failed();
    return;
  }

  // Step 2: install the uvc_host driver on top of the already-running USB host
  // (usb_host component called usb_host_install()). Wire its event_cb directly
  // to the esp_video pipeline callback obtained above — no indirection needed.
  const uvc_host_driver_config_t uvc_cfg = {
      .driver_task_stack_size = this->task_stack_,
      .driver_task_priority   = this->task_priority_,
      .xCoreID                = this->task_affinity_ >= 0 ? (BaseType_t) this->task_affinity_ : tskNO_AFFINITY,
      .create_background_task = true,
      .event_cb               = video_cfg.event_cb,
      .user_ctx               = video_cfg.event_cb_ctx,
  };
  ret = uvc_host_install(&uvc_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "uvc_host_install failed: %s", esp_err_to_name(ret));
    esp_video_uninstall_usb_uvc_driver();
    this->mark_failed();
    return;
  }

  this->driver_installed_ = true;
  ESP_LOGI(TAG, "USB UVC ready — waiting for camera");
}

void UsbUvcComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "USB UVC:");
  ESP_LOGCONFIG(TAG, "  VID: 0x%04X  PID: 0x%04X%s", this->vid_, this->pid_,
                (this->vid_ == 0 && this->pid_ == 0) ? " (any)" : "");
  ESP_LOGCONFIG(TAG, "  UVC slots: %" PRIu32, this->uvc_dev_num_);
  ESP_LOGCONFIG(TAG, "  Driver installed: %s", this->driver_installed_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Camera connected: %s", this->camera_connected_ ? "YES" : "NO");
}

// Called by USBClient base after:
//   1. Device descriptor fetched (VID/PID matched or 0/0 wildcard)
//   2. Config descriptor scanned — interface class 0x0E found
// Descriptors are cached by IDF USB host stack; no extra USB transfers needed.
void UsbUvcComponent::on_connected() {
  const usb_device_desc_t *dev_desc;
  const usb_config_desc_t *cfg_desc;

  if (usb_host_get_device_descriptor(this->device_handle_, &dev_desc) != ESP_OK) {
    ESP_LOGE(TAG, "get_device_descriptor failed");
    this->disconnect();
    return;
  }
  if (usb_host_get_active_config_descriptor(this->device_handle_, &cfg_desc) != ESP_OK) {
    ESP_LOGE(TAG, "get_active_config_descriptor failed");
    this->disconnect();
    return;
  }

  ESP_LOGI(TAG, "UVC camera connected: VID=0x%04X PID=0x%04X", dev_desc->idVendor, dev_desc->idProduct);

  // Walk the config descriptor to find the VideoControl interface number.
  // uvc_host handles VideoStreaming enumeration itself after we claim VC.
  uint8_t vc_intf = 0xFF;
  int offset = 0;
  const usb_standard_desc_t *desc = reinterpret_cast<const usb_standard_desc_t *>(cfg_desc);
  while ((desc = usb_parse_next_descriptor_of_type(desc, cfg_desc->wTotalLength,
                                                    USB_W_VALUE_DT_INTERFACE, &offset)) != nullptr) {
    const auto *intf = reinterpret_cast<const usb_intf_desc_t *>(desc);
    if (intf->bInterfaceClass == 0x0E && intf->bInterfaceSubClass == UVC_SC_VIDEOCONTROL) {
      vc_intf = intf->bInterfaceNumber;
      ESP_LOGD(TAG, "Found UVC VideoControl interface: %d", vc_intf);
      break;
    }
  }

  if (vc_intf == 0xFF) {
    ESP_LOGE(TAG, "UVC VideoControl interface not found");
    this->disconnect();
    return;
  }

  // Claim the VideoControl interface so UVC class requests are accepted.
  // uvc_host will claim the VideoStreaming interface itself when opening a stream.
  esp_err_t ret = usb_host_interface_claim(this->handle_, this->device_handle_, vc_intf, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to claim VideoControl interface %d: %s", vc_intf, esp_err_to_name(ret));
    this->disconnect();
    return;
  }

  this->vc_intf_ = vc_intf;
  this->camera_connected_ = true;

  // uvc_host's background task has already seen this device via its own USB
  // client registration and will fire UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED
  // through the event_cb we wired in setup() -> uvc_host_install().
  // That callback (uvc_host_driver_event_callback in esp_video_usb_uvc_device.c)
  // gives the ready semaphore so the esp_video pipeline unblocks.
  // Nothing more to do here.
}

void UsbUvcComponent::on_disconnected() {
  if (this->camera_connected_) {
    ESP_LOGI(TAG, "UVC camera disconnected");
    if (this->vc_intf_ != 0xFF) {
      usb_host_interface_release(this->handle_, this->device_handle_, this->vc_intf_);
      this->vc_intf_ = 0xFF;
    }
    this->camera_connected_ = false;
  }
  USBClient::on_disconnected();
}

}  // namespace usb_uvc
}  // namespace esphome
