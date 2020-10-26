#! /bin/sh
pkill -INT epoll_clt
sleep 1
pkill -INT epoll_svc
