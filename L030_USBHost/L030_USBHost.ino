#include <Arduino.h>

#include "esp_err.h"
#include "tusb.h"

extern "C" esp_err_t init_usb_hal(bool external_phy);

// 流程概览：
// 1. setup() 初始化串口、USB PHY 和 TinyUSB Host。
// 2. loop() 持续调用 tuh_task()，让 TinyUSB 处理插拔和枚举事件。
// 3. 插入设备后，TinyUSB 先触发 tuh_enum_descriptor_device_cb()，这里打印设备描述符。
// 4. 设备成功配置后，TinyUSB 再触发 tuh_mount_cb()，这里补读字符串描述符和有效 class。
// 5. 拔出设备时，TinyUSB 触发 tuh_umount_cb()。

// ESP32-S3 has one native USB OTG controller. This sketch uses it as a
// TinyUSB host port, so keep Serial logs on UART or USB-Serial-JTAG.
static constexpr uint8_t USB_HOST_RHPORT = 0;
static constexpr uint16_t STRING_LANGID_EN_US = 0x0409;
static constexpr size_t CONFIG_DESC_BUFFER_SIZE = 256;

// Control-transfer buffers must be placed in memory suitable for USB DMA.
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t config_desc_buffer[CONFIG_DESC_BUFFER_SIZE];
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint16_t string_desc_buffer[64];

// Friendly names for the USB-IF class codes found in device descriptors.
static const char *className(uint8_t cls) {
  switch (cls) {
    case TUSB_CLASS_UNSPECIFIED:
      return "Unspecified / per-interface";
    case TUSB_CLASS_AUDIO:
      return "Audio";
    case TUSB_CLASS_CDC:
      return "CDC control";
    case TUSB_CLASS_HID:
      return "HID";
    case TUSB_CLASS_PHYSICAL:
      return "Physical";
    case TUSB_CLASS_IMAGE:
      return "Image / PTP";
    case TUSB_CLASS_PRINTER:
      return "Printer";
    case TUSB_CLASS_MSC:
      return "Mass Storage";
    case TUSB_CLASS_HUB:
      return "Hub";
    case TUSB_CLASS_CDC_DATA:
      return "CDC data";
    case TUSB_CLASS_SMART_CARD:
      return "Smart Card";
    case TUSB_CLASS_CONTENT_SECURITY:
      return "Content Security";
    case TUSB_CLASS_VIDEO:
      return "Video";
    case TUSB_CLASS_PERSONAL_HEALTHCARE:
      return "Personal Healthcare";
    case TUSB_CLASS_AUDIO_VIDEO:
      return "Audio/Video";
    case TUSB_CLASS_DIAGNOSTIC:
      return "Diagnostic";
    case TUSB_CLASS_WIRELESS_CONTROLLER:
      return "Wireless Controller";
    case TUSB_CLASS_MISC:
      return "Miscellaneous";
    case TUSB_CLASS_APPLICATION_SPECIFIC:
      return "Application Specific";
    case TUSB_CLASS_VENDOR_SPECIFIC:
      return "Vendor Specific";
    default:
      return "Reserved / Unknown";
  }
}

static void printHex8(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void printHex16(uint16_t value) {
  if (value < 0x1000) {
    Serial.print('0');
  }
  if (value < 0x0100) {
    Serial.print('0');
  }
  if (value < 0x0010) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void printClassLine(const char *prefix, uint8_t cls, uint8_t subcls, uint8_t proto) {
  Serial.print(prefix);
  Serial.print(" class=0x");
  printHex8(cls);
  Serial.print(" (");
  Serial.print(className(cls));
  Serial.print("), subclass=0x");
  printHex8(subcls);
  Serial.print(", protocol=0x");
  printHex8(proto);
  Serial.println();
}

static bool getStringDescriptor(uint8_t daddr, uint8_t index, char *out, size_t out_len) {
  if (out_len == 0) {
    return false;
  }
  out[0] = '\0';

  if (index == 0) {
    return false;
  }

  // 读取 USB 字符串描述符。USB 字符串是 UTF-16LE，这里只保留可打印 ASCII，
  // 方便直接在 Serial Monitor 里观察。
  memset(string_desc_buffer, 0, sizeof(string_desc_buffer));
  tusb_xfer_result_t result = tuh_descriptor_get_string_sync(
    daddr, index, STRING_LANGID_EN_US, string_desc_buffer, sizeof(string_desc_buffer)
  );
  if (result != XFER_RESULT_SUCCESS) {
    return false;
  }

  uint8_t byte_len = string_desc_buffer[0] & 0xFF;
  if (byte_len < 2) {
    return false;
  }

  size_t chars = (byte_len - 2) / 2;
  size_t copy_len = min(chars, out_len - 1);
  for (size_t i = 0; i < copy_len; ++i) {
    uint16_t c = string_desc_buffer[1 + i];
    out[i] = (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
  }
  out[copy_len] = '\0';
  return true;
}

static void printStringField(uint8_t daddr, const char *label, uint8_t index) {
  char text[64];
  Serial.print(label);
  Serial.print(": ");
  if (getStringDescriptor(daddr, index, text, sizeof(text))) {
    Serial.println(text);
  } else {
    Serial.println(index ? "(read failed)" : "(none)");
  }
}

static void printDeviceDescriptorFields(uint8_t daddr, tusb_desc_device_t const *device) {
  // 设备描述符是枚举时最早拿到的信息，包含 VID/PID、USB 版本、设备级 class 等。
  Serial.println();
  Serial.println("========== USB DEVICE ==========");
  Serial.print("Address: ");
  Serial.println(daddr);
  Serial.print("VID:PID: ");
  printHex16(device->idVendor);
  Serial.print(':');
  printHex16(device->idProduct);
  Serial.println();
  Serial.print("USB BCD: 0x");
  printHex16(device->bcdUSB);
  Serial.print(", Device BCD: 0x");
  printHex16(device->bcdDevice);
  Serial.println();
  Serial.print("EP0 max packet: ");
  Serial.println(device->bMaxPacketSize0);
  Serial.print("Configurations: ");
  Serial.println(device->bNumConfigurations);
  printClassLine("Device", device->bDeviceClass, device->bDeviceSubClass, device->bDeviceProtocol);

  if (device->bDeviceClass == TUSB_CLASS_UNSPECIFIED) {
    // 很多复合设备会把 device class 写成 0x00，表示真正类型在 interface 描述符中。
    Serial.println("Device class 0x00 is normal: check interface class for the real device type.");
  }
}

static bool getFirstInterfaceClass(uint8_t daddr, uint8_t *cls, uint8_t *subcls, uint8_t *proto) {
  // 如果设备级 class 是 0x00，就读取 configuration descriptor，
  // 找第一个 interface descriptor，用它判断“实际设备类型”。
  memset(config_desc_buffer, 0, sizeof(config_desc_buffer));

  tusb_xfer_result_t result = tuh_descriptor_get_configuration_sync(
    daddr, 0, config_desc_buffer, sizeof(tusb_desc_configuration_t)
  );
  if (result != XFER_RESULT_SUCCESS) {
    return false;
  }

  auto const *config = reinterpret_cast<tusb_desc_configuration_t const *>(config_desc_buffer);
  uint16_t read_len = min<uint16_t>(config->wTotalLength, CONFIG_DESC_BUFFER_SIZE);

  result = tuh_descriptor_get_configuration_sync(daddr, 0, config_desc_buffer, read_len);
  if (result != XFER_RESULT_SUCCESS) {
    return false;
  }

  uint16_t offset = config->bLength;
  while (offset + 2 <= read_len) {
    uint8_t const *desc = config_desc_buffer + offset;
    uint8_t len = desc[0];
    uint8_t type = desc[1];
    if (len < 2 || offset + len > read_len) {
      break;
    }

    if (type == TUSB_DESC_INTERFACE && len >= sizeof(tusb_desc_interface_t)) {
      auto const *itf = reinterpret_cast<tusb_desc_interface_t const *>(desc);
      *cls = itf->bInterfaceClass;
      *subcls = itf->bInterfaceSubClass;
      *proto = itf->bInterfaceProtocol;
      return true;
    }

    offset += len;
  }

  return false;
}

static void printMountedDeviceStrings(uint8_t daddr) {
  // mount 代表 TinyUSB 已经完成配置，此时可以再发控制传输读取字符串描述符。
  tusb_desc_device_t device;
  memset(&device, 0, sizeof(device));

  tusb_xfer_result_t result = tuh_descriptor_get_device_sync(daddr, &device, sizeof(device));
  if (result != XFER_RESULT_SUCCESS) {
    Serial.println("Failed to read device descriptor.");
    return;
  }

  if (device.bDeviceClass == TUSB_CLASS_UNSPECIFIED) {
    uint8_t cls = 0;
    uint8_t subcls = 0;
    uint8_t proto = 0;

    if (getFirstInterfaceClass(daddr, &cls, &subcls, &proto)) {
      printClassLine("Effective interface", cls, subcls, proto);
    } else {
      Serial.println("Effective interface class: (read failed)");
    }
  }

  printStringField(daddr, "Manufacturer", device.iManufacturer);
  printStringField(daddr, "Product", device.iProduct);
  printStringField(daddr, "Serial", device.iSerialNumber);
}

void tuh_enum_descriptor_device_cb(uint8_t daddr, tusb_desc_device_t const *desc_device) {
  // 枚举阶段回调：此时设备还不一定 mount 成功，但 device descriptor 已经可用。
  Serial.println();
  Serial.println("Device descriptor received during enumeration.");
  printDeviceDescriptorFields(daddr, desc_device);
}

void tuh_mount_cb(uint8_t daddr) {
  // mount 回调：设备已经枚举并配置成功。这里读取字符串描述符，并在需要时补读 interface class。
  Serial.println();
  Serial.println("Device mounted/configured. Reading string descriptors...");
  printMountedDeviceStrings(daddr);
  Serial.println("================================");
  Serial.println("Unplug this device or plug another one to compare descriptors.");
}

void tuh_umount_cb(uint8_t daddr) {
  // 拔出设备，或者枚举失败后，TinyUSB 会进入 unmount 回调。
  Serial.println();
  Serial.print("Device unmounted, address ");
  Serial.println(daddr);
}

void setup() {
  // 初始化串口日志。注意：USB OTG 口做 Host 时，不要同时拿它做 USB-CDC 日志口。
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("L030 ESP32-S3 TinyUSB Host Descriptor Viewer");
  Serial.println("Use the USB OTG D+/D- pins for the device-under-test.");
  Serial.println("For logs, use UART or USB-Serial-JTAG, not the same OTG port.");

#if !CONFIG_IDF_TARGET_ESP32S3
  Serial.println("WARNING: this sketch is intended for ESP32-S3.");
#endif

  // Arduino 默认 TinyUSB 封装通常把 OTG 控制器作为 device 启动。
  // 这个例程需要 Host，所以直接初始化底层 USB HAL，再用 tusb_init() 指定 Host 角色。
  esp_err_t hal_result = init_usb_hal(false);
  if (hal_result != ESP_OK) {
    Serial.print("USB HAL init failed: ");
    Serial.println(esp_err_to_name(hal_result));
    return;
  }

  tusb_rhport_init_t host_init = {};
  host_init.role = TUSB_ROLE_HOST;
  host_init.speed = TUSB_SPEED_FULL;

  if (!tusb_init(USB_HOST_RHPORT, &host_init)) {
    Serial.println("TinyUSB host init failed.");
    return;
  }

  Serial.println("TinyUSB host is ready. Plug in a USB device.");
}

void loop() {
  // TinyUSB Host 必须被持续轮询。插入、拔出、枚举、控制传输完成等事件都在这里推进。
  tuh_task();
  delay(1);
}


