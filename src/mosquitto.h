#ifndef _MOSQUITTO_H
#define _MOSQUITTO_H

/* CONTROL PACKET TYPE */
#define MQTT_PACKET_TYPE_CONNECT 1
#define MQTT_PACKET_TYPE_CONNACK 2
#define MQTT_PACKET_TYPE_PUBLISH 3
#define MQTT_PACKET_TYPE_PUBACK 4
#define MQTT_PACKET_TYPE_PUBREC 5
#define MQTT_PACKET_TYPE_PUBREL 6
#define MQTT_PACKET_TYPE_PUBCOMP 7
#define MQTT_PACKET_TYPE_SUBSCRIBE 8
#define MQTT_PACKET_TYPE_SUBACK 9
#define MQTT_PACKET_TYPE_UNSUBSCRIBE 10
#define MQTT_PACKET_TYPE_UNSUBACK 11
#define MQTT_PACKET_TYPE_PINGREQ 12
#define MQTT_PACKET_TYPE_PINGRESP 13
#define MQTT_PACKET_TYPE_DISCONNECT 14

#define MQTT_TOPIC_MAXLENGTH 65536
#define MQTT_MESSAGE_MAXLENGTH 256000
#define MQTT_PACKET_PUBLISH_MAXLENGTH 4 + MQTT_TOPIC_MAXLENGTH + MQTT_MESSAGE_MAXLENGTH

#define MQTT_GRANTED_QOS 0

const unsigned char MQTT_PROTOCOL_NAME[] = { 0x00, 0x04, 'M', 'Q', 'T', 'T' };
const unsigned char MQTT_PACKET_DISCONNET[] = { 0xe0, 0x00 };
const unsigned char MQTT_PACKET_CONNACK[] = { 0x20, 0x02, 0x00, 0x00 };
const unsigned char MQTT_PACKET_PINGRESP[] = { 0xd0, 0x00 };

#endif
