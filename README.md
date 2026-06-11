# esphome_esp-video

External [ESPHome](https://esphome.io/) components for camera capture, sensor
drivers and LVGL camera display on Espressif ESP32 targets (notably the
ESP32-P4 with MIPI-CSI).

## Components

| Component | Description |
|-----------|-------------|
| `esp_video` | Video capture pipeline (CSI/DVP/ISP/JPEG/H264) built on the Espressif `esp_video` framework. |
| `esp_cam_sensor` | Camera sensor drivers (OV5647, OV02C10, SC2336, SC202CS, …). |
| `lvgl_camera_display` | LVGL widget to display the live camera stream, with optional detection overlays. |

## Usage

Reference this repository as an external component source in your ESPHome
configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/esphome_esp-video
    components: [esp_video, esp_cam_sensor, lvgl_camera_display]
```

## License

This project is released under the [MIT License](LICENSE).

Copyright holders:

- ESPHome
- Espressif Systems (Shanghai) CO LTD
- youkorr (Sapphire Younes)
