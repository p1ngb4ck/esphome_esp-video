"""
ESPHome component for Espressif ESP-Video (v1.4.0)
JPEG support with ESP-IDF dependencies

Initializes ESP-Video using the ESPHome I2C bus.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, esp32
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
from esphome.const import CONF_ID, CONF_I2C_ID
from esphome.core import CORE
import os
import logging

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32", "i2c"]
AUTO_LOAD = []

esp_video_ns = cg.esphome_ns.namespace("esp_video")
ESPVideoComponent = esp_video_ns.class_("ESPVideoComponent", cg.Component)

# Configuration keys
CONF_ENABLE_JPEG = "enable_jpeg"
CONF_ENABLE_ISP = "enable_isp"
CONF_ENABLE_UVC = "enable_uvc"
CONF_USE_HEAP_ALLOCATOR = "use_heap_allocator"
CONF_XCLK_PIN = "xclk_pin"
CONF_XCLK_FREQ = "xclk_freq"
CONF_ENABLE_XCLK_INIT = "enable_xclk_init"

# Use xclk_pin: -1 for boards with an external oscillator on the PCB
NO_CLOCK = -1

def parse_gpio_pin(value):
    """Parse a GPIO pin in ESPHome format (GPIO36 or -1)."""
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        if value == "-1" or value.upper() == "NO_CLOCK":
            return NO_CLOCK
        # "GPIO36" -> 36
        if value.upper().startswith("GPIO"):
            try:
                return int(value[4:])
            except ValueError:
                raise cv.Invalid(f"Invalid GPIO format: {value}. Use 'GPIO36' or -1")
        try:
            return int(value)
        except ValueError:
            raise cv.Invalid(f"Invalid GPIO format: {value}. Use 'GPIO36' or -1")
    raise cv.Invalid(f"Invalid pin type: {type(value)}")

def validate_esp_video_config(config):
    """Validate ESP-Video configuration."""
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(ESPVideoComponent),
        cv.Required(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
        cv.Optional(CONF_ENABLE_JPEG, default=True): cv.boolean,
        cv.Optional(CONF_ENABLE_ISP, default=True): cv.boolean,
        # USB-UVC host: enables plugging an external USB (UVC) camera into the
        # ESP32-P4 USB OTG port. Off by default (MIPI-CSI only). When true, the
        # USB host stack + UVC host driver are compiled in and started, and a
        # connected UVC camera is enumerated as a /dev/videoN V4L2 device.
        cv.Optional(CONF_ENABLE_UVC, default=False): cv.boolean,
        cv.Optional(CONF_USE_HEAP_ALLOCATOR, default=True): cv.boolean,
        # XCLK pin accepts: "GPIO36", 36, -1, or "NO_CLOCK"
        cv.Optional(CONF_XCLK_PIN, default="GPIO36"): cv.Any(cv.string, cv.int_range(min=-1, max=48)),
        cv.Optional(CONF_XCLK_FREQ, default=24000000): cv.int_range(min=1000000, max=40000000),  # 1-40 MHz
        # Enable XCLK initialization via LEDC (for non-M5Stack boards)
        cv.Optional(CONF_ENABLE_XCLK_INIT, default=False): cv.boolean,
    }).extend(cv.COMPONENT_SCHEMA),
    validate_esp_video_config
)


async def to_code(config):
    # ESP-IDF is required; Arduino framework is not supported
    if CORE.using_arduino:
        raise cv.Invalid(
            "ESP-Video requires the esp-idf framework. "
            "Add 'framework: type: esp-idf' to your configuration."
        )

    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Auto-download missing dependencies
    from .esp_video_download import ensure_esp_video_dependencies

    try:
        ensure_esp_video_dependencies(parent_components_dir)
    except Exception as e:
        logging.warning(
            f"[ESP-Video] Auto-download failed: {e}\n"
            f"If components are already present locally, this is OK."
        )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    i2c_bus = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_i2c_bus(i2c_bus))

    # CRITICAL: sensors need an active XCLK to respond on I2C
    xclk_pin_raw = config[CONF_XCLK_PIN]
    xclk_pin = parse_gpio_pin(xclk_pin_raw)
    xclk_freq = config[CONF_XCLK_FREQ]
    has_ext_clock = xclk_pin != NO_CLOCK

    cg.add(var.set_xclk_pin(cg.RawExpression(f"static_cast<gpio_num_t>({xclk_pin})")))
    cg.add(var.set_xclk_freq(xclk_freq))
    cg.add(var.set_enable_xclk_init(config[CONF_ENABLE_XCLK_INIT]))
    cg.add(var.set_enable_uvc(config[CONF_ENABLE_UVC]))

    # USB-UVC: the usb_uvc component owns USB host init and pulls the IDF UVC
    # driver. esp_video only validates that usb_uvc is present in the config.
    if config[CONF_ENABLE_UVC]:
        if "usb_uvc" not in CORE.config:
            raise cv.Invalid(
                "enable_uvc: true requires a 'usb_uvc:' entry in your configuration. "
                "The usb_uvc component manages USB host and UVC driver initialization."
            )

    logging.debug(f"[ESP-Video] I2C bus: '{config[CONF_I2C_ID]}'")
    if has_ext_clock:
        logging.debug(f"[ESP-Video] XCLK: GPIO{xclk_pin} @ {xclk_freq/1000000:.1f} MHz")
    else:
        logging.debug(f"[ESP-Video] XCLK: PCB oscillator @ {xclk_freq/1000000:.1f} MHz")

    # Register local components as IDF components via override_path.
    # This replaces the PlatformIO SCons build script — IDF/cmake picks up
    # each component's CMakeLists.txt, include dirs, and sources automatically.
    for comp_name in ("esp_video", "esp_cam_sensor", "esp_ipa", "esp_sccb_intf"):
        comp_path = os.path.join(parent_components_dir, comp_name)
        if os.path.exists(comp_path):
            add_idf_component(name=comp_name, override_path=comp_path)


    # ESP_VIDEO feature flags: use add_idf_sdkconfig_option so CMakeLists if(CONFIG_...)
    # guards evaluate correctly at cmake configure time (add_build_flag only adds -D
    # compiler flags and does NOT populate CMake/sdkconfig variables).
    add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE", True)
    add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_DISABLE_MIPI_CSI_DRIVER_BACKUP_BUFFER", True)

    if config[CONF_ENABLE_ISP]:
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_ISP", True)
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE", True)
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER", True)
        cg.add_build_flag("-DESP_VIDEO_ISP_ENABLED=1")

    if config[CONF_ENABLE_UVC]:
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE", True)

    if config[CONF_USE_HEAP_ALLOCATOR]:
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR", True)

    if config[CONF_ENABLE_JPEG]:
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE", True)
        add_idf_sdkconfig_option("CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE", True)
        cg.add_build_flag("-DESP_VIDEO_JPEG_ENABLED=1")

    # Camera sensor CONFIG_ symbols: these are consumed by C source only (not CMake guards),
    # so add_build_flag is correct here. They are also set via target_compile_definitions
    # in esp_cam_sensor/CMakeLists.txt for the sensor driver sources; these flags cover
    # any other translation units that include sensor headers.
    cg.add_build_flag("-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1")

    for flag in [
        "-Wno-unused-function",
        "-Wno-unused-variable",
        "-Wno-missing-field-initializers",
    ]:
        cg.add_build_flag(flag)

