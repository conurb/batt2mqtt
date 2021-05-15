#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <os/log.h>

#include <mosquitto.h>

// #include <mach/mach_port.h>
// #include <mach/mach_interface.h>
// #include <mach/mach_init.h>

// #include <IOKit/IOKitLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

// BEGIN: change to fit your needs
#define MQTT_HOST "pi"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "macbook"
#define MQTT_SUBSCRIBE_TOPIC_CMD "home/mac/cmd"         // subscribe
#define MQTT_PUBLISH_TOPIC_BAT "home/mac/battery/state" // publish
#define MQTT_PUBLISH_TOPIC_INFO "home/mac/info"         // publish
// END

#define MQTT_USER (getenv("MQTT_USER"))
#define MQTT_PASSWORD (getenv("MQTT_PASSWORD"))
#define BUFFER_SIZE 200
#define BATTERY_NOTIFICATION_FREQ_IN_SEC (5 * 60)
#define UNUSED(x) (void)(x)

/**
 *  LOGS
 **/
static os_log_t log_mqtt;
static os_log_t log_batt;

static void initialize_logs(void)
{
    // init logging
    // log only mqtt info/error
    log_mqtt = os_log_create("conurb.batt2mqtt", "mqtt");
    // log internals info/error
    log_batt = os_log_create("conurb.batt2mqtt", "batt");
}

/**
 * MOSQUITTO
 **/
typedef struct mosquitto *mqtt_t;

static inline void cleanup_mosquitto(mqtt_t mosq)
{
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}

static inline void log_mosquitto_publish_response(int rp, const char *msg)
{
    switch (rp)
    {
    case MOSQ_ERR_SUCCESS:
        os_log_info(log_batt, "message published: %{public}s", msg);
        break;
    case MOSQ_ERR_INVAL:
        os_log_error(log_mqtt, "can't publish: invalid input parameters");
        break;
    case MOSQ_ERR_NOMEM:
        os_log_error(log_mqtt, "can't publish: out of memory");
        break;
    case MOSQ_ERR_NO_CONN:
        os_log_error(log_mqtt, "can't publish: not connected to broker");
        break;
    case MOSQ_ERR_PROTOCOL:
        os_log_error(log_mqtt, "can't publish: protocol error");
        break;
    case MOSQ_ERR_PAYLOAD_SIZE:
        os_log_error(log_mqtt, "can't publish: payloadlen is too large");
        break;
    case MOSQ_ERR_MALFORMED_UTF8:
        os_log_error(log_mqtt, "can't publish: topic is not valid UTF-8");
        break;
    case MOSQ_ERR_QOS_NOT_SUPPORTED:
        os_log_error(log_mqtt, "can't publish: QoS is greater than that supported by the broker");
        break;
    case MOSQ_ERR_OVERSIZE_PACKET:
        os_log_error(log_mqtt, "can't publish: packet too large");
        break;
    default:
        os_log_error(log_batt, "can't publish message: %{public}s", msg);
        break;
    }
}

static inline void subscribe_to_topic(mqtt_t mosq, const char *topic)
{
    if (MOSQ_ERR_SUCCESS == mosquitto_subscribe(mosq, NULL, topic, 0))
        os_log_info(log_mqtt, "subscribed to topic \"%s\"", topic);
    else
        os_log_error(log_mqtt, "unable to subscribe to topic \"%s\"", topic);
}

static void message_callback(mqtt_t mosq, void *obj, const struct mosquitto_message *msg)
{
    UNUSED(mosq);
    UNUSED(obj);
    bool match = false;

    mosquitto_topic_matches_sub(MQTT_SUBSCRIBE_TOPIC_CMD, msg->topic, &match);

    if (match && msg->payloadlen)
    {
        if (!strcmp(msg->payload, "displaysleepnow"))
        {
            os_log_info(log_batt, "command received: displaysleepnow");
            pid_t pid = fork();
            if (!pid)
                execl("/usr/bin/pmset", "pmset", "displaysleepnow", NULL);
            else
                signal(SIGCHLD, SIG_IGN);
        }
        else if (!strcmp(msg->payload, "sleepnow"))
        {
            os_log_info(log_batt, "command received: sleepnow");
            pid_t pid = fork();
            if (!pid)
            {
                close(1);
                execl("/usr/bin/pmset", "pmset", "sleepnow", NULL);
            }
            else
            {
                signal(SIGCHLD, SIG_IGN);
            }
        }
    }
}

static void connect_callback(mqtt_t mosq, void *obj, int rc)
{
    UNUSED(obj);

    switch (rc)
    {
    case 0:
        os_log_info(log_mqtt, "successfully connected to broker");
        subscribe_to_topic(mosq, MQTT_SUBSCRIBE_TOPIC_CMD);
        break;
    case 1:
        os_log_error(log_mqtt, "connection refused: unacceptable protocol version");
        break;
    case 2:
        os_log_error(log_mqtt, "connection refused: identifier rejected");
        break;
    case 3:
        os_log_error(log_mqtt, "connection refused: broker unavailable");
        break;
    default:
        os_log_error(log_mqtt, "connection refused: unknown reason");
        break;
    }
}

static mqtt_t initialize_mosquitto(void)
{
    // init mosquitto library
    mosquitto_lib_init();

    // create mqtt client
    struct mosquitto *mosq = mosquitto_new(MQTT_CLIENT_ID, true, 0);
    if (!mosq)
    {
        mosquitto_lib_cleanup();
        os_log_error(log_mqtt, "mosquitto init: can't create mqtt client");
        return NULL;
    }

    // set mosq user and password
    mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASSWORD);

    // setup mosq callbacks
    mosquitto_connect_callback_set(mosq, connect_callback);
    mosquitto_message_callback_set(mosq, message_callback);

    // mosq connection
    int rc = mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60);
    if (MOSQ_ERR_SUCCESS != rc)
        os_log_error(log_mqtt, "mosquitto init: unable to connect to broker");

    // start mosquitto loop
    rc = mosquitto_loop_start(mosq);
    if (MOSQ_ERR_SUCCESS != rc)
    {
        cleanup_mosquitto(mosq);
        os_log_error(log_mqtt, "mosquitto init: unable to start mosquitto loop");
        return NULL;
    }

    return mosq;
}

/**
 *  POWER NOTIFICATIONS
 **/
typedef struct
{
    io_connect_t *root_port;
    mqtt_t mosq;
} power_args;

static void power_callback(void *arg, io_service_t service, natural_t msg_type, void *msg_arg)
{
    UNUSED(service);
    // cf https://opensource.apple.com/source/IOKitUser/IOKitUser-1445.31.1/pwr_mgt.subproj/IOPMLib.h.auto.html

    power_args *args = (power_args *)arg;

    switch (msg_type)
    {
    case kIOMessageCanSystemSleep:
        /**
         * kIOMessageCanSystemSleep indicates the system is pondering an idle sleep, but gives apps the
         * chance to veto that sleep attempt.
         * Caller must acknowledge kIOMessageCanSystemSleep by calling IOAllowPowerChange or IOCancelPowerChange.
         * Calling IOAllowPowerChange will not veto the sleep; any app that calls IOCancelPowerChange will veto
         * the idle sleep. A kIOMessageCanSystemSleep notification will be followed up to 30 seconds later by a
         * kIOMessageSystemWillSleep message or a kIOMessageSystemWillNotSleep message.
         **/
        IOAllowPowerChange(*args->root_port, (long)msg_arg);
        break;

    case kIOMessageSystemWillSleep:
    {
        /**
         * kIOMessageSystemWillSleep is delivered at the point the system is initiating a
         * non-abortable sleep.
         * Callers MUST acknowledge this event by calling IOAllowPowerChange.
         * If a caller does not acknowledge the sleep notification, the sleep will continue anyway after
         * a 30 second timeout (resulting in bad user experience).
         * Delivered before any hardware is powered off.
         **/
        const char *msg = "willsleep";
        int rp = mosquitto_publish(args->mosq, NULL, MQTT_PUBLISH_TOPIC_INFO, (int)strlen(msg), msg, 0, false);
        log_mosquitto_publish_response(rp, msg);
        IOAllowPowerChange(*args->root_port, (long)msg_arg);
        break;
    }

    case kIOMessageSystemHasPoweredOn:
    {
        const char *msg = "haspoweredon";
        int rp = mosquitto_publish(args->mosq, NULL, MQTT_PUBLISH_TOPIC_INFO, (int)strlen(msg), msg, 2, false);
        log_mosquitto_publish_response(rp, msg);
        break;
    }

    default:
        break;
    }
}

static void initialize_power_notifications(power_args *p_power_args)
{
    // Port used to communicate to the kernel
    static io_connect_t root_port;

    // notification port allocated by IORegisterForSystemPower
    IONotificationPortRef notification_port;

    // notifier object
    io_object_t notifier;

    // pass static root_port address to callback func
    p_power_args->root_port = &root_port;

    // register to receive system sleep notifications
    root_port = IORegisterForSystemPower(p_power_args, &notification_port, power_callback, &notifier);
    if (!root_port)
    {
        cleanup_mosquitto(p_power_args->mosq);
        os_log_error(log_batt, "IORegisterForSystemPower failed, exit program");
        exit(EXIT_FAILURE);
    }

    // add the notification port to the application runloop
    CFRunLoopAddSource(CFRunLoopGetCurrent(),
                       IONotificationPortGetRunLoopSource(notification_port),
                       kCFRunLoopDefaultMode);
}

/** 
 * BATTERY NOTIFICATIONS
 **/
static void battery_callback(CFRunLoopTimerRef timer, void *info)
{
    UNUSED(timer);

    mqtt_t mosq = (mqtt_t)info;
    char buffer[BUFFER_SIZE] = {0};
    // st: state (0=discharging / 1=charging); bp: baterry percent
    int32_t st = -1, bp = -1;

    CFTypeRef ps_info = NULL;       // power source info
    CFArrayRef ps_list = NULL;      // power source list
    CFTypeRef ps = NULL;            // power source
    CFDictionaryRef ps_desc = NULL; // power source description
    CFStringRef ps_type = NULL;     // power source type
    CFTypeRef ps_capacity = NULL;   // power source battery percent
    CFStringRef ps_state = NULL;    // power source state

    ps_info = IOPSCopyPowerSourcesInfo();
    if (!ps_info)
        goto end;

    ps_list = IOPSCopyPowerSourcesList(ps_info);
    if (!ps_list)
        goto end;

    for (int i = 0; i < CFArrayGetCount(ps_list); i++)
    {
        ps = CFArrayGetValueAtIndex(ps_list, i);
        if (!ps)
            goto end;

        ps_desc = IOPSGetPowerSourceDescription(ps_info, ps);
        if (!ps_desc)
            goto end;

        ps_type = CFDictionaryGetValue(ps_desc, CFSTR(kIOPSTypeKey));
        if (!ps_type)
            goto end;

        if (CFStringCompare(ps_type, CFSTR(kIOPSInternalBatteryType), 0) == kCFCompareEqualTo)
        {
            // battery percent
            ps_capacity = CFDictionaryGetValue(ps_desc, CFSTR(kIOPSCurrentCapacityKey));
            if (!ps_capacity)
                break;
            CFNumberGetValue((CFNumberRef)ps_capacity, kCFNumberSInt32Type, &bp);

            // state (0=discharging / 1=charging)
            ps_state = CFDictionaryGetValue(ps_desc, CFSTR(kIOPSPowerSourceStateKey));
            if (!ps_state)
                break;
            st = (CFStringCompare(ps_state, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) ? 1 : 0;

            break;
        }
    }

    if (bp != -1 && st != -1)
    {
        snprintf(buffer, BUFFER_SIZE, "{\"percentage\":%d, \"state\":%d}", bp, st);
        int rp = mosquitto_publish(mosq, NULL, MQTT_PUBLISH_TOPIC_BAT, (int)strlen(buffer), buffer, 0, false);
        log_mosquitto_publish_response(rp, buffer);
    }
    else
        os_log_error(log_batt, "informations not found about internal battery: %d/%d", bp, st);

end:
    if (ps_list)
        CFRelease(ps_list);
    if (ps_info)
        CFRelease(ps_info);
}

static void initialize_battery_notifications(mqtt_t mosq)
{
    CFRunLoopTimerRef timer;
    CFRunLoopTimerContext timer_context;
    memset(&timer_context, 0, sizeof(timer_context));
    timer_context.info = mosq;
    timer = CFRunLoopTimerCreate(NULL,
                                 CFAbsoluteTimeGetCurrent() + 1,
                                 BATTERY_NOTIFICATION_FREQ_IN_SEC,
                                 0,
                                 0,
                                 battery_callback,
                                 &timer_context);
    if (!timer)
    {
        cleanup_mosquitto(mosq);
        os_log_error(log_batt, "CFRunLoopTimerCreate failed, exit program");
        exit(EXIT_FAILURE);
    }
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
}

/**
 * SIGNAL
 **/
static void signal_handler(int sig)
{
    switch (sig)
    {
    case SIGTERM:
    case SIGINT:
        os_log_info(log_batt, "got %s - will stop", sig == SIGTERM ? "SIGTERM" : "SIGINT");
        CFRunLoopStop(CFRunLoopGetCurrent());
        break;
    }
}

int main(void)
{
    // logs
    initialize_logs();

    // signal
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // mqtt client
    mqtt_t mosq = initialize_mosquitto();
    if (!mosq)
        return EXIT_FAILURE;

    // power notifications
    power_args args = {.mosq = mosq};
    initialize_power_notifications(&args);

    // battery status notifications
    initialize_battery_notifications(mosq);

    // run loop
    CFRunLoopRun();

    // clean mosquitto
    cleanup_mosquitto(mosq);

    os_log_info(log_batt, "-- program ended --");

    return EXIT_SUCCESS;
}
