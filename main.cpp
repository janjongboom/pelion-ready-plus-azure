// ----------------------------------------------------------------------------
// Copyright 2016-2018 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------
#ifndef MBED_TEST_MODE

#include "mbed.h"
#include "mbed_mem_trace.h"
#include "simple-mbed-cloud-client.h"
#include "SimpleAzureIoTHub.h"
#include "FATFileSystem.h"
#include "LittleFileSystem.h"

// Default network interface object. Don't forget to change the WiFi SSID/password in mbed_app.json if you're using WiFi.
NetworkInterface *net = NetworkInterface::get_default_instance();

// Default block device available on the target board
BlockDevice *bd = BlockDevice::get_default_instance();

#if COMPONENT_SD || COMPONENT_NUSD
// Use FATFileSystem for SD card type blockdevices
FATFileSystem fs("fs", bd);
#else
// Use LittleFileSystem for non-SD block devices to enable wear leveling and other functions
LittleFileSystem fs("fs", bd);
#endif

#if USE_BUTTON == 1
InterruptIn button(BUTTON1);
#endif /* USE_BUTTON */

// Default LED to use for PUT/POST example
DigitalOut led(LED1);

// Declaring pointers for access to Pelion Device Management Client resources outside of main()
MbedCloudClientResource *led_res;
MbedCloudClientResource *post_res;

// An event queue is a very useful structure to debounce information between contexts (e.g. ISR and normal threads)
// This is great because things such as network operations are illegal in ISR, so updating a resource in a button's fall() function is not allowed
EventQueue *eventQueue = mbed_event_queue();

void azure_message_handler(MQTT::MessageData& md);

AzureIoT *azure;

/*
 * Callback function called when a message arrived from Azure
 */
void azure_message_handler(MQTT::MessageData& md)
{
    // Copy payload to the buffer.
    MQTT::Message &message = md.message;
    if (message.payloadlen > 127) message.payloadlen = 127;
    char buff[128] = { 0 };
    memcpy(buff, message.payload, message.payloadlen);

    printf("Message arrived from Azure: '%s'\n", buff);
}


void print_memory_info() {
    // allocate enough room for every thread's stack statistics
    int cnt = osThreadGetCount();
    mbed_stats_stack_t *stats = (mbed_stats_stack_t*) malloc(cnt * sizeof(mbed_stats_stack_t));

    cnt = mbed_stats_stack_get_each(stats, cnt);
    for (int i = 0; i < cnt; i++) {
        printf("Thread: 0x%lX, Stack size: %lu / %lu\r\n", stats[i].thread_id, stats[i].max_size, stats[i].reserved_size);
    }
    free(stats);

    // Grab the heap statistics
    mbed_stats_heap_t heap_stats;
    mbed_stats_heap_get(&heap_stats);
    printf("Heap size: %lu / %lu bytes (max: %lu bytes)\r\n", heap_stats.current_size, heap_stats.reserved_size, heap_stats.max_size);
}

/**
 * PUT handler - sets the value of the built-in LED
 * @param resource The resource that triggered the callback
 * @param newValue Updated value for the resource
 */
void put_callback(MbedCloudClientResource *resource, m2m::String newValue) {
    printf("PUT received. New value: %s\n", newValue.c_str());
    led = atoi(newValue.c_str());
}

/**
 * POST handler - prints the content of the payload
 * @param resource The resource that triggered the callback
 * @param buffer If a body was passed to the POST function, this contains the data.
 *               Note that the buffer is deallocated after leaving this function, so copy it if you need it longer.
 * @param size Size of the body
 */
void post_callback(MbedCloudClientResource *resource, const uint8_t *buffer, uint16_t size) {
    printf("POST received (length %u). Payload: ", size);
    for (size_t ix = 0; ix < size; ix++) {
        printf("%02x ", buffer[ix]);
    }
    printf("\n");
}

/**
 * Heap stats don't properly work with the split RAM bank on DISCO L475VG board
 * This is a quick way to test how much space there actually is on the heap.
 */
void fill_memory_up() {
    while (1) {
        static uint32_t allocated = 0;
        static uint32_t block = 10000;
        if (malloc(block) != NULL) {
            allocated += block;
            printf("Allocated %lu bytes\n", block);
        }
        block--;
        if (block == 0) {
            printf("Allocated: %lu bytes\n", allocated);
            break;
        }
    }
}

/**
 * Button handler that sends data to Azure IoT Hub
 * This function will be triggered either by a physical button press or by a ticker every 5 seconds (see below)
 */
void button_press() {
    if (!azure) return;

    static uint32_t count = 0;
    static uint32_t id = 0;

    MQTT::Message message;
    message.retained = false;
    message.dup = false;

    const size_t len = 128;
    char buf[len];
    snprintf(buf, len, "Button press: #%d from %s", count, DEVICE_ID);
    printf("Sending %s\n", buf);
    message.payload = (void*)buf;

    message.qos = MQTT::QOS0;
    message.id = id++;
    message.payloadlen = strlen(buf);

    nsapi_error_t rc = azure->publish(&message);
    if(rc != MQTT::SUCCESS) {
        printf("ERROR: rc from MQTT publish is %d\r\n", rc);
    }

    count++;

    print_memory_info();
}

/**
 * Notification callback handler
 * @param resource The resource that triggered the callback
 * @param status The delivery status of the notification
 */
void button_callback(MbedCloudClientResource *resource, const NoticationDeliveryStatus status) {
    printf("Button notification, status %s (%d)\n", MbedCloudClientResource::delivery_status_to_string(status), status);
}

/**
 * Registration callback handler
 * @param endpoint Information about the registered endpoint such as the name (so you can find it back in portal)
 */
void registered(const ConnectorClientEndpointInfo *endpoint) {
    printf("Registered to Pelion Device Management. Endpoint Name: %s\n", endpoint->internal_endpoint_name.c_str());

    // fill_memory_up();

    printf("Registering to Azure IoT Hub...\n");

    azure = new AzureIoT(eventQueue, net, &azure_message_handler);

    nsapi_error_t cr = azure->connect();
    if (cr != 0) {
        printf("Azure IoT Hub Client initialization failed (%d)\n", cr);
        print_memory_info();
        return;
    }
    printf("Azure IoT Hub is connected. Endpoint name: %s\n", DEVICE_ID);

    print_memory_info();
}

int main(void) {
    // mbed_trace_init();
    printf("\nStarting Simple Pelion Device Management Client example\n");

    print_memory_info();

    // SimpleMbedCloudClient handles registering over LwM2M to Pelion Device Management
    SimpleMbedCloudClient client(net, bd, &fs);

#if USE_BUTTON == 1
    // If the User button is pressed ons start, then format storage.
    if (button.read() == MBED_CONF_APP_BUTTON_PRESSED_STATE) {
        printf("User button is pushed on start. Formatting the storage...\n");
        int storage_status = StorageHelper::format(&fs, bd);
        if (storage_status != 0) {
            printf("ERROR: Failed to reformat the storage (%d).\n", storage_status);
        }
    } else {
        printf("You can hold the user button during boot to format the storage and change the device identity.\n");
    }
#endif /* USE_BUTTON */

    // Connect to the Internet (DHCP is expected to be on)
    printf("Connecting to the network using the default network interface...\n");

    nsapi_error_t net_status = NSAPI_ERROR_NO_CONNECTION;
    while ((net_status = net->connect()) != NSAPI_ERROR_OK) {
        printf("Unable to connect to network (%d). Retrying...\n", net_status);
    }

    printf("Connected to the network successfully. IP address: %s\n", net->get_ip_address());

    print_memory_info();

    // First we'll do Azure
    printf("Initializing Azure IoT Hub Client...\n");

    print_memory_info();

    mbed_mem_trace_set_callback(mbed_mem_trace_default_callback);

    printf("Initializing Pelion Device Management Client...\n");

    int client_status = client.init();
    if (client_status != 0) {
        printf("Pelion Client initialization failed (%d)\n", client_status);
        return -1;
    }

    print_memory_info();

    // Creating resources, which can be written or read from the cloud
    led_res = client.create_resource("3201/0/5853", "led_state");
    led_res->set_value(led.read());
    led_res->methods(M2MMethod::GET | M2MMethod::PUT);
    led_res->attach_put_callback(put_callback);

    post_res = client.create_resource("3300/0/5605", "execute_function");
    post_res->methods(M2MMethod::POST);
    post_res->attach_post_callback(post_callback);

    printf("Initialized Pelion Device Management Client. Registering...\n");

    // Callback that fires when registering is complete
    client.on_registered(&registered);

    // Register with Pelion DM
    client.register_and_connect();

#if USE_BUTTON == 1
    // The button fires on an interrupt context, but debounces it to the eventqueue, so it's safe to do network operations
    button.fall(eventQueue->event(&button_press));
    printf("Press the user button to increment the LwM2M resource value...\n");
#else
    // The timer fires on an interrupt context, but debounces it to the eventqueue, so it's safe to do network operations
    Ticker timer;
    timer.attach(eventQueue->event(&button_press), 5.0);
    printf("Simulating button press every 5 seconds...\n");
#endif /* USE_BUTTON */

#if MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_DISPATCH_FROM_APPLICATION == 1 // if running device management in single-thread mode
    // Run the scheduler on the main event queue
    eventQueue->call_every(1, callback(&client, &SimpleMbedCloudClient::process_events));
#endif

    // Process events forever
    eventQueue->dispatch_forever();
}

#endif /* MBED_TEST_MODE */
