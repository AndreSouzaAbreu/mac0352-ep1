#!/bin/sh
host=$(ifconfig wlan0 | grep inet | head -n 1 | awk '{print $2}') # internal ip
topic=$(shuf topics.txt | head -n1) # random topic
img=mosquitto # docker image name
msg=$(lorem -s $(( 1 + $RANDOM % 10)))
port=1883
qos=0

echo $topic >&2
docker run --detach $img mosquitto_pub -h $host -p $port -t $topic -q $qos -m "$msg"
