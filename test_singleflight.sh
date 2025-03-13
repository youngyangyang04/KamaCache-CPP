#! /bin/bash

# 执行 10 次，按理来说使用了 singleflight 之后，向 Charlie 所在的节点的请求次数会在 10 次以内
for i in {1..10}; do
    curl "http://localhost:9999/api?key=Charlie" &
done