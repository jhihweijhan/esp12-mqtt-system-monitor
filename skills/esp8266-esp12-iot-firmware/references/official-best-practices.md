# Official Best Practices: ESP8266 + IoT

This reference is intentionally short and source-first. Use it to justify engineering decisions with primary documentation.

## 1) ESP8266 Runtime Reliability

- Keep control in `loop()` and avoid blocking work in callbacks.
Source: ESP8266 Arduino Core reference  
https://arduino-esp8266.readthedocs.io/en/3.1.1/reference.html

- On ESP8266, software watchdog pressure appears when execution does not return frequently enough; avoid long blocking loops and ensure cooperative yielding.
Source: ESP8266 crash FAQ  
https://arduino-esp8266.readthedocs.io/en/latest/faq/a02-my-esp-crashes.html

- Deep-sleep wake paths can require explicit RF wake handling in specific patterns.
Source: ESP8266 crash FAQ deep-sleep section  
https://arduino-esp8266.readthedocs.io/en/latest/faq/a02-my-esp-crashes.html

## 2) Hardware Safety for ESP-12 Designs

- ESP8266 IO is 3.3V domain; do not assume 5V tolerance.
- Boot strap pins must hold valid levels at reset, especially GPIO15/GPIO0/GPIO2 combinations.
Source: Espressif ESP8266 resources (hardware design guidance)  
https://www.espressif.com/en/products/socs/esp8266/resources

## 3) Memory, Flash, and Filesystem Constraints

- Align flash map, sketch size, and filesystem size; verify with actual board settings.
Source: ESP8266 filesystem docs  
https://arduino-esp8266.readthedocs.io/en/3.1.1/filesystem.html

- BearSSL has explicit RAM/stack implications; TLS setup can fail or destabilize runtime if memory budget is not planned.
Source: BearSSL client secure class docs  
https://arduino-esp8266.readthedocs.io/en/3.1.1/esp8266wifi/bearssl-client-secure-class.html

## 4) OTA Security

- Start with OTA authentication for development and move to signed binaries for production integrity.
Source: ESP8266 OTA docs (password + signed updates sections)  
https://arduino-esp8266.readthedocs.io/en/3.1.1/ota_updates/readme.html

## 5) MQTT and IoT Security Baselines

- MQTT v5 security section strongly recommends TLS to protect data and credentials.
Source: OASIS MQTT Version 5.0 specification  
https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.pdf

- IoT device cybersecurity capabilities should cover identity, configuration, data protection, logical access, software update, and cybersecurity state awareness.
Source: NISTIR 8259A (IoT Device Cybersecurity Capability Core Baseline)  
https://nvlpubs.nist.gov/nistpubs/ir/2020/NIST.IR.8259A.pdf

## 6) Project-Level Engineering Implications

Apply the following defaults unless constraints force exceptions:

1. Prefer non-blocking state machines over long retry loops.
2. Keep high-frequency loops WDT-safe with frequent cooperative yielding.
3. Budget TLS + JSON + display buffers together; measure heap trend under real traffic.
4. Enforce authenticated and integrity-protected update + control channels.
5. Treat any secret exposure in logs, firmware image, or repo as a security defect.
