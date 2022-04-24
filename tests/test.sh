#!/bin/sh
n=${1:-10}
cd $(dirname $0)

while [[ "$n" -gt 0 ]]; do
  ./run_mosquitto_sub.sh
  let n=n-1
done

while [[ true ]]; do
  ./run_mosquitto_pub.sh
done
