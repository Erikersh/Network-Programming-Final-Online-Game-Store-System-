CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread

SERVER_BIN = server_app
DEV_BIN = dev_app
PLAYER_BIN = player_app

COMMON_SRC = basic.cpp
SERVER_SRC = server/main.cpp
DEV_SRC = client_dev/developer.cpp
PLAYER_SRC = client_player/player.cpp

all: $(SERVER_BIN) $(DEV_BIN) $(PLAYER_BIN)

$(SERVER_BIN): $(SERVER_SRC) $(COMMON_SRC)
	$(CXX) $(CXXFLAGS) -o $(SERVER_BIN) $(SERVER_SRC) $(COMMON_SRC)

$(DEV_BIN): $(DEV_SRC) $(COMMON_SRC)
	$(CXX) $(CXXFLAGS) -o $(DEV_BIN) $(DEV_SRC) $(COMMON_SRC)

$(PLAYER_BIN): $(PLAYER_SRC) $(COMMON_SRC)
	$(CXX) $(CXXFLAGS) -o $(PLAYER_BIN) $(PLAYER_SRC) $(COMMON_SRC)

clean:
	rm -f $(SERVER_BIN) $(DEV_BIN) $(PLAYER_BIN)