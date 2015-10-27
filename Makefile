CXX = g++

CXXFLAGS = -g -O0 -Wall -fPIC -std=c++0x
LDFLAGS = -shared -ldl -lpthread -fPIC

SRCS	= retarder.cpp
OBJS	= ${SRCS:.cpp=.o}

.SUFFIXES:
.SUFFIXES: .o .cpp

OUTLIB = libsocket_retarder.so.1

all: $(OUTLIB)

.cpp.o :
	$(CXX) $(CXXFLAGS) -c $<

$(OUTLIB): $(OBJS)
	$(CXX) -Wl,-soname,$@ -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(OUTLIB)
