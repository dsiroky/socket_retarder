#!/bin/bash

export LD_PRELOAD=./libsocket_retarder.so.1
export SOCKET_RETARDER_DEBUG=2
export SOCKET_RETARDER_DNS=0
export SOCKET_RETARDER_DISTRIBUTION=uniform
export SOCKET_RETARDER_UNIFORMDIST_A=1000
export SOCKET_RETARDER_UNIFORMDIST_B=2000

time wget http://www.google.com -O - > /dev/null
