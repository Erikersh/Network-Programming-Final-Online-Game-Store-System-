#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    void usleep(int64_t usec);
    #define close closesocket
#else
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
#endif

#define MAX_MSG_SIZE 65536

bool init_socket_env();
void clean_socket_env();

bool send_message(int sockfd, const std::string& message);
bool recv_message(int sockfd, std::string& message);

bool send_raw_data(int sockfd, const char* data, size_t length);
bool recv_raw_data(int sockfd, char* buffer, size_t length);

long get_file_size(const std::string& filename);