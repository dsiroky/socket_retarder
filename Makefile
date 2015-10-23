CXX = g++

CXXFLAGS = -g -O0 -Wall -fPIC -std=c++0x
LDFLAGS = -shared -ldl -lpthread -fPIC

SRCS	= retarder.cc
OBJS	= ${SRCS:.cc=.o}

.SUFFIXES:
.SUFFIXES: .o .cc

OUTLIB = libsocket_retarder.so.1

all: $(OUTLIB)

.cc.o :
	$(CXX) $(CXXFLAGS) -c $<

$(OUTLIB): $(OBJS)
	$(CXX) -Wl,-soname,$@ -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(OUTLIB)
