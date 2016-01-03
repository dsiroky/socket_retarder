License
=======
See LICENSE.txt.

What is it good for
===================
You can artificially slow down socket traffic (TCP, UDP) and drop/damage UDP
datagrams for almost any program without any modification of that program. Good
for testing purposes.

Usage
=====
#. Run `make` in the socket retarder directory.
#. Run your program::

    $ LD_PRELOAD=/path_to_the_socket_retarder/libsocket_retarder.so program_to_slow_down

  or::

    $ export LD_PRELOAD=/path_to_the_socket_retarder/libsocket_retarder.so
    $ program_to_slow_down

For Mac OS X
------------
::

    $ DYLD_INSERT_LIBRARIES=/path_to_the_socket_retarder/libsocket_retarder.so DYLD_FORCE_FLAT_NAMESPACE=y program_to_slow_down


Configuration
=============
by environment variables

``SOCKET_RETARDER_DEBUG=0``
  - debug level - 0=none, 1=verbose, 2=more verbose

``SOCKET_RETARDER_DNS=0|1``
  - retard communication on port 53 (default: 0)

``SOCKET_RETARDER_DISTRIBUTION=normal|uniform``
  - default: "normal"

``SOCKET_RETARDER_NORMALDIST_MEAN``
  - default: 1000 [ms, integer]

``SOCKET_RETARDER_NORMALDIST_VARIANCE``
  - default: 500 [ms, integer]

``SOCKET_RETARDER_UNIFORMDIST_A``
  - default: 500

``SOCKET_RETARDER_UNIFORMDIST_B``
  - default: 1500

``SOCKET_RETARDER_UDP_DROP_PROBABILITY``
  - probability of UDP datagram to be dropped, 0.0..1.0, default: 0.0

``SOCKET_RETARDER_UDP_DAMAGE_PROBABILITY``
  - probability of UDP datagram to be damaged, 0.0..1.0, default: 0.0

``SOCKET_RETARDER_UDP_DUPLICATE_PROBABILITY``
  - probability of UDP datagram to be duplicated, 0.0..1.0, default: 0.0

see ``./runner_example_*.sh``

Limitations
===========
Slows down only send() (TCP/IPv4, only connect() side), sendto() (UDP/IPv4), sendmsg().
Some programs need to be run under the user root like "ping". They have a setuid flag
and there is a problem with an environment/LD_PRELOAD.

Retarder for TCP connections creates its own proxy on ports 20000-20500. So if
you need to use those ports you have to recompile the retarder with different
contants PROXY_PORT_START and PROXY_PORT_STOP.

And many other limitations :-)
