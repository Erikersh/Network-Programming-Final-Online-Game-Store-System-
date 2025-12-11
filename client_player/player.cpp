#include "../basic.hpp"
#include "../json.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <sys/select.h>
#include <unistd.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

#define SERVER_IP "140.113.17.11"
#define SERVER_PORT 10988

enum class ClientState { LOGIN, LOBBY, IN_ROOM };

int sockfd = -1;
ClientState current_state = ClientState::LOGIN;
std::string current_user;
json current_room_data;
bool running = true;

void clear_screen() {
    std::cout << "\033[2J\033[1;1H";
}

std::string read_line() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

void ensure_directory_exists(const std::string &path) {
    if (!fs::exists(path)) fs::create_directories(path);
}

std::string get_local_version_simple(const std::string& game_name) {
    if (current_user.empty()) return "0.0";
    std::string v_path = "client_player/downloads/" + current_user + "/" + game_name + ".ver";
    std::ifstream in(v_path);
    if (in.good()) {
        std::string v; in >> v; return v;
    }
    return "0.0";
}

void print_list_or_empty(std::string title, const json& list) {
    std::cout << "\n=== " << title << " ===" << std::endl;
    if (list.empty()) {
        std::cout << "empty" << std::endl; 
    } else {
        for (const auto& item : list) {
            if (item.is_string()) {
                std::cout << "- " << item.get<std::string>() << std::endl;
            } else if (item.is_object()) {
                std::cout << "[" << item.value("id", -1) << "] " 
                          << item.value("name", "Unknown") 
                          << " (Game: " << item.value("game", "") << ") - " 
                          << item.value("status", "") << std::endl;
            }
        }
    }
    std::cout << "====================" << std::endl;
}

bool is_pure_integer(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!isdigit(c)) return false;
    }
    return true;
}

void do_rate_game(const json& game_info) {
    std::string gname = game_info["name"];
    clear_screen();
    std::cout << "=== Rate & Comment " << gname << " ===" << std::endl;

    int score = 0;
    while (true) {
        std::cout << "Rating (Integers 1-5): "; 
        std::string input = read_line();
        
        if (!is_pure_integer(input)) {
            std::cout << "[Warning] Please enter an integer number (no decimals)." << std::endl;
            continue;
        }

        try {
            score = std::stoi(input);
            if (score >= 1 && score <= 5) break;
            std::cout << "[Warning] Please enter a number between 1 and 5." << std::endl;
        } catch (...) {
            std::cout << "[Warning] Invalid input." << std::endl;
        }
    }

    std::cout << "Comment (Optional, press Enter to skip): ";
    std::string content = read_line();
    if (content.empty()) content = "No comment";

    json req;
    req["action"] = "add_comment";
    req["game_name"] = gname;
    req["score"] = score;
    req["content"] = content;

    send_message(sockfd, req.dump());

    std::string res_str;
    if (recv_message(sockfd, res_str)) {
        json res = json::parse(res_str);
        if (res["status"] == "ok") {
            std::cout << "[Success] Thank you for your feedback!" << std::endl;
        } else {
            std::cout << "[Error] " << res.value("message", "Unknown") << std::endl;
        }
    }
    sleep(2);
}

bool download_game_blocking(std::string game_name, std::string server_ver = "") {
    std::cout << "[Auto-Download] Checking game: " << game_name << "...\n";

    json req = {
        {"action", "download_request"},
        {"gamename", game_name}};
    send_message(sockfd, req.dump());

    std::string res_str;
    if (!recv_message(sockfd, res_str)) return false;

    json res = json::parse(res_str);
    if (res["status"] != "ok") {
        std::cout << "[Error] Download failed: "
                  << res.value("message", "Unknown") << "\n";
        sleep(2);
        return false;
    }

    int data_port = res["port"];
    long filesize = res["filesize"];
    std::string filename = res["filename"];

    std::cout << "[Auto-Download] Fetching " << filename
              << " (" << filesize << " bytes)...\n";

    std::string user_dir = "client_player/downloads/" + current_user;
    ensure_directory_exists(user_dir);

    std::string save_path = user_dir + "/" + filename;

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(data_port);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(data_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "[Error] Data connection failed.\n";
        close(data_sock);
        return false;
    }

    std::ofstream outfile(save_path, std::ios::binary);
    char buffer[4096];
    long total_received = 0;

    while (total_received < filesize) {
        size_t to_recv = std::min<long>(sizeof(buffer), filesize - total_received);
        if (!recv_raw_data(data_sock, buffer, to_recv)) break;
        outfile.write(buffer, to_recv);
        total_received += to_recv;
        std::cout << "\rProgress: " << (total_received * 100 / filesize) << "%" << std::flush;
    }

    std::cout << "\n";
    outfile.close();
    close(data_sock);

    if (total_received == filesize) {
        std::cout << "[Success] Game downloaded.\n";
        if (!server_ver.empty()) {
            std::string v_path = "client_player/downloads/" + current_user + "/" + game_name + ".ver";
            std::ofstream vout(v_path);
            vout << server_ver;
        }
        return true;
    }
    return false;
}

void show_game_store_interactive() {
    std::cout << "\n[System] Connecting to Store...\n";
    json req = {{"action", "list_games"}};
    send_message(sockfd, req.dump());
    
    std::string res_str;
    if (!recv_message(sockfd, res_str)) return;
    json res = json::parse(res_str);
    json games = res["data"];

    while (true) {
        clear_screen();
        std::cout << "=== Game Store ===\n";
        for (size_t i = 0; i < games.size(); ++i) {
            std::string name = games[i]["name"];
            std::string s_ver = games[i].value("version", "1.0");
            std::string l_ver = get_local_version_simple(name);
            int dl_count = games[i].value("downloads", 0);
            
            std::string status_tag = "";
            if (l_ver == "0.0") status_tag = "[New]";
            else if (l_ver != s_ver) status_tag = "[Update Available]";
            else status_tag = "[Installed]";

            std::cout << (i + 1) << ". " << name << " " << status_tag 
                      << " (Rating: " << games[i].value("avg_rating", 0.0) 
                      << " | DL: " << dl_count << ")" << std::endl;
        }
        std::cout << "0. Back\nSelect: ";
        
        std::string input = read_line();
        if (input == "0") break;

        try {
            size_t idx = std::stoi(input);
            if (idx < 1 || idx > games.size()) continue;
            json& g = games[idx-1];

            clear_screen();
            std::cout << "=== " << g["name"] << " ===" << std::endl;
            std::cout << "Author: " << g.value("dev", "Unknown") << std::endl;
            std::cout << "Type: " << g.value("game_type", "CLI") << std::endl;
            std::cout << "Max Players: " << g.value("max_players", 2) << std::endl;
            std::cout << "Version: " << g.value("version", "1.0") << std::endl;
            std::cout << "Description: " << g.value("description", "None") << std::endl;
            
            std::cout << "\n--- Ratings & Comments (" << g.value("avg_rating", 0.0) << "/5.0) ---" << std::endl;
            if (g.contains("comments")) {
                for (const auto& c : g["comments"]) {
                    std::cout << c["user"] << ": " << c["score"] << "/5 - " << c["content"] << std::endl;
                }
            } else {
                std::cout << "(No comments yet)" << std::endl;
            }

            std::cout << "\nActions:" << std::endl;
            std::cout << "1. Download / Update Game" << std::endl;
            std::cout << "2. Rate this Game" << std::endl;
            std::cout << "3. Back" << std::endl;
            std::cout << "Select: ";

            std::string act = read_line();
            if (act == "1") {
                download_game_blocking(g["name"], g.value("version", "1.0"));
                read_line();
            } else if (act == "2") {
                do_rate_game(g);
            }

        } catch (...) {}
    }
}

void list_games_simple() {
    std::cout << "\n[System] Fetching game list...\n";
    json req = {{"action", "list_games"}};
    send_message(sockfd, req.dump());

    std::string res_str;
    if (!recv_message(sockfd, res_str)) return;

    json res = json::parse(res_str);

    std::cout << "--- Available Games ---\n";
    if (res.contains("data") && res["data"].is_array()) {
        if (res["data"].empty())
            std::cout << "(No games available)\n";

        for (auto &item : res["data"])
            std::cout << "* " << item["name"] << "\n";
    }
    std::cout << "-----------------------\n";
}

void show_lobby_status() {
    json req_rooms; req_rooms["action"] = "list_rooms";
    send_message(sockfd, req_rooms.dump());
    std::string res_rooms_str;
    json room_list = json::array();
    if (recv_message(sockfd, res_rooms_str)) {
        json res = json::parse(res_rooms_str);
        if (res.contains("data")) room_list = res["data"];
    }

    json req_players; req_players["action"] = "list_players";
    send_message(sockfd, req_players.dump());
    std::string res_players_str;
    json player_list = json::array();
    if (recv_message(sockfd, res_players_str)) {
        json res = json::parse(res_players_str);
        if (res.contains("data")) player_list = res["data"];
    }

    print_list_or_empty("Active Rooms", room_list);
    print_list_or_empty("Online Players", player_list);
}

// ------------------------------------------------------------
// UI & Message Loop
// ------------------------------------------------------------
void draw_ui() {
    clear_screen();

    if (current_state == ClientState::LOGIN) {
        std::cout << "==================================\n"
                  << "      Game Store - Login\n"
                  << "==================================\n"
                  << "1. Register (New User)\n"
                  << "2. Login\n"
                  << "3. Quit\n"
                  << "----------------------------------\n"
                  << "Select (1-3): " << std::flush;
    }
    else if (current_state == ClientState::LOBBY) {
        std::cout << "==================================\n"
                  << " Lobby (" << current_user << ")\n"
                  << "==================================\n"
                  << "1. Game Store (View Details & Rate)\n"
                  << "2. List Active Rooms and Online Players\n"
                  << "3. Create Room\n"
                  << "4. Join Room\n"
                  << "5. Logout\n"
                  << "----------------------------------\n"
                  << "Select (1-5): " << std::flush;
    }
    else if (current_state == ClientState::IN_ROOM) {
        std::cout << "==================================\n"
                  << " Room: " << current_room_data.value("name", "Unknown")
                  << " (ID: " << current_room_data.value("id", -1) << ")\n"
                  << "==================================\n"
                  << "Game: " << current_room_data.value("game", "??") << "\n"
                  << "Status: " << current_room_data.value("status", "idle") << "\n"
                  << "Host: " << current_room_data.value("host", "??") << "\n"
                  << "Players: ";

        if (current_room_data.contains("players")) {
            for (auto &p : current_room_data["players"])
                std::cout << p.get<std::string>() << " ";
        }

        std::cout << "\n----------------------------------\n";

        bool is_host = (current_room_data["host"] == current_user);

        if (is_host) {
            std::cout << "1. Start Game\n"
                      << "2. Disband Room\n"
                      << "Select (1-2): " << std::flush;
        } else {
            std::cout << "1. Leave Room\n"
                      << "Waiting for host to start...\n"
                      << "Select (1): " << std::flush;
        }
    }
}

void launch_game_client(int port, std::string filename) {
    std::string path = "client_player/downloads/" + current_user + "/" + filename;
    std::cout << "\n[System] Launching Game: " << filename << " on port " << port << "...\n";

    struct stat st;
    if (stat(path.c_str(), &st) == -1) {
        std::cout << "[Error] Game file missing! (" << path << ")\n";
        sleep(3);
        return;
    }

#ifdef _WIN32
    std::string cmd = "start /W python " + path + " --client --connect " SERVER_IP " " + std::to_string(port);
#else
    std::string cmd = "python3 " + path + " --client --connect " SERVER_IP " " + std::to_string(port);
#endif

    system(cmd.c_str());

    if (current_room_data["host"] == current_user) {
        json req = {{"action", "finish_game"}};
        send_message(sockfd, req.dump());
    }
}

void handle_server_message() {
    std::string msg_str;
    if (!recv_message(sockfd, msg_str)) {
        std::cout << "\nDisconnected from server.\n";
        running = false;
        return;
    }

    json msg;
    try {
        msg = json::parse(msg_str);
    } catch (...) {
        return;
    }

    std::string status = msg.value("status", "");
    std::string action = msg.value("action", "");

    if (action == "player_joined" || action == "player_left" || action == "room_reset") {
        if (current_state == ClientState::IN_ROOM) {
            current_room_data = msg["data"];
            draw_ui();
        }
        return;
    }

    if (action == "room_disbanded") {
        std::cout << "\n[Info] Room disbanded by host.\n";
        current_state = ClientState::LOBBY;
        sleep(1);
        draw_ui();
        return;
    }

    if (action == "game_start") {
        int port = msg["game_port"];
        std::string filename = msg["filename"];
        launch_game_client(port, filename);
        draw_ui();
        return;
    }

    bool join_success = (status == "ok" && msg.value("message", "") == "Joined" && msg.contains("data"));

    if (msg.contains("room_id") || join_success) {
        if (status == "ok") {
            std::string game_name = msg["data"]["game"];
            if (download_game_blocking(game_name)) {
                current_state = ClientState::IN_ROOM;
                current_room_data = msg["data"];
            } else {
                std::cout << "\n[Error] Auto-download failed during join.\n";
                sleep(2);
            }
        } else {
            std::cout << "\n[Error] " << msg.value("message", "") << "\n";
            sleep(1);
        }
    }
    else if (msg.contains("message") || msg.contains("role")) {
        if (status == "ok") {
            if (msg.contains("role")) {
                current_state = ClientState::LOBBY;
            } else {
                std::cout << "\n[Success] " << msg.value("message", "") << "\n";
                sleep(1);
            }
        } else {
            std::cout << "\n[Error] " << msg.value("message", "Unknown error") << "\n";
            sleep(1);
        }
    }

    draw_ui();
}

bool is_valid_credential(const std::string& s) {
    return s.find_first_not_of(" \t\n\v\f\r") != std::string::npos;
}

void handle_input() {
    std::string input = read_line();
    if (input.empty()) {
        draw_ui();
        return;
    }

    json req;
    if (current_state == ClientState::LOGIN) {
        if (input == "1") {
            std::string user, pass;
            while (true) {
                std::cout << "Enter Username: ";
                user = read_line();
                if (is_valid_credential(user)) break;
                std::cout << "[Warning] Username cannot be empty!" << std::endl;
            }
            while (true) {
                std::cout << "Enter Password: ";
                pass = read_line();
                if (is_valid_credential(pass)) break;
                std::cout << "[Warning] Password cannot be empty!" << std::endl;
            }

            req = {{"action", "register"},
                   {"username", user},
                   {"password", pass},
                   {"role", "player"}};
        }

        else if (input == "2") {
            std::cout << "Enter Username: ";
            std::string user = read_line();

            std::cout << "Enter Password: ";
            std::string pass = read_line();

            req = {{"action", "login"},
                   {"username", user},
                   {"password", pass}};

            current_user = user;
        }

        else if (input == "3") {
            running = false;
            return;
        }
    }
    else if (current_state == ClientState::LOBBY) {
        if (input == "1") {
            show_game_store_interactive();
            draw_ui();
            return;
        }
        else if (input == "2") {
            show_lobby_status();
            std::cout << "Press Enter to return..."; read_line();
        }
        else if (input == "3") { 
            show_lobby_status(); 
            list_games_simple();
            std::cout << "Enter Room Name: "; std::string rname = read_line();
            if (rname.empty()) return;

            std::cout << "Enter Game Name (from list above): "; std::string gname = read_line();
            
            if (download_game_blocking(gname)) {
                req["action"] = "create_room";
                req["room_name"] = rname;
                req["game_name"] = gname;
            } else { return; }
        }
        else if (input == "4") { 
            show_lobby_status();
            std::cout << "Enter Room ID to join: "; 
            std::string rid_str = read_line();
            if (rid_str.empty()) return;
            try {
                req["action"] = "join_room";
                req["room_id"] = std::stoi(rid_str);
            } catch(...) {
                std::cout << "Invalid Number" << std::endl; sleep(1); draw_ui(); return;
            }
        }
        else if (input == "5") { 
            req["action"] = "logout";
            current_state = ClientState::LOGIN;
            current_user = "";
        }
    }
    else if (current_state == ClientState::IN_ROOM) {
        bool is_host = (current_room_data["host"] == current_user);
        if (is_host) {
            if (input == "1") req["action"] = "start_game";
            else if (input == "2") {
                req["action"] = "leave_room";
                current_state = ClientState::LOBBY;
            }
        } else {
            if (input == "1") {
                req["action"] = "leave_room";
                current_state = ClientState::LOBBY;
            }
        }
    }

    if (!req.is_null()) send_message(sockfd, req.dump());
    else draw_ui();
}

int main() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Cannot connect to server\n";
        return 1;
    }

    draw_ui();

    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sockfd, &read_fds);

        if (select(sockfd + 1, &read_fds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(STDIN_FILENO, &read_fds)) handle_input();
        if (FD_ISSET(sockfd, &read_fds)) handle_server_message();
    }

    close(sockfd);
    return 0;
}