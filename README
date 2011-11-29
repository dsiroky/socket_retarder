License
=======
see LICENSE.txt

What is it good for
===================
You can artificially slow down socket traffic (TCP, UDP) of almost any program
without any modification. Good for testing purposes.

Usage
=====
LD_PRELOAD=./libsocket_retarder.so.1 program_to_slow_down

Configuration
=============
by environment variables

SOCKET_RETARDER_DEBUG=0
  - debug level - 0=none, 1=verbose, 2=more verbose

SOCKET_RETARDER_DNS=0
SOCKET_RETARDER_DNS=1
  - retard communication on port 53 (implicit 0)

SOCKET_RETARDER_DISTRIBUTION=normal
SOCKET_RETARDER_DISTRIBUTION=uniform
  - implicit "normal"

SOCKET_RETARDER_NORMALDIST_MEAN
  - implicit 1000 [ms, integer]

SOCKET_RETARDER_NORMALDIST_VARIANCE
  - implicit 500 [ms, integer]

SOCKET_RETARDER_UNIFORMDIST_A
  - implicit 500

SOCKET_RETARDER_UNIFORMDIST_B
  - implicit 1500

see ./runner_example_*.sh

Limitations
===========
Slows down only send() (TCP/IPv4), sendto() (UDP/IPv4), sendmsg().
Some programs need to be run under the user root like "ping". They has setuid flag
and there is a problem with environment/LD_PRELOAD.

Retarder for TCP connections creates its own proxy on ports 20000-20500. So if
you need to use those ports you have to to recompile retarder with different
contants PROXY_PORT_START and PROXY_PORT_STOP.

And many other limitations :-)

