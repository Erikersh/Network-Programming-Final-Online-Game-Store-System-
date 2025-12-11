#include "../basic.hpp"
#include "db.hpp"
#include "room.hpp"

#include <iostream>
#include <vector>
#include <map>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>

#define SERVER_PORT 10988

enum class ClientState {
    CONNECTED,
    LOGGED_IN,
    IN_ROOM
};

struct ClientInfo {
    int sockfd;
    ClientState state;
    std::string username;
    std::string role;
    int room_id;

    ClientInfo() : sockfd(-1), state(ClientState::CONNECTED), room_id(-1) {}
};

Database db;
RoomManager room_mgr;
std::map<int, ClientInfo> clients;
fd_set master_fds;
int fdmax;

void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void ensure_directory_exists(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == -1) {
        mkdir(path.c_str(), 0777);
    }
}

void handle_file_upload_connection(int transfer_sockfd, std::string filepath, size_t filesize) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    struct timeval tv;
    tv.tv_sec  = 10;
    tv.tv_usec = 0;
    setsockopt(transfer_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    int data_sock = accept(transfer_sockfd, (struct sockaddr*)&cli_addr, &clilen);
    if (data_sock < 0) {
        close(transfer_sockfd);
        return;
    }

    std::ofstream outfile(filepath, std::ios::binary);
    if (!outfile.is_open()) {
        close(data_sock);
        close(transfer_sockfd);
        return;
    }

    size_t remaining = filesize;
    char buffer[4096];

    while (remaining > 0) {
        size_t to_read = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        if (!recv_raw_data(data_sock, buffer, to_read)) break;
        outfile.write(buffer, to_read);
        remaining -= to_read;
    }

    outfile.close();
    close(data_sock);
    close(transfer_sockfd);

    std::cout << "[System] File saved: " << filepath << std::endl;
}

void handle_file_download_connection(int transfer_sockfd, std::string filepath) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    struct timeval tv;
    tv.tv_sec  = 10;
    tv.tv_usec = 0;
    setsockopt(transfer_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    int data_sock = accept(transfer_sockfd, (struct sockaddr*)&cli_addr, &clilen);
    if (data_sock < 0) {
        std::cerr << "[Error] Download accept timeout." << std::endl;
        close(transfer_sockfd);
        return;
    }

    std::ifstream infile(filepath, std::ios::binary);
    if (!infile.is_open()) {
        close(data_sock);
        close(transfer_sockfd);
        return;
    }

    char buffer[4096];
    while (infile.read(buffer, sizeof(buffer)) || infile.gcount() > 0) {
        if (!send_raw_data(data_sock, buffer, infile.gcount())) break;
        if (infile.eof()) break;
    }

    infile.close();
    close(data_sock);
    close(transfer_sockfd);

    std::cout << "[System] File sent: " << filepath << std::endl;
}

long get_file_size_force(const std::string& path) {
    struct stat stat_buf;
    int rc = stat(path.c_str(), &stat_buf);
    if (rc == 0) {
        return stat_buf.st_size;
    } else {
        return -1;
    }
}

void handle_client_message(int sockfd) {
    std::string req_str;
    if (!recv_message(sockfd, req_str)) {
        std::cout << "Socket " << sockfd << " disconnected." << std::endl;

        ClientInfo& info = clients[sockfd];
        if (info.room_id != -1) {
            int rid = info.room_id;
            int ret = room_mgr.leave_room(rid, info.username);

            json notify;
            if (ret == 1) {
                notify["action"] = "room_disbanded";
            } else {
                notify["action"]   = "player_left";
                notify["username"] = info.username;
                notify["data"]     = room_mgr.get_room_info(rid);
            }

            for (auto& [id, c] : clients) {
                if (c.room_id == rid && id != sockfd) {
                    send_message(id, notify.dump());
                    if (ret == 1) {
                        c.state   = ClientState::LOGGED_IN;
                        c.room_id = -1;
                    }
                }
            }
        }

        close(sockfd);
        FD_CLR(sockfd, &master_fds);
        clients.erase(sockfd);
        return;
    }

    json req;
    try {
        req = json::parse(req_str);
    } catch (...) {
        return;
    }

    json res;
    std::string action = req.value("action", "");
    ClientInfo& client = clients[sockfd];

    std::cout << "[Req] " 
              << (client.username.empty() ? "Guest" : client.username)
              << ": " << action << std::endl;

    if (action == "register") {
        std::string role = req.value("role", "player"); 
        
        if (db.register_user(req["username"], req["password"], role)) {
            res = {{"status", "ok"}, {"message", "Registration successful"}};
        } else {
            res = {{"status", "error"}, {"message", "Username already exists"}};
        }
        send_message(sockfd, res.dump());
    }
    else if (action == "login") {
        std::string target_user = req["username"];
        bool already_online = false;

        for (const auto& [id, info] : clients) {
            if (info.username == target_user &&
                info.state != ClientState::CONNECTED) {
                already_online = true;
                break;
            }
        }

        if (already_online) {
            res = {{"status", "error"}, {"message", "User is already logged in."}};
        } else {
            std::string role;
            if (db.login_user(target_user, req["password"], role)) {
                client.state    = ClientState::LOGGED_IN;
                client.username = target_user;
                client.role     = role;
                res = {{"status", "ok"}, {"role", role}};
            } else {
                res = {{"status", "error"}, {"message", "Invalid username or password"}};
            }
        }
        send_message(sockfd, res.dump());
    }
    else if (action == "upload_request") {
        std::string game_name = req["gamename"];
        bool is_new_game = req.value("is_new_game", false);
        
        std::string owner = db.get_game_owner(game_name);
        
        if (is_new_game) {
            if (!owner.empty()) {
                std::string msg;
                if (owner == client.username) {
                    msg = "Failed: You already have a game named '" + game_name + "'. Please use 'Update Game'.";
                } else {
                    msg = "Failed: Game name '" + game_name + "' is already taken by another developer.";
                }
                res = {{"status", "error"}, {"message", msg}};
                send_message(sockfd, res.dump());
                return;
            }
        } else {
            if (owner.empty()) {
                res = {{"status", "error"}, {"message", "Failed: Game '" + game_name + "' does not exist."}};
                send_message(sockfd, res.dump());
                return;
            }
            if (owner != client.username) {
                res = {{"status", "error"}, {"message", "Failed: Permission Denied. You do not own this game."}};
                send_message(sockfd, res.dump());
                return;
            }
        }

        std::string filename = req["filename"];
        size_t filesize      = req["filesize"];
        std::string ver      = req.value("version", "1.0");
        std::string type     = req.value("game_type", "CLI"); 
        int max_p            = req.value("max_players", 2);          

        int transfer_sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = {0};
        sa.sin_family      = AF_INET;
        sa.sin_addr.s_addr = INADDR_ANY;

        bind(transfer_sock, (struct sockaddr*)&sa, sizeof(sa));
        listen(transfer_sock, 1);

        socklen_t len = sizeof(sa);
        getsockname(transfer_sock, (struct sockaddr*)&sa, &len);
        int port = ntohs(sa.sin_port); 

        std::string save_path = "server/uploaded_games/" + filename;
        
        std::thread(handle_file_upload_connection, transfer_sock, save_path, filesize).detach();

        db.upsert_game(
            client.username,
            game_name,
            req.value("description", ""),
            filename,
            ver,
            type,   
            max_p   
        );

        res = {{"status", "ok"}, {"port", port}};
        send_message(sockfd, res.dump());
    }
    else if (action == "download_request") {
        std::string gamename = req["gamename"];
        std::string filename = db.get_game_filename(gamename);
        
        std::cout << "[Debug] Download Request for Game: " << gamename << " -> Filename: " << filename << std::endl;

        if (filename.empty()) {
            res = {{"status", "error"}, {"message", "Game not found in DB"}};
        } else {
            std::string filepath = "server/uploaded_games/" + filename;
            long fsize = get_file_size_force(filepath);
            
            if (fsize < 0) {
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    std::cout << "[Error] File missing at: " << cwd << "/" << filepath << std::endl;
                }
                res = {{"status", "error"}, {"message", "File missing on server"}};
            } else {
                db.record_download(gamename, client.username);
                int transfer_sock = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in sa = {0}; sa.sin_family=AF_INET; sa.sin_addr.s_addr=INADDR_ANY;
                
                sa.sin_port = 0; 
                
                bind(transfer_sock, (struct sockaddr*)&sa, sizeof(sa));
                listen(transfer_sock, 1);
                
                socklen_t len = sizeof(sa); 
                getsockname(transfer_sock, (struct sockaddr*)&sa, &len);
                int port = ntohs(sa.sin_port);

                std::cout << "[System] Ready to send " << filename << " (" << fsize << " bytes) on port " << port << std::endl;
                std::thread(handle_file_download_connection, transfer_sock, filepath).detach();
                
                res = {{"status", "ok"}, {"port", port}, {"filesize", fsize}, {"filename", filename}};
            }
        }
        send_message(sockfd, res.dump());
    }
    else if (action == "delete_game") {
        std::string game_name = req["gamename"];

        if (room_mgr.is_game_active(game_name)) {
            res = {
                {"status", "error"},
                {"message", "Failed: Game is currently active in a room. Please wait for matches to finish."}
            };
        } else {
            std::string filename = db.delete_game(client.username, game_name);

            if (!filename.empty()) {
                std::string filepath = "server/uploaded_games/" + filename;
                remove(filepath.c_str());

                res = {{"status", "ok"}, {"message", "Game deleted successfully"}};
                std::cout << "[System] Deleted game file: " << filepath << std::endl;
            } else {
                res = {
                    {"status", "error"},
                    {"message", "Permission Denied: You do not own this game or it does not exist."}
                };
            }
        }

        send_message(sockfd, res.dump());
    }
    else if (action == "list_games") {
        res = {{"status", "ok"}, {"data", db.get_games()}};
        send_message(sockfd, res.dump());
    }
    else if (action == "create_room") {
        std::string rname = req["room_name"];
        std::string gname = req["game_name"];

        if (db.get_game_filename(gname).empty()) {
            res = {{"status", "error"}, {"message", "Game not found"}};
        } else {
            int max_players = db.get_game_max_players(gname);
            int rid = room_mgr.create_room(rname, client.username, gname, max_players);
            
            client.state   = ClientState::IN_ROOM;
            client.room_id = rid;

            res = {
                {"status", "ok"},
                {"room_id", rid},
                {"data", room_mgr.get_room_info(rid)}
            };
        }

        send_message(sockfd, res.dump());
    }
    else if (action == "list_rooms") {
        res = {{"status", "ok"}, {"data", room_mgr.list_rooms()}};
        send_message(sockfd, res.dump());
    }
    else if (action == "list_players") {
        std::vector<std::string> player_names;
        for (const auto& [id, info] : clients) {
            if (!info.username.empty() && info.role == "player") {
                player_names.push_back(info.username);
            }
        }
        res = {{"status", "ok"}, {"data", player_names}};
        send_message(sockfd, res.dump());
    }
    else if (action == "join_room") {
        int rid = req["room_id"];

        if (room_mgr.join_room(rid, client.username)) {
            client.state   = ClientState::IN_ROOM;
            client.room_id = rid;

            res = {{"status", "ok"}, {"message", "Joined"}, {"data", room_mgr.get_room_info(rid)}};

            json notify;
            notify["action"]   = "player_joined";
            notify["username"] = client.username;
            notify["data"]     = room_mgr.get_room_info(rid);

            for (auto& [id, info] : clients) {
                if (info.room_id == rid && id != sockfd) {
                    send_message(id, notify.dump());
                }
            }
        } else {
            res = {{"status", "error"}, {"message", "Cannot join (Room full or playing)"}};
        }

        send_message(sockfd, res.dump());
    }
    else if (action == "leave_room") {
        if (client.room_id != -1) {
            int rid = client.room_id;
            int ret = room_mgr.leave_room(rid, client.username);

            json notify;
            if (ret == 1) {
                notify["action"] = "room_disbanded";
            } else {
                notify["action"]   = "player_left";
                notify["username"] = client.username;
                notify["data"]     = room_mgr.get_room_info(rid);
            }

            for (auto& [id, info] : clients) {
                if (info.room_id == rid && id != sockfd) {
                    send_message(id, notify.dump());
                    if (ret == 1) {
                        info.state   = ClientState::LOGGED_IN;
                        info.room_id = -1;
                    }
                }
            }

            client.state   = ClientState::LOGGED_IN;
            client.room_id = -1;

            res = {{"status", "ok"}};
        }

        send_message(sockfd, res.dump());
    }
    else if (action == "start_game") {
        if (client.room_id != -1) {
            json info = room_mgr.get_room_info(client.room_id);

            if (info["host"] == client.username) {
                if (!room_mgr.is_room_full(client.room_id)) {
                    res = {
                        {"status", "error"}, 
                        {"message", "Cannot start: Room is not full yet."}
                    };
                    send_message(sockfd, res.dump());
                } else {
                    std::string filename = db.get_game_filename(info["game"]);
                    int game_port = 14010 + client.room_id;

                    pid_t pid = fork();
                    if (pid == 0) {
                        std::string path = "server/uploaded_games/" + filename;
                        std::string port_str = std::to_string(game_port);
                        execlp("python3", "python3", path.c_str(), "--server", port_str.c_str(), NULL);
                        exit(1);
                    }

                    room_mgr.start_game(client.room_id, game_port);

                    json broadcast;
                    broadcast["action"]    = "game_start";
                    broadcast["game_port"] = game_port;
                    broadcast["filename"]  = filename;

                    for (auto& [id, c] : clients) {
                        if (c.room_id == client.room_id) {
                            send_message(id, broadcast.dump());
                        }
                    }
                }
            }
        }
    }
    else if (action == "finish_game") {
        if (client.room_id != -1) {
            json info = room_mgr.get_room_info(client.room_id);

            if (info["host"] == client.username) {
                room_mgr.finish_game(client.room_id);
                std::string gname = info["game"];
                for (const auto& p : info["players"]) {
                    db.record_play_history(p.get<std::string>(), gname);
                }

                json notify;
                notify["action"] = "room_reset";
                notify["data"]   = room_mgr.get_room_info(client.room_id);

                for (auto& [id, c] : clients) {
                    if (c.room_id == client.room_id) {
                        send_message(id, notify.dump());
                    }
                }
            }
        }
    }
    else if (action == "add_comment") {
        std::string gname = req["game_name"];
        int score = req["score"];
        std::string content = req["content"];
        if (!db.has_played(client.username, gname)) {
            res = {
                {"status", "error"}, 
                {"message", "You must play this game before rating it!"}
            };
        } else {
            if (db.add_comment(gname, client.username, score, content)) {
                res = {{"status", "ok"}, {"message", "Comment added successfully"}};
            } else {
                res = {{"status", "error"}, {"message", "You have already rated this game or game not found."}};
            }
        }
        send_message(sockfd, res.dump());
    }
    else if (action == "logout") {
        if (client.room_id != -1) {
            room_mgr.leave_room(client.room_id, client.username);
        }

        client.state    = ClientState::CONNECTED;
        client.username = "";
        client.room_id  = -1;

        res = {{"status", "ok"}};
        send_message(sockfd, res.dump());
    }
}

int main() {
    signal(SIGCHLD, handle_sigchld);
    ensure_directory_exists("server/uploaded_games");

    int listener = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }

    listen(listener, 10);

    FD_ZERO(&master_fds);
    FD_SET(listener, &master_fds);
    fdmax = listener;

    std::cout << "Lobby Server (Full Features) running on " << SERVER_PORT << std::endl;

    while (true) {
        fd_set read_fds = master_fds;

        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == listener) {
                    struct sockaddr_in cli_addr;
                    socklen_t len = sizeof(cli_addr);

                    int newfd = accept(listener, (struct sockaddr*)&cli_addr, &len);
                    if (newfd >= 0) {
                        FD_SET(newfd, &master_fds);
                        if (newfd > fdmax) fdmax = newfd;

                        clients[newfd] = ClientInfo();
                        std::cout << "New connection: " << newfd << std::endl;
                    }
                } else {
                    handle_client_message(i);
                }
            }
        }
    }

    return 0;
}