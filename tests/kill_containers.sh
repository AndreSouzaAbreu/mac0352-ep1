#!/bin/sh
tmp_file=$(mktemp)
docker ps --all | grep mosquitto_ | awk '{ print $1 }' > $tmp_file
lines=$(wc -l $tmp_file | awk '{ print $1 }')
if [[ $lines -eq 0 ]]; then
  exit 0
fi
xargs docker stop < $tmp_file
xargs docker rm < $tmp_file
