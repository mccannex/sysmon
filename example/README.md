# SysMon Example

This example demonstrates how to use the SysMon component to monitor CPU usage, stack usage, and memory statistics on an ESP32 device through a web-based dashboard.

## Hardware Required

* A development board with Espressif SoC (ESP32, ESP32-S2, ESP32-S3, etc.)
* A USB cable for power supply and programming
* WiFi network access

## Configure the Example

Before building, you must provide your WiFi credentials:

Open the main example source file and locate the `#define WIFI_SSID` and `#define WIFI_PASSWORD` lines near the top. Uncomment these lines and enter your own WiFi network name and password:

```c
#define WIFI_SSID     "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
```

## Build and Flash

1. Set the target chip: `idf.py set-target esp32` (or esp32s2, esp32s3, etc.)
2. Build and flash: `idf.py -p PORT build flash monitor`

(To exit the serial monitor, type `Ctrl-]`.)

## Accessing the Dashboard

After the device boots and connects to WiFi:

1. Check the serial monitor for the assigned IP address
2. Open a web browser and navigate to `http://<device-ip>:8080/`
3. The SysMon dashboard will display real-time CPU, stack, and memory statistics

## Example Features

This example creates several demo tasks to showcase SysMon's monitoring capabilities:

- **Sine wave CPU load generator** - Generates variable CPU load for testing
- **Task lifecycle manager** - Creates and deletes tasks dynamically
- **RGB LED controller** - Optional LED strip control (requires hardware)

All tasks are registered with SysMon for stack usage tracking, demonstrating how to monitor your own application tasks.

## Configuration

You can configure SysMon via `idf.py menuconfig`:

- Navigate to **Component config → SysMon Configuration**
- Adjust HTTP server port (default: 8080)
- Configure CPU sampling interval
- Set history buffer size

See the main [README.md](../README.md) for more details.

