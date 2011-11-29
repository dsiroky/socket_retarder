#!/bin/bash

export LD_PRELOAD=./libsocket_retarder.so.1
export SOCKET_RETARDER_DEBUG=2
export SOCKET_RETARDER_DNS=0
export SOCKET_RETARDER_NORMALDIST_MEAN=500
export SOCKET_RETARDER_NORMALDIST_VARIANCE=500

if [ "`whoami`" != "root" ]; then
  echo "(ERROR) ping needs to be retarded under the user root so run me like e.g."
  echo "sudo su -c ./runner_example_ping.sh"
  exit 1
fi
ping google.com
