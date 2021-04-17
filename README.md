# batt2mqtt

Tired of plugging/unplugging my mac to preserve the battery. I want to use my home automation system. batt2mqtt simply sends the battery status to an MQTT server. With Node-Red, my home automation system can automatically turn the plug on/off when it's time.

## Build dependencies
Need `cmake` and `pkg-config`:
```bash
$ brew install pkg-config
$ brew install cmake
```

## Executable dependency

Install `mosquitto`:
```bash
$ brew install mosquitto
```

## Configure

- Modify `batt2mqtt.plist` to fit your needs (replace the `XXXX`). You can safely remove the section `EnvironmentVariables` if you don't use a user/password to connect to your broker (MQTT server)
- Modify what you want in `src/batt2mqtt.c` between `BEGIN` and `END` tags to set informations about your broker and topics.

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
$ log show --predicate 'subsystem contains "conurb.batt2mqtt"' --last 2h --info --debug --style compact
```

## Node-Red (simple function example)

```javascript
/**
 * Battery State:
 * 0 = DISCHARGING
 * 1 = CHARGING
 * 2 = FINISHING_CHARGE
 * 3 = CHARGED
 **/

const bat = msg.payload;

if (bat.percentage < 11 && bat.state === 0)
{
    msg.payload = "ON";
    return msg;
}
else if (bat.state === 3) 
{
    msg.payload = "OFF";
    return msg;
}
```

## Send commands

You can send 2 commands to your mac (cf topic `MQTT_SUBSCRIBE_TOPIC_CMD` in `src/batt2mqtt.c`) :
- `displaysleepnow` (causes display to go to sleep immediately)
- `sleepnow` (causes an immediate system sleep)