CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -Werror -std=c11
CXXFLAGS = -Wall -Wextra -Werror -std=c++17

SERVER = server
CLIENT = client

COMMON_OBJS = io_utils.o

SERVER_OBJS = server.o $(COMMON_OBJS)
CLIENT_OBJS = client.o $(COMMON_OBJS)

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CXX) -o $@ $(SERVER_OBJS)

$(CLIENT): $(CLIENT_OBJS)
	$(CXX) -o $@ $(CLIENT_OBJS)

server.o: server.cpp io_utils.h
	$(CXX) $(CXXFLAGS) -c server.cpp

client.o: client.cpp io_utils.h
	$(CXX) $(CXXFLAGS) -c client.cpp

io_utils.o: io_utils.c io_utils.h
	$(CC) $(CFLAGS) -c io_utils.c

clean:
	rm -f *.o $(SERVER) $(CLIENT)
