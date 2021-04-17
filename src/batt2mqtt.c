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

// BEGIN: change to fit your needs
#define MQTT_HOST "pi"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "macbook"
#define MQTT_SUBSCRIBE_TOPIC_CMD "home/mac/cmd"         // subscribe
#define MQTT_PUBLISH_TOPIC_BAT "home/mac/battery/state" // publish
// END

#define MQTT_USER (getenv("MQTT_USER"))
#define MQTT_PASSWORD (getenv("MQTT_PASSWORD"))
#define BUFFER_SIZE 200
#define THREAD_SLEEP_TIME (5 * 60)
#define UNUSED(x) (void)(x)

enum batteryState
{
    DISCHARGING,
    CHARGING,
    FINISHING_CHARGE,
    CHARGED,
    UNDEFINED
};

static int run = 1;
static os_log_t log_mqtt;
static os_log_t log_batt;

static void handle_signal(int s)
{
    UNUSED(s);
    run = 0;
}

static void subscribe_to_topic(struct mosquitto *mosq, const char *topic)
{
    if (MOSQ_ERR_SUCCESS == mosquitto_subscribe(mosq, NULL, topic, 0))
        os_log_info(log_batt, "subscribed to topic \"%s\"", topic);
    else
        os_log_error(log_batt, "unable to subscribe to topic \"%s\"", topic);
}

static void connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    UNUSED(mosq);
    UNUSED(obj);

    switch (rc)
    {
    case 0:
        os_log_info(log_mqtt, "successfully connected");
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

static void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    UNUSED(mosq);
    UNUSED(obj);
    bool match = 0;

    mosquitto_topic_matches_sub(MQTT_SUBSCRIBE_TOPIC_CMD, message->topic, &match);

    if (match && message->payloadlen)
    {
        if (!strcmp(message->payload, "displaysleepnow"))
        {
            os_log_info(log_batt, "command received: displaysleepnow");
            pid_t pid = fork();
            if (!pid)
                execl("/usr/bin/pmset", "pmset", "displaysleepnow", NULL);
            else
                signal(SIGCHLD, SIG_IGN);
        }
        else if (!strcmp(message->payload, "sleepnow"))
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

static void *get_battery_state(void *arg)
{
    struct mosquitto *mosq = (struct mosquitto *)arg;

    char *token, *cpy_buf, *tofree;

    char buffer[BUFFER_SIZE];

    int rp;
    int8_t bp = -1;
    enum batteryState st = UNDEFINED;

    while (1)
    {
        FILE *pipe = popen("pmset -g batt", "r");

        if (pipe)
        {
            while (fgets(buffer, BUFFER_SIZE, pipe))
            {
                tofree = cpy_buf = strdup(buffer);

                /** 
                 * parse "pmset -g batt" output
                 * wanted:
                 * - bp: int8_t (battery percentage)
                 * - st: enum batteryState (battery status)
                 **/
                while ((token = strtok_r(cpy_buf, ";\t ", &cpy_buf)))
                {
                    // battery percentage
                    if (bp == -1 && strchr(token, '%') != NULL)
                        bp = (int8_t)atoi(token);

                    // battery state
                    if (!strcmp(token, "discharging"))
                        st = DISCHARGING;
                    else if (!strcmp(token, "charging"))
                        st = CHARGING;
                    else if (!strcmp(token, "finishing"))
                        st = FINISHING_CHARGE;
                    else if (!strcmp(token, "charged"))
                        st = CHARGED;
                } // while (token = ../..)

                free(tofree);
            } // while (fgets(../..))
            pclose(pipe);

            // send MQTT message
            if (bp >= 0 && st != UNDEFINED && mosq)
            {
                snprintf(buffer, BUFFER_SIZE, "{\"percentage\":%d, \"state\":%d}", bp, st);
                rp = mosquitto_publish(mosq, NULL, MQTT_PUBLISH_TOPIC_BAT, (int)strlen(buffer), buffer, 0, false);
                switch (rp)
                {
                case MOSQ_ERR_SUCCESS:
                    os_log_info(log_batt, "message published (percentage: %d, state: %d)", bp, st);
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
                    os_log_error(log_mqtt, "can't publish: unknown error");
                    break;
                }
                        }
            else
            {
                os_log_error(log_batt, "can't make a valid message with percentage (%d) and state (%d)", bp, st);
            }

            // reset values to default
            bp = -1;
            st = UNDEFINED;
        }
        else
        {
            os_log_error(log_batt, "unable to execute command (popen error)");
        } // if (pipe)
        // go to sleep
        sleep(THREAD_SLEEP_TIME);
    } // while (1)

    return NULL;
}

int main(void)
{
    // thread in charge of publishing battery state to mqtt broker
    // publish every THREAD_SLEEP_TIME seconds
    pthread_t th_battery;

    // init logging
    // log only mqtt info/error
    log_mqtt = os_log_create("conurb.batt2mqtt", "mqtt");
    // log internals info/error
    log_batt = os_log_create("conurb.batt2mqtt", "batt");

    // mqtt client
    struct mosquitto *mosq;
    int rc = 0;

    // signals
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    mosquitto_lib_init();

    // create mqtt client
    mosq = mosquitto_new(MQTT_CLIENT_ID, true, 0);
    if (!mosq)
    {
        mosquitto_lib_cleanup();
        os_log_error(log_mqtt, "can't create mqtt client, exit program");
        return EXIT_FAILURE;
    }

    // set mosq user and password
    mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASSWORD);

    // set mosq callbacks
    mosquitto_connect_callback_set(mosq, connect_callback);
    mosquitto_message_callback_set(mosq, message_callback);

    // mosq connection
    mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60);

    // create and detach "battery thread"
    pthread_create(&th_battery, NULL, get_battery_state, mosq);
    pthread_detach(th_battery);

    while (run)
    {
        rc = mosquitto_loop(mosq, -1, 1);
        if (run && rc)
        {
            switch (rc)
            {
            case MOSQ_ERR_INVAL:
                os_log_error(log_mqtt, "input parameters are invalid");
                break;
            case MOSQ_ERR_NOMEM:
                os_log_error(log_mqtt, "an out of memory condition occurred");
                break;
            case MOSQ_ERR_NO_CONN:
                os_log_error(log_mqtt, "not connected to broker");
                break;
            case MOSQ_ERR_CONN_LOST:
                os_log_error(log_mqtt, "connection to the broker was lost");
                break;
            case MOSQ_ERR_PROTOCOL:
                os_log_error(log_mqtt, "protocol error communicating with the broker");
                break;
            case MOSQ_ERR_ERRNO:
            {
                char buffer[BUFFER_SIZE];
                int error_num;
                if ((error_num = strerror_r(errno, buffer, BUFFER_SIZE)) == 0)
                    os_log_error(log_mqtt, "unable to connect to the broker: %s", buffer);
                else
                    os_log_error(log_mqtt, "unknown error (forgot credentials?)");
                break;
            }
            default:
                break;
            }
            sleep(10);

            mosquitto_reconnect(mosq);
        } // if (run && rc)
    }     // while(run)

    mosquitto_destroy(mosq);

    mosquitto_lib_cleanup();

    os_log_info(log_batt, "program ended");

    return EXIT_SUCCESS;
}