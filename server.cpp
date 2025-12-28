#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <string>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace std;

const int THREAD_COUNT = 4;

queue<int> taskQueue;
mutex queueMutex;
condition_variable cv;

void worker() {
    while (true) {
        int client_fd;

        unique_lock<mutex> lock(queueMutex);
        cv.wait(lock, [] {
            return !taskQueue.empty();
        });

        client_fd = taskQueue.front();
        taskQueue.pop();
        lock.unlock();

        char buffer[1024] = {0};
        ssize_t n = read(client_fd, buffer, sizeof(buffer));
        if (n <= 0) {
            close(client_fd);
            continue;
        }

        string request(buffer);
        string path = "/";

        size_t pos1 = request.find(" ");
        size_t pos2 = request.find(" ", pos1 + 1);

        if (pos1 != string::npos && pos2 != string::npos) {
            path = request.substr(pos1 + 1, pos2 - pos1 - 1);
        }

        if (path == "/") {
            path = "/index.html";
        }

        string filePath = "www" + path;
        int fd = open(filePath.c_str(), O_RDONLY);

        if (fd < 0) {
            const char* notFound =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 13\r\n"
                "\r\n"
                "404 Not Found";

            write(client_fd, notFound, strlen(notFound));
            close(client_fd);
            continue;
        }

        struct stat st;
        if (stat(filePath.c_str(), &st) < 0) {
            close(fd);
            close(client_fd);
            continue;
        }

        string header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: " + to_string(st.st_size) + "\r\n"
            "Content-Type: text/html\r\n"
            "\r\n";

        write(client_fd, header.c_str(), header.size());

        char fileBuf[4096];
        ssize_t bytes;

        while ((bytes = read(fd, fileBuf, sizeof(fileBuf))) > 0) {
            write(client_fd, fileBuf, bytes);
        }

        close(fd);
        close(client_fd);
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    ::bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 128);

    vector<thread> workers;
    for (int i = 0; i < THREAD_COUNT; i++) {
        workers.emplace_back(worker);
    }

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        {
            lock_guard<mutex> lock(queueMutex);
            taskQueue.push(client_fd);
        }
        cv.notify_one();
    }
}