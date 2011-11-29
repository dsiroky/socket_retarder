CC = gcc

CFLAGS = -g -O0 -Wall -fPIC -std=c++0x
LDFLAGS = -shared -ldl -lstdc++ -lpthread -fPIC

SRCS	= retarder.cc
OBJS	= ${SRCS:.cc=.o}

.SUFFIXES:
.SUFFIXES: .o .cc

OUTLIB = libsocket_retarder.so.1

all: $(OUTLIB)

.cc.o :
	$(CC) $(CFLAGS) -x c++ -c $<

$(OUTLIB): $(OBJS)
	gcc -Wl,-soname,$@ $(LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) $(OUTLIB)
