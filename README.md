# Pelion Device Ready example + Azure IoT Hub

This is an example application that uses both Pelion Device Management (for device management) and Azure IoT Hub (for data) from the same Mbed OS application.

For more information and configuration, see:

* [pelion-ready-example](https://github.com/ARMmbed/pelion-ready-example).
* [Mbed-to-Azure-IoT-Hub](https://github.com/coisme/Mbed-to-Azure-IoT-Hub/).

This project just combines both in a single application.

## DISCO-L475VG-IOT01A1

This application contains a bunch of optimalizations specifically for the DISCO-L475VG-IOT01A1 board. Because this board has two RAM banks and Mbed OS does not support a split heap we statically allocate as much as possible until the smallest bank is full. This gives us the biggest heap possible, which is required because we have TLS connections open to both Pelion and Azure at the same time. That requires a lot of memory.

Note that on this board you can only use symmetric keys for Azure IoT Hub, otherwise this won't fit.

There are a number of patches included in this example project which are not mainlined at the time of writing:

1. Statically allocate the stack for the WiFi driver: [wifi-ism43362](https://github.com/ARMmbed/wifi-ism43362/pull/46).
1. Allocate a number of large buffers statically in Pelion Client: [mbed-cloud-client](https://github.com/janjongboom/mbed-cloud-client-1/tree/2.2.1-statically).
1. Disable EST functions in Pelion Client: [simple-mbed-cloud-client](https://github.com/janjongboom/simple-mbed-cloud-client-1/tree/low-mem-disco).
1. Disable high prio queue and put crash log info in RAM2: [mbed-os](https://github.com/janjongboom/mbed-os/tree/low-mem-pelion).

In addition there are a number of optimizations specific to this board regarding thread sizes, and disabling of certain features in [mbed_app.json](mbed_app.json) in this repository. You can use the same optimizations if you want to run other things alongside Pelion Client (e.g. [uTensor](http://utensor.ai)) on this board.

**Memory usage**

With both Pelion Client and Azure IoT Hub you'll have ~2.5K static RAM and ~10K heap space left. If you want to allocate anything, make sure to do it after Azure is initialized. RAM usage is at it's peak during connection, but about 7K is free'd when the connection is established.

With only Pelion Client running you'll have ~2.5K static RAM and ~44K heap space left. You can move e.g. the WiFi stack from static to dynamic RAM if you need more static RAM.
