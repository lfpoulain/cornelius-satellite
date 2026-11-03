# Optional PAJ7620 gestures

Disabled by default. Enable CONFIG_SATELLITE_GESTURES (or use sdkconfig.gestures.defaults) only for a PAJ7620 on the existing SDA11/SCL10 I2C bus, address 0x73. An absent sensor is nonfatal: audio continues and gesture capability is not advertised.

Protocol stays at version 1. The hello adds gestures:true only after sensor initialization, and micToggle:true for explicit boolean mute control. Gesture events are additive and ignored by an older Core. A new Core requires the advertised capability before accepting gestures or requesting microphone toggle. Existing firmware remains usable.

Seven gestures: up, down, left, right, forward, backward, wave. Circles are ignored. Horizontal movements are grouped before emitting a direction or wave. Commands are disabled by default in Core and configurable per satellite. Home Assistant lights must be explicitly selected and authorized; physical gestures do not create a conversation or require a camera. Voice authorization remains unchanged.

Volume feedback briefly displays a cyan ring meter. Microphone state is persisted using the existing NVS setting; the physical button remains available.

Upgrade Core or firmware independently. This change does not require the separate OTA PR. It adds no camera support, private infrastructure addresses, OTA keys or deployment configuration.
