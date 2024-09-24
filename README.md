# AltaNode32
A physical control unit for the Avigilon Alta API powered by ESP32.

<img src="https://jwise.dev/content/images/size/w1000/2024/06/altanode.png" width="500" >

### Project Details
This project implements an ESP32 (LUANODE32) microcontroller that provides control of Avigilon Alta entries via API using physical buttons.

### Features:
- Connects to WiFi using credentials stored on the SD card.
- Provides a web interface for configuration of Avigilon Alta API details and WiFi.
- Implements secure access to configuration pages with username and password authentication.
- Saves configuration data (API URL and button entries) to the SD card.
- Encrypts SD card data & stores key in EEPROM via AES
- Provides Over-the-Air (OTA) updates for easy firmware updates.
- Controls entries via button presses that make use of the Avigilon Alta API.

### Hardware Components:
- ESP32 Microcontroller (LUANODE32 variant recommended)
- SD Card Module & SD Card
- Buttons
- Enclosure

### Software Libraries:
- Arduino core for ESP32
- WiFi library for ESP32
- AsyncTCP library
- ESPAsyncWebServer library
- ArduinoJson library
- AsyncElegantOTA library
- mbedtls library
- EEPROM library

### Resources
- [Avigilon Alta API docs](https://openpath.readme.io/)
- [AltaNode Project Blog Post](https://jwise.dev/aviligon-alta-api/)
- [AltaNode Project](https://github.com/Joshua-Wise/AltaNode)
- [AltaNode32POE Project](https://github.com/Joshua-Wise/AltaNode32POE)

### Amazon Hardware List
- [Enclosure](https://www.amazon.com/uxcell-Button-Control-Station-Aperture/dp/B07WKJM1NJ)
- [Buttons](https://www.amazon.com/Waterproof-Momentary-Mushroom-Terminal-EJ22-241A/dp/B098FGVVFZ)
- [Esp32](https://www.amazon.com/DORHEA-ESP-WROOM-32-Development-Interface-Breakout/dp/B0CP5FN6N4?th=1)
- [SD Card Reader](https://www.amazon.com/dp/B0B7WZQVHS)
- [Panel Mount Micro-USB (optional)](https://www.amazon.com/QIANRENON-Threaded-Charging-Connector-Dashboard/dp/B0D17N33TX)
- [Breakout Board (optional)](https://www.amazon.com/dp/B0B85C98FX)
- [JST Connectors (optional)](https://www.amazon.com/dp/B076HLQ4FX)
- [6-pin Dupont (optional)](https://www.amazon.com/dp/B0B8YWWXS3)
