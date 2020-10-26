#! /bin/bash
# /bin/sh -> /bin/dash, do not recognize our for loop

for((i=0;i<10;i=i+1))
do
    ./epoll_clt 1025 < demo &
    echo "start "
done
