from pathlib import Path
from typing import Any

_import_fn: Any = globals().get("Import")
if _import_fn is None:
    raise RuntimeError("This script must be run by PlatformIO/SCons")
_import_fn("env")
env: Any = globals()["env"]


def patch_pubsubclient() -> None:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    pio_env = env.subst("$PIOENV")
    target = project_dir / ".pio" / "libdeps" / pio_env / "PubSubClient" / "src" / "PubSubClient.cpp"

    if not target.exists():
        print(f"[patch_pubsubclient] skip (not found): {target}")
        return

    content = target.read_text(encoding="utf-8")
    marker = "PubSubClient malformed publish guard"
    if marker in content:
        print("[patch_pubsubclient] already patched")
        return

    old = """                        uint16_t tl = (this->buffer[llen+1]<<8)+this->buffer[llen+2]; /* topic length in bytes */
                        memmove(this->buffer+llen+2,this->buffer+llen+3,tl); /* move topic inside buffer 1 byte to front */
                        this->buffer[llen+2+tl] = 0; /* end the topic as a 'C' string with \\x00 */
                        char *topic = (char*) this->buffer+llen+2;
                        // msgId only present for QOS>0
                        if ((this->buffer[0]&0x06) == MQTTQOS1) {
                            msgId = (this->buffer[llen+3+tl]<<8)+this->buffer[llen+3+tl+1];
                            payload = this->buffer+llen+3+tl+2;
                            callback(topic,payload,len-llen-3-tl-2);

                            this->buffer[0] = MQTTPUBACK;
                            this->buffer[1] = 2;
                            this->buffer[2] = (msgId >> 8);
                            this->buffer[3] = (msgId & 0xFF);
                            _client->write(this->buffer,4);
                            lastOutActivity = t;

                        } else {
                            payload = this->buffer+llen+3+tl;
                            callback(topic,payload,len-llen-3-tl);
                        }
"""

    new = """                        uint16_t tl = (this->buffer[llen+1]<<8)+this->buffer[llen+2]; /* topic length in bytes */
                        // PubSubClient malformed publish guard (ESP12 stability patch)
                        uint16_t topicStart = llen + 2;
                        uint16_t topicDataStart = llen + 3;
                        uint16_t payloadStart = topicDataStart + tl;
                        if (topicDataStart > len || payloadStart > len || payloadStart > this->bufferSize) {
                            _state = MQTT_DISCONNECTED;
                            _client->stop();
                            return false;
                        }

                        memmove(this->buffer + topicStart, this->buffer + topicDataStart, tl); /* move topic inside buffer 1 byte to front */
                        if (topicStart + tl >= this->bufferSize) {
                            _state = MQTT_DISCONNECTED;
                            _client->stop();
                            return false;
                        }
                        this->buffer[topicStart + tl] = 0; /* end the topic as a 'C' string with \\x00 */

                        char *topic = (char*)this->buffer + topicStart;
                        // msgId only present for QOS>0
                        if ((this->buffer[0]&0x06) == MQTTQOS1) {
                            if (payloadStart + 2 > len) {
                                _state = MQTT_DISCONNECTED;
                                _client->stop();
                                return false;
                            }
                            msgId = (this->buffer[payloadStart]<<8)+this->buffer[payloadStart+1];
                            payload = this->buffer + payloadStart + 2;
                            callback(topic,payload,len-payloadStart-2);

                            this->buffer[0] = MQTTPUBACK;
                            this->buffer[1] = 2;
                            this->buffer[2] = (msgId >> 8);
                            this->buffer[3] = (msgId & 0xFF);
                            _client->write(this->buffer,4);
                            lastOutActivity = t;

                        } else {
                            payload = this->buffer + payloadStart;
                            callback(topic,payload,len-payloadStart);
                        }
"""

    if old not in content:
        raise RuntimeError("[patch_pubsubclient] target block not found; upstream changed")

    target.write_text(content.replace(old, new), encoding="utf-8")
    print(f"[patch_pubsubclient] patched {target}")


patch_pubsubclient()
