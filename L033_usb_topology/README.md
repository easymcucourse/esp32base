# L033 USB Topology

ESP-IDF USB Host diagnostic project for parsing complete USB descriptors from a
USB audio device.

It prints:

- Device address, parent port, speed, VID/PID, class, and string descriptors
- Cached active configuration descriptor from ESP-IDF
- Raw configuration descriptor fetched with a large control transfer
- Interface alternate settings
- Endpoint transfer type, isochronous sync/usage, MPS, and interval
- UAC AudioControl and AudioStreaming class-specific descriptors

The descriptor control-transfer size is raised in `sdkconfig.defaults` to make
large USB audio topology descriptors visible:

```text
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=4096
```
