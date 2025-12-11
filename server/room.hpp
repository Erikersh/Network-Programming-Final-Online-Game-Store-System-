#pragma once
#include "../json.hpp"
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <algorithm>

using json = nlohmann::json;

struct Room {
    int id;
    std::string name;
    std::string host_user;
    std::string game_name; 
    std::string status;
    int game_port;         
    int max_players;
    std::vector<std::string> players;
};

class RoomManager {
private:
    std::map<int, Room> rooms;
    std::mutex room_mutex;

public:
    int create_room(std::string name, std::string host, std::string game_name, int max_players) {
        std::lock_guard<std::mutex> lock(room_mutex);
        
        int id = 1;
        while (rooms.find(id) != rooms.end()) {
            id++;
        }

        Room r;
        r.id = id;
        r.name = name;
        r.host_user = host;
        r.game_name = game_name;
        r.status = "idle";
        r.game_port = 0;
        r.max_players = max_players;
        r.players.push_back(host);
        
        rooms[id] = r;
        return id;
    }

    bool join_room(int room_id, std::string user) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return false;
        
        Room& r = rooms[room_id];
        if (r.status != "idle") return false; 
        
        if (r.players.size() >= (size_t)r.max_players) return false;

        for(const auto& p : r.players) {
            if (p == user) return false;
        }
        r.players.push_back(user);
        return true;
    }

    bool is_room_full(int room_id) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return false;
        return rooms[room_id].players.size() == (size_t)rooms[room_id].max_players;
    }

    int leave_room(int room_id, std::string user) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return -1;

        Room& r = rooms[room_id];
        
        if (r.host_user == user) {
            rooms.erase(room_id); 
            return 1; 
        }

        auto it = std::find(r.players.begin(), r.players.end(), user);
        if (it != r.players.end()) {
            r.players.erase(it);
            if (r.players.empty()) {
                rooms.erase(room_id);
                return 1;
            }
            return 0;
        }
        return -1;
    }

    json list_rooms() {
        std::lock_guard<std::mutex> lock(room_mutex);
        json list = json::array();
        for (auto const& [id, r] : rooms) {
            json item;
            item["id"] = r.id;
            item["name"] = r.name;
            item["game"] = r.game_name;
            item["status"] = r.status;
            item["players"] = r.players.size();
            item["max_players"] = r.max_players;
            list.push_back(item);
        }
        return list;
    }
    
    json get_room_info(int room_id) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return nullptr;
        
        Room& r = rooms[room_id];
        json info;
        info["id"] = r.id;
        info["name"] = r.name;
        info["host"] = r.host_user;
        info["game"] = r.game_name;
        info["status"] = r.status;
        info["players"] = r.players;
        info["max_players"] = r.max_players;
        info["game_port"] = r.game_port; 
        return info;
    }

    bool start_game(int room_id, int port) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return false;
        rooms[room_id].status = "playing";
        rooms[room_id].game_port = port;
        return true;
    }

    bool finish_game(int room_id) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return false;
        rooms[room_id].status = "idle";
        rooms[room_id].game_port = 0;
        return true;
    }

    bool is_game_active(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(room_mutex);
        for (auto const& [id, r] : rooms) {
            if (r.game_name == game_name) {
                return true; 
            }
        }
        return false;
    }

    std::string get_room_game_name(int room_id) {
        std::lock_guard<std::mutex> lock(room_mutex);
        if (rooms.find(room_id) == rooms.end()) return "";
        return rooms[room_id].game_name;
    }
};