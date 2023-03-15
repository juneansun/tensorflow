
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <memory.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "sched_client.h"

#define LOG_TAG "\t\t\tsched_client"

typedef struct RequestMsg {
    int pid;
    int cpu_inf_time;
    int gpu_inf_time;
    int hexagon_inf_time;
    int pu_inf_time;
    int other_inf_time;
};

volatile sig_atomic_t canLoop = 1;

int udsfd = -1;
sockaddr_un addr = { 0 };

// init UNIX socket
void initializeSocket(const char *name) {
    udsfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (udsfd < 0) handleError("socket");

    memset(&addr, 0, sizeof(sockaddr_un));
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path + 1, name, 64);
    int addrlen = sizeof(sa_family_t) + strlen(name) + 1;
    if (connect(udsfd, (sockaddr*)&addr, addrlen) < 0) handleError("connect");
}

void terminate(void) {
    if (udsfd >= 0) close(udsfd);
}

void write_data(int size, void *data) {
    if (write(udsfd, data, size) < 0) {
        handleError("write");
    }
}

int read_data() {
    int data;
    int state = read(udsfd, &data, sizeof(int));
    if (state < 0 || state != sizeof(int)) {
        handleError("read");
    }
    if (state == 0) {
        handleError("sock broken");
    }

    return data;
}

void handleError(const char *msg) {
    perror(msg);
    terminate();
    exit(EXIT_FAILURE);
}

// void signalHandler(int signal) {
//    canLoop = 0;
// }

// int main(int argc, char** argv) { return tflite::benchmark::Main(argc, argv); }
