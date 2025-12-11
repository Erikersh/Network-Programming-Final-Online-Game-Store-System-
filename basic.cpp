#include "basic.hpp"
#include <fstream>
#include <thread>
#include <chrono>

#ifdef _WIN32
void usleep(int64_t usec) {
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}
#endif

bool init_socket_env() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }
#endif
    return true;
}

void clean_socket_env() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool write_all(int sockfd, const void* buffer, size_t len) {
    const char* p = (const char*)buffer;
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sockfd, p + total_sent, len - total_sent, 0);
        if (sent <= 0) return false;
        total_sent += sent;
    }
    return true;
}

bool read_all(int sockfd, void* buffer, size_t len) {
    char* p = (char*)buffer;
    size_t total_received = 0;
    while (total_received < len) {
        ssize_t received = recv(sockfd, p + total_received, len - total_received, 0);
        if (received <= 0) return false;
        total_received += received;
    }
    return true;
}

bool send_message(int sockfd, const std::string& message) {
    if (message.length() > MAX_MSG_SIZE) return false;
    uint32_t len = message.length();
    uint32_t net_len = htonl(len);
    if (!write_all(sockfd, &net_len, sizeof(net_len))) return false;
    if (!write_all(sockfd, message.c_str(), len)) return false;
    return true;
}

bool recv_message(int sockfd, std::string& message) {
    uint32_t net_len;
    if (!read_all(sockfd, &net_len, sizeof(net_len))) return false;
    uint32_t len = ntohl(net_len);
    if (len == 0 || len > MAX_MSG_SIZE) return false;
    std::vector<char> buffer(len);
    if (!read_all(sockfd, buffer.data(), len)) return false;
    message.assign(buffer.begin(), buffer.end());
    return true;
}

bool send_raw_data(int sockfd, const char* data, size_t length) {
    return write_all(sockfd, data, length);
}

bool recv_raw_data(int sockfd, char* buffer, size_t length) {
    return read_all(sockfd, buffer, length);
}

long get_file_size(const std::string& filename) {
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}