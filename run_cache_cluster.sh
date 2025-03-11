#!/bin/bash
trap "rm kcache;kill 0" EXIT

cd build && cmake --build . --target kcache

./kcache --port=8001 &
./kcache --port=8002 &
./kcache --port=8003 --api &

sleep 1

echo ">>> start test"

wait