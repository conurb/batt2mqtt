# batt2mqtt

Tired of plugging/unplugging my mac to preserve the battery. I wanted to use my home automation system. batt2mqtt simply sends the battery status to an MQTT server. With Node-Red, my home automation system can automatically turn the plug on/off when it's time.

According to [Battery University](https://batteryuniversity.com/learn/article/how_to_charge_when_to_charge_table), the battery lasts longest when operating between 30 and 80 percent. With something very simple like [Node-RED](https://nodered.org), you can adjust the limits as you want according to the information received on the topic `MQTT_PUBLISH_TOPIC_BAT`. You can also turn off the power when the mac goes to sleep and turn it back on when it has woken up (`MQTT_PUBLISH_TOPIC_INFO`).

## Build dependencies

Need `cmake` and `pkg-config`:
```bash
$ brew install pkg-config
$ brew install cmake
```

## Executable dependency

depends on:
- `CoreFoundation` and `IOKit` (XCode)
- `mosquitto`

```bash
$ brew install mosquitto
```

## Configure

- Modify [`batt2mqtt.plist`](https://github.com/conurb/batt2mqtt/blob/main/batt2mqtt.plist) to fit your needs (replace the `XXXX`). You can safely remove the section `EnvironmentVariables` if you don't use a user/password to connect to your broker (MQTT server)
- Modify what you want in [`src/batt2mqtt.c`](https://github.com/conurb/batt2mqtt/blob/main/src/batt2mqtt.c) between `BEGIN` and `END` tags to set informations about your broker and topics.

### MQTT topics

#### topic: `MQTT_PUBLISH_TOPIC_INFO`

Messages received on this topic are:
- `willsleep` when the laptop will go to sleep
- `haspoweredon` when the laptop woke up

Message format: `string`

#### topic: `MQTT_PUBLISH_TOPIC_BAT`

Message received on this topic is for example:
- `{"percentage":51, "state":0}`

where `percentage` is the battery level and `state` is the power source status (`0` = discharging (on Battery), `1` = charging (on Power AC))

Message format: `JSON`

#### topic: `MQTT_SUBSCRIBE_TOPIC_CMD`

You can send 2 commands to your mac (cf topic `MQTT_SUBSCRIBE_TOPIC_CMD` in `src/batt2mqtt.c`) :
- `displaysleepnow` (causes display to go to sleep immediately)
- `sleepnow` (causes an immediate system sleep)

Message format: `string` 

## Build & Install

```bash
$ mkdir build
$ cd build
$ cmake ..
$ make
$ make install
```

This will install `batt2mqtt` as an agent.

## Reboot your mac

## Show me the logs

If you want to see the logs, use `log`.  
Eg (like `tail -f`):
```bash
$ log stream --predicate 'subsystem contains "conurb.batt2mqtt"' --info --debug --style compact
```
or, for example, show the logs from the past 2 hours:
```bash
$ log show --predicate 'subsystem contains "conurb.batt2mqtt"' --info --debug --style compact --last 2h
```
