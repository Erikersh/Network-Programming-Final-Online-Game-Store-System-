#include "../basic.hpp"
#include "../json.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <limits>
#include <filesystem>
#include <sys/stat.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

#define SERVER_IP "140.113.17.11"
#define SERVER_PORT 10988

enum class ClientState {
    LOGIN,
    MAIN_MENU
};

int sockfd = -1;
ClientState current_state = ClientState::LOGIN;
std::string current_user;
bool running = true;

void clear_screen() {
    std::cout << "\033[2J\033[1;1H";
}

std::string read_line() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

long check_file_size(const std::string &filepath) {
    struct stat stat_buf;
    int rc = stat(filepath.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

bool send_file_data(int port, std::string filepath, long filesize) {
    std::cout << "[System] Connecting to data channel port " << port << "..." << std::endl;

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(data_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "[Error] Data connection failed." << std::endl;
        close(data_sock);
        return false;
    }

    std::cout << "[System] Uploading..." << std::flush;

    std::ifstream infile(filepath, std::ios::binary);
    char buffer[4096];
    long total_sent = 0;

    while (infile.read(buffer, sizeof(buffer)) || infile.gcount() > 0) {
        if (!send_raw_data(data_sock, buffer, infile.gcount())) {
            std::cout << " Failed!" << std::endl;
            break;
        }

        total_sent += infile.gcount();
        int progress = (total_sent * 100) / filesize;
        std::cout << "\r[System] Uploading: " << progress << "%" << std::flush;

        if (infile.eof()) break;
    }

    std::cout << std::endl;

    infile.close();
    close(data_sock);
    return true;
}

json fetch_my_games() {
    json req;
    req["action"] = "list_games";
    send_message(sockfd, req.dump());

    std::string res_str;
    if (!recv_message(sockfd, res_str)) return json::array();

    json res = json::parse(res_str);
    if (res["status"] != "ok") return json::array();

    json my_games = json::array();

    if (res.contains("data") && res["data"].is_array()) {
        for (const auto &g : res["data"]) {
            if (g.value("dev", "") == current_user) {
                my_games.push_back(g);
            }
        }
    }

    return my_games;
}

void do_remove_game() {
    clear_screen();
    std::cout << "=== Remove Game ===" << std::endl;
    std::cout << "Fetching your game list..." << std::endl;

    json my_games = fetch_my_games();

    if (my_games.empty()) {
        std::cout << "[Info] You have no games to remove." << std::endl;
        std::cout << "Press Enter to return...";
        read_line();
        return;
    }

    for (size_t i = 0; i < my_games.size(); ++i) {
        json &g = my_games[i];
        std::cout << (i + 1) << ". " << g["name"]
                  << " (Ver: " << g.value("version", "1.0") << ")" << std::endl;
    }

    std::cout << "0. Cancel" << std::endl;
    std::cout << "---------------------------" << std::endl;
    std::cout << "Select game to REMOVE (Number): ";

    std::string input = read_line();
    if (input == "0" || input.empty()) return;

    try {
        size_t idx = std::stoi(input);
        if (idx < 1 || idx > my_games.size()) throw std::runtime_error("Invalid index");

        json &target_game = my_games[idx - 1];
        std::string gamename = target_game["name"];

        std::cout << "\n[WARNING] Are you sure you want to remove '" << gamename << "'?" << std::endl;
        std::cout << "Players will no longer be able to download or create rooms for this game." << std::endl;
        std::cout << "Type 'yes' to confirm: ";

        std::string confirm = read_line();
        if (confirm != "yes") {
            std::cout << "Cancelled." << std::endl;
            sleep(1);
            return;
        }

        json req;
        req["action"] = "delete_game";
        req["gamename"] = gamename;

        if (!send_message(sockfd, req.dump())) {
            std::cout << "\n[Error] Removal Failed: Connection lost." << std::endl;
            std::cout << "The game remains on the server (State Preserved)." << std::endl;
            return;
        }

        std::string res_str;
        if (!recv_message(sockfd, res_str)) {
            std::cout << "\n[Error] Removal Failed: No response from server." << std::endl;
            std::cout << "Please check your connection and verify the game status later." << std::endl;
            return;
        }

        json res = json::parse(res_str);

        if (res.value("status", "") == "ok") {
            std::cout << "\n[Success] Game '" << gamename << "' has been removed from the store." << std::endl;
        } else {
            std::cout << "\n[Error] Removal Failed: " << res.value("message", "Unknown reason") << std::endl;
        }

    } catch (...) {
        std::cout << "Invalid input." << std::endl;
    }

    std::cout << "Press Enter...";
    read_line();
}

std::string get_required_input(const std::string& prompt, const std::string& field_name) {
    std::string input;
    while (true) {
        std::cout << prompt;
        input = read_line();
        if (!input.empty()) {
            return input;
        }
        std::cout << "[Warning] " << field_name << " is required! Please try again." << std::endl;
    }
}

std::string get_required_line(const std::string& prompt, const std::string& field_name) {
    std::string input;
    while (true) {
        std::cout << prompt;
        input = read_line();
        size_t first = input.find_first_not_of(' ');
        if (std::string::npos != first) {
            size_t last = input.find_last_not_of(' ');
            input = input.substr(first, (last - first + 1));
        }
        
        if (!input.empty()) {
            return input;
        }
        std::cout << "[Warning] " << field_name << " cannot be empty! Please fill it in." << std::endl;
    }
}

void do_upload_new() {
    clear_screen();
    std::cout << "=== Upload New Game (Strict Validation) ===" << std::endl;
    std::cout << "All fields are required. Please do not leave them empty." << std::endl;
    std::cout << "-------------------------------------------------------" << std::endl;

    std::string name = get_required_line("Game Name: ", "Game Name");
    std::string version = get_required_line("Version (e.g., 1.0): ", "Version");


    std::string type;
    while (true) {
        type = get_required_line("Game Type (CLI/GUI): ", "Game Type");
        for (auto & c: type) c = toupper(c);

        if (type == "CLI" || type == "GUI") {
            break;
        }
        std::cout << "[Warning] Invalid type. Please enter 'CLI' or 'GUI'." << std::endl;
    }

    int max_players = 0;
    while (true) {
        std::string input = get_required_line("Max Players (e.g., 2): ", "Max Players");
        try {
            max_players = std::stoi(input);
            if (max_players > 0) break;
            std::cout << "[Warning] Number of players must be > 0." << std::endl;
        } catch (...) {
            std::cout << "[Warning] Please enter a valid number." << std::endl;
        }
    }
    std::string desc = get_required_line("Description: ", "Description");

    std::string filepath;
    long filesize = -1;
    std::string filename;

    while (true) {
        std::cout << "File Path (e.g., ../templates/game.py): ";
        filepath = read_line();

        if (filepath == "cancel") {
            std::cout << "Upload cancelled." << std::endl;
            return;
        }

        if (filepath.empty()) {
            std::cout << "[Warning] File path is required!" << std::endl;
            continue;
        }

        filesize = check_file_size(filepath);
        if (filesize >= 0) {
            filename = filepath.substr(filepath.find_last_of("/\\") + 1);
            break;
        }

        std::cout << "[Error] File not found or unreadable: " << filepath << std::endl;
        std::cout << "Please enter a valid path (or type 'cancel')." << std::endl;
    }

    bool upload_success = false;
    while (!upload_success) {
        json req;
        req["action"] = "upload_request";
        req["is_new_game"] = true;
        req["gamename"] = name;
        req["version"] = version;
        req["description"] = desc;
        req["game_type"] = type;
        req["max_players"] = max_players;
        req["filename"] = filename;
        req["filesize"] = filesize;

        if (!send_message(sockfd, req.dump())) {
            std::cout << "[Error] Connection lost." << std::endl;
            return;
        }

        std::string res_str;
        if (!recv_message(sockfd, res_str)) {
            std::cout << "[Error] No response from server." << std::endl;
            return;
        }

        json res = json::parse(res_str);
        if (res.value("status", "") == "ok") {
            int port = res["port"];
            if (send_file_data(port, filepath, filesize)) {
                std::cout << "[Success] Game uploaded successfully!" << std::endl;
                upload_success = true;
            } else {
                std::cout << "[Error] File transfer failed." << std::endl;
            }
        } else {
            std::cout << "[Error] Server rejected: " << res.value("message", "Unknown") << std::endl;
            break; 
        }

        if (!upload_success) {
            std::cout << "Upload failed. Retry? (y/n): ";
            std::string choice = read_line();
            if (choice != "y" && choice != "Y") break;
        }
    }

    std::cout << "Press Enter...";
    read_line();
}

float parse_version(const std::string& ver_str) {
    if (ver_str.length() < 3) return -1.0f;
    if (ver_str[ver_str.length() - 2] != '.') return -1.0f;

    for (size_t i = 0; i < ver_str.length(); ++i) {
        if (i == ver_str.length() - 2) continue;
        if (!isdigit(ver_str[i])) return -1.0f;
    }

    try {
        float v = std::stof(ver_str);
        return (v > 0.0f) ? v : -1.0f;
    } catch (...) {
        return -1.0f;
    }
}

void do_update_game() {
    clear_screen();
    std::cout << "=== Update Existing Game (Strict & Smart) ===" << std::endl;
    std::cout << "Note: Press ENTER to keep the original value for optional fields." << std::endl;
    std::cout << "Fetching your game list..." << std::endl;

    json my_games = fetch_my_games();
    if (my_games.empty()) {
        std::cout << "[Info] You have no games to update." << std::endl;
        std::cout << "Press Enter to return...";
        read_line();
        return;
    }

    for (size_t i = 0; i < my_games.size(); ++i) {
        json &g = my_games[i];
        std::cout << (i + 1) << ". " << g["name"]
                  << " (Ver: " << g.value("version", "1.0") << ")" << std::endl;
    }

    std::cout << "0. Cancel" << std::endl;
    std::cout << "---------------------------" << std::endl;
    std::cout << "Select game to update (Number): ";

    std::string input = read_line();
    if (input == "0" || input.empty()) return;

    try {
        size_t idx = std::stoi(input);
        if (idx < 1 || idx > my_games.size()) throw std::runtime_error("Invalid index");

        json &target_game = my_games[idx - 1];
        
        std::string gamename = target_game["name"];
        std::string old_ver_str = target_game.value("version", "1.0");
        std::string old_desc = target_game.value("description", "");
        std::string old_type = target_game.value("game_type", "CLI");
        int old_max_players = target_game.value("max_players", 2);

        float old_ver_val = parse_version(old_ver_str);
        if (old_ver_val < 0) old_ver_val = 1.0f;

        std::cout << "\n--- Updating: " << gamename << " ---" << std::endl;

        std::string new_ver;
        while (true) {
            std::cout << "New Version (Current: " << old_ver_str << "): ";
            std::string buf = read_line();

            if (buf.empty()) {
                new_ver = old_ver_str;
                std::cout << "-> Keeping version: " << new_ver << std::endl;
                break;
            }

            float new_val = parse_version(buf);
            
            if (new_val < 0) {
                std::cout << "[Warning] Invalid format! Must be a positive 1-decimal number (e.g., 2.5)." << std::endl;
                continue;
            }

            if (new_val < old_ver_val) {
                std::cout << "[Warning] New version (" << new_val << ") cannot be older than current (" << old_ver_val << ")." << std::endl;
                continue;
            }

            new_ver = buf;
            break;
        }

        std::string new_type;
        while (true) {
            std::cout << "Game Type (CLI/GUI) [Current: " << old_type << "]: ";
            std::string buf = read_line();

            if (buf.empty()) {
                new_type = old_type;
                std::cout << "-> Keeping type: " << new_type << std::endl;
                break;
            }

            for (auto & c: buf) c = toupper(c);
            if (buf == "CLI" || buf == "GUI") {
                new_type = buf;
                break;
            }
            std::cout << "[Warning] Invalid type. Enter 'CLI' or 'GUI' (or Enter to keep old)." << std::endl;
        }

        int new_max_players = old_max_players;
        while (true) {
            std::cout << "Max Players [Current: " << old_max_players << "]: ";
            std::string buf = read_line();

            if (buf.empty()) {
                std::cout << "-> Keeping max players: " << new_max_players << std::endl;
                break;
            }

            try {
                int val = std::stoi(buf);
                if (val > 0) {
                    new_max_players = val;
                    break;
                }
                std::cout << "[Warning] Must be > 0." << std::endl;
            } catch (...) {
                std::cout << "[Warning] Invalid number." << std::endl;
            }
        }

        std::cout << "Description [Current: " << old_desc << "]: ";
        std::string new_desc = read_line();
        if (new_desc.empty()) {
            new_desc = old_desc;
            std::cout << "-> Keeping description." << std::endl;
        }

        std::string filepath;
        long filesize = -1;
        std::string filename;

        while (true) {
            std::cout << "New File Path (Required): ";
            filepath = read_line();

            if (filepath == "cancel") return;

            if (filepath.empty()) {
                std::cout << "[Warning] File path cannot be empty! Please enter a path." << std::endl;
                continue;
            }

            filesize = check_file_size(filepath);
            if (filesize >= 0) {
                filename = filepath.substr(filepath.find_last_of("/\\") + 1);
                break;
            }

            std::cout << "[Error] File not found or unreadable: " << filepath << std::endl;
            std::cout << "Please try again (or type 'cancel')." << std::endl;
        }

        bool update_success = false;
        while (!update_success) {
            json req;
            req["action"] = "upload_request";
            req["is_new_game"] = false;
            req["gamename"] = gamename;
            req["version"] = new_ver;
            req["description"] = new_desc;
            req["game_type"] = new_type;
            req["max_players"] = new_max_players;
            req["filename"] = filename;
            req["filesize"] = filesize;

            if (!send_message(sockfd, req.dump())) {
                std::cout << "[Error] Connection lost." << std::endl;
                return;
            }

            std::string res_str;
            if (!recv_message(sockfd, res_str)) {
                 std::cout << "[Error] No response from server." << std::endl;
                 return;
            }

            json res = json::parse(res_str);
            if (res.value("status", "") == "ok") {
                int port = res["port"];
                if (send_file_data(port, filepath, filesize)) {
                    std::cout << "[Success] Game updated to version " << new_ver << "!" << std::endl;
                    update_success = true;
                } else {
                    std::cout << "[Error] File transfer failed." << std::endl;
                }
            } else {
                std::cout << "[Error] Server rejected: " << res.value("message", "") << std::endl;
                break;
            }

            if (!update_success) {
                std::cout << "Retry? (y/n): ";
                std::string retry = read_line();
                if (retry != "y" && retry != "Y") break;
            }
        }

    } catch (...) {
        std::cout << "Invalid input." << std::endl;
    }

    std::cout << "Press Enter...";
    read_line();
}

void do_list_my_games_ui() {
    clear_screen();
    std::cout << "=== My Published Games ===" << std::endl;

    json my_games = fetch_my_games();
    if (my_games.empty()) {
        std::cout << "(No games found)" << std::endl;
    } else {
        for (const auto &g : my_games) {
            std::cout << "Name: " << g["name"] << std::endl;
            std::cout << "Version: " << g.value("version", "1.0") << std::endl;
            std::cout << "File: " << g.value("filename", "??") << std::endl;
            std::cout << "Desc: " << g.value("description", "") << std::endl;
            std::cout << "--------------------------" << std::endl;
        }
    }

    std::cout << "Press Enter to return...";
    read_line();
}

bool is_valid_string(const std::string& s) {
    return s.find_first_not_of(" \t\n\v\f\r") != std::string::npos;
}

void do_auth_menu() {
    clear_screen();
    std::cout << "==================================\n"
                  << "      Developer - Login\n"
                  << "==================================\n"
                  << "1. Register (New Developer)\n"
                  << "2. Login\n"
                  << "3. Quit\n"
                  << "----------------------------------\n"
                  << "Select (1-3): ";

    std::string input = read_line();
    json req;

    if (input == "1") { 
        std::string u, p;
        
        while (true) {
            std::cout << "Username: ";
            u = read_line();
            if (is_valid_string(u)) break;
            std::cout << "[Warning] Username cannot be empty or just spaces!" << std::endl;
        }

        while (true) {
            std::cout << "Password: ";
            p = read_line();
            if (is_valid_string(p)) break;
            std::cout << "[Warning] Password cannot be empty or just spaces!" << std::endl;
        }

        req["action"] = "register";
        req["username"] = u;
        req["password"] = p;
        req["role"] = "developer";

    } else if (input == "2") { 
        std::cout << "Username: ";
        std::string u = read_line();
        std::cout << "Password: ";
        std::string p = read_line();

        req["action"] = "login";
        req["username"] = u;
        req["password"] = p;
        current_user = u;

    } else if (input == "3") {
        running = false;
        return;

    } else {
        return;
    }

    send_message(sockfd, req.dump());

    std::string res_str;
    if (recv_message(sockfd, res_str)) {
        json res = json::parse(res_str);

        if (res.value("status", "") == "ok") {
            if (input == "2") {
                if (res.value("role", "") == "developer") {
                    current_state = ClientState::MAIN_MENU;
                } else {
                    std::cout << "[Error] This account is not a developer!" << std::endl;
                    sleep(2);
                }
            } else {
                std::cout << "[Success] " << res.value("message", "Registered") << std::endl;
                sleep(1);
            }
        } else {
            std::cout << "[Error] " << res.value("message", "Unknown Error") << std::endl;
            sleep(1);
        }

    } else {
        std::cout << "[Error] Server disconnected or not responding." << std::endl;
        running = false;
    }
}

void do_main_menu() {
    clear_screen();
    std::cout << "=== Developer Studio (" << current_user << ") ===" << std::endl;

    std::cout << "1. List My Games" << std::endl;
    std::cout << "2. Upload New Game" << std::endl;
    std::cout << "3. Update Existing Game" << std::endl;
    std::cout << "4. Remove Game" << std::endl;
    std::cout << "5. Logout" << std::endl;
    std::cout << "Select(1-5): ";

    std::string input = read_line();
    if (input == "1") do_list_my_games_ui();
    else if (input == "2") do_upload_new();
    else if (input == "3") do_update_game();
    else if (input == "4") do_remove_game();
    else if (input == "5") {
        json req;
        req["action"] = "logout";
        send_message(sockfd, req.dump());
        current_state = ClientState::LOGIN;
        current_user.clear();
    }
}

int main() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Cannot connect to server." << std::endl;
        return 1;
    }

    while (running) {
        if (current_state == ClientState::LOGIN)
            do_auth_menu();
        else
            do_main_menu();
    }

    close(sockfd);
    return 0;
}