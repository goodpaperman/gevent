#! /bin/bash
# /bin/sh -> /bin/dash, do not recognize our for loop

i=0
while [ $i -lt 10 ]; 
do
    ./epoll_clt 1025 < demo &
    echo "start "
    i=$(($i+1))
done
