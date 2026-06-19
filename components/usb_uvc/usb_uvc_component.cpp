#include "usb_uvc_component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace usb_uvc {

static const char *const TAG = "usb_uvc";

void UsbUvcComponent::setup() {
    /* USBClient::setup() registers the USB client handle (this->handle_),
     * allocates the transfer pool and starts the USB task. */
    usb_host::USBClient::setup();
    if (this->is_failed()) {
        return;
    }

    /* Install the esp_video V4L2 pipeline slots (/dev/videoX).
     * Fills in event_cb + event_cb_ctx — the callback to call when a camera connects. */
    esp_video_uvc_driver_config_t video_cfg = {};
    video_cfg.uvc_dev_num   = this->uvc_dev_num_;
    video_cfg.task_stack    = this->task_stack_;
    video_cfg.task_priority = this->task_priority_;
    video_cfg.task_affinity = this->task_affinity_;

    esp_err_t err = esp_video_install_usb_uvc_driver(&video_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_install_usb_uvc_driver failed: %s", esp_err_to_name(err));
        this->mark_failed();
        return;
    }

    this->event_cb_     = video_cfg.event_cb;
    this->event_cb_ctx_ = video_cfg.event_cb_ctx;

    /* Initialise the uvc_host driver internals using our already-registered
     * USB client handle. This replaces uvc_host_install() and avoids
     * registering a second USB client that would interfere with usb_storage. */
    err = uvc_host_driver_init(this->handle_, this->event_cb_, this->event_cb_ctx_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uvc_host_driver_init failed: %s", esp_err_to_name(err));
        esp_video_uninstall_usb_uvc_driver();
        this->mark_failed();
        return;
    }

    this->driver_installed_ = true;
    ESP_LOGI(TAG, "USB UVC ready — waiting for camera");
}

void UsbUvcComponent::loop() {
    this->process_usb_events_();
}

void UsbUvcComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "USB UVC:");
    ESP_LOGCONFIG(TAG, "  VID: 0x%04X  PID: 0x%04X%s", this->vid_, this->pid_,
                  (this->vid_ == 0 && this->pid_ == 0) ? " (any)" : "");
    ESP_LOGCONFIG(TAG, "  UVC slots: %" PRIu32, this->uvc_dev_num_);
    ESP_LOGCONFIG(TAG, "  Driver installed: %s", this->driver_installed_ ? "YES" : "NO");
}

void UsbUvcComponent::on_connected() {
    const usb_device_desc_t *desc;
    if (usb_host_get_device_descriptor(this->device_handle_, &desc) == ESP_OK) {
        ESP_LOGI(TAG, "UVC camera connected: addr=%d VID=0x%04X PID=0x%04X",
                 this->device_addr_, desc->idVendor, desc->idProduct);
    }

    this->connected_addr_ = static_cast<uint8_t>(this->device_addr_);

    /* Upper bound for frame_info allocation in esp_video.
     * uvc_host_get_frame_list() fills the actual entries; 32 covers all real cameras. */
    size_t frame_info_num = 32;

    /* Notify esp_video that a UVC device is connected.
     * esp_video will call uvc_host_stream_open() in response using connected_addr_. */
    if (this->event_cb_) {
        const uvc_host_driver_event_data_t event = {
            .type = UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED,
            .device_connected = {
                .dev_addr         = this->connected_addr_,
                .uvc_stream_index = 0,
                .frame_info_num   = frame_info_num,
            },
        };
        this->event_cb_(&event, this->event_cb_ctx_);
    }
}

void UsbUvcComponent::on_removed(usb_device_handle_t handle) {
    if (this->connected_addr_ != 0) {
        ESP_LOGI(TAG, "UVC camera removed: addr=%d", this->connected_addr_);
        this->connected_addr_ = 0;
    }
    usb_host::USBClient::on_removed(handle);
}

}  // namespace usb_uvc
}  // namespace esphome
