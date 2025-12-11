#pragma once
#include "../json.hpp"
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>

using json = nlohmann::json;

float calculate_rating(const json& comments) {
    if (comments.empty()) return 0.0f;
    float sum = 0;
    for (const auto& c : comments) {
        sum += c.value("score", 0);
    }
    return sum / comments.size();
}

class Database {
private:
    const std::string DB_FILE = "database.json";
    std::mutex db_mutex;
    json db_data;

    void load() {
        std::ifstream in(DB_FILE);
        if (in.good()) {
            try {
                in >> db_data;
            } catch (...) {
                std::cerr << "Warning: DB file corrupted or empty, initializing new." << std::endl;
                db_data = json::object();
            }
        }
        if (!db_data.contains("users")) db_data["users"] = json::array();
        if (!db_data.contains("games")) db_data["games"] = json::array();
    }

    void save() {
        std::ofstream out(DB_FILE);
        out << db_data.dump(4);
    }

public:
    Database() { load(); }

    std::string get_game_owner(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (const auto& g : db_data["games"]) {
            if (g["name"] == game_name) {
                return g["dev"];
            }
        }
        return "";
    }

    int get_game_max_players(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (const auto& g : db_data["games"]) {
            if (g["name"] == game_name) {
                return g.value("max_players", 2);
            }
        }
        return 2;
    }

    void increment_download_count(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (auto& g : db_data["games"]) {
            if (g["name"] == game_name) {

                int current = g.value("downloads", 0);
                g["downloads"] = current + 1;
                save();
                return;
            }
        }
    }

    void record_play_history(const std::string& username, const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        
        for (auto& u : db_data["users"]) {
            if (u["username"] == username) {
                if (!u.contains("play_history")) {
                    u["play_history"] = json::array();
                }
                
                bool played = false;
                for (const auto& g : u["play_history"]) {
                    if (g == game_name) { played = true; break; }
                }
                
                if (!played) {
                    u["play_history"].push_back(game_name);
                    save();
                }
                return;
            }
        }
    }

    bool has_played(const std::string& username, const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (const auto& u : db_data["users"]) {
            if (u["username"] == username) {
                if (u.contains("play_history")) {
                    for (const auto& g : u["play_history"]) {
                        if (g == game_name) return true;
                    }
                }
            }
        }
        return false;
    }

    bool add_comment(const std::string& game_name, const std::string& user, int score, const std::string& content) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (auto& g : db_data["games"]) {
            if (g["name"] == game_name) {
                if (!g.contains("comments")) g["comments"] = json::array();

                for (const auto& c : g["comments"]) {
                    if (c["user"] == user) return false;
                }

                json comment = {
                    {"user", user},
                    {"score", score},
                    {"content", content}
                };
                g["comments"].push_back(comment);
                save();
                return true;
            }
        }
        return false;
    }

    void record_download(const std::string& game_name, const std::string& username) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (auto& g : db_data["games"]) {
            if (g["name"] == game_name) {
                if (!g.contains("downloaded_by")) {
                    g["downloaded_by"] = json::array();
                }
                
                bool already_downloaded = false;
                for (const auto& user : g["downloaded_by"]) {
                    if (user == username) {
                        already_downloaded = true;
                        break;
                    }
                }
                
                if (!already_downloaded) {
                    g["downloaded_by"].push_back(username);
                    save();
                }
                return;
            }
        }
    }

    json get_games() {
        std::lock_guard<std::mutex> lock(db_mutex);
        json list = json::array();
        for (const auto& g : db_data["games"]) {
            json item = g;
            
            if (g.contains("comments")) {
                item["avg_rating"] = calculate_rating(g["comments"]);
                item["comment_count"] = g["comments"].size();
            } else {
                item["avg_rating"] = 0.0;
                item["comment_count"] = 0;
            }

            if (g.contains("downloaded_by")) {
                item["downloads"] = g["downloaded_by"].size();
            } else {
                item["downloads"] = 0;
            }
            
            item.erase("downloaded_by");

            list.push_back(item);
        }
        return list;
    }

    bool register_user(const std::string& username, const std::string& password, const std::string& role) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (const auto& u : db_data["users"]) {
            if (u["username"] == username) return false; 
        }
        json new_user = {
            {"username", username},
            {"password", password},
            {"role", role} 
        };
        db_data["users"].push_back(new_user);
        save();
        return true;
    }

    bool login_user(const std::string& username, const std::string& password, std::string& out_role) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (const auto& u : db_data["users"]) {
            if (u["username"] == username && u["password"] == password) {
                out_role = u["role"];
                return true;
            }
        }
        return false;
    }

    void upsert_game(const std::string& dev_name, const std::string& game_name, 
                     const std::string& desc, const std::string& filename, 
                     const std::string& version, 
                     const std::string& type, int max_players) {
        
        std::lock_guard<std::mutex> lock(db_mutex);
        bool found = false;
        
        for (auto& g : db_data["games"]) {
            if (g["name"] == game_name && g["dev"] == dev_name) {
                g["description"] = desc;
                g["filename"] = filename;
                g["version"] = version;
                g["game_type"] = type;
                g["max_players"] = max_players;
                found = true;
                break;
            }
        }
        
        if (!found) {
            json new_game = {
                {"name", game_name},
                {"dev", dev_name},
                {"description", desc},
                {"filename", filename},
                {"version", version},
                {"game_type", type},
                {"max_players", max_players},
                {"downloaded_by", json::array()}
            };
            db_data["games"].push_back(new_game);
        }
        save();
    }

    std::string delete_game(const std::string& dev_name, const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto& games = db_data["games"];
        for (auto it = games.begin(); it != games.end(); ++it) {
            if ((*it)["name"] == game_name && (*it)["dev"] == dev_name) {
                std::string filename = (*it)["filename"];
                games.erase(it);
                save();
                return filename;
            }
        }
        return "";
    }
    
    std::string get_game_filename(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (const auto& g : db_data["games"]) {
            if (g["name"] == game_name) {
                return g["filename"];
            }
        }
        return "";
    }
};