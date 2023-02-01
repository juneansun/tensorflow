
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


volatile sig_atomic_t canLoop = 1;

int udsfd = -1;
sockaddr_un addr = { 0 };

// UNIXドメインソケットの初期化
void initializeSocket(const char *name) {
    udsfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (udsfd < 0) handleError("socket");

    memset(&addr, 0, sizeof(sockaddr_un));
    addr.sun_family = AF_LOCAL;
    // 抽象名前空間につきsun_path[0]は\0
    // さらにconnectに渡すlengthはsun_pathの実質的な終端(\0含まない)までとする必要がある
    strncpy(addr.sun_path + 1, name, 64);
    int addrlen = sizeof(sa_family_t) + strlen(name) + 1;
    if (connect(udsfd, (sockaddr*)&addr, addrlen) < 0) handleError("connect");
}

// 終了処理
void terminate(void) {
    if (udsfd >= 0) close(udsfd);
}

// エラー処理
void handleError(const char *msg) {
    perror(msg);
    terminate();
    exit(EXIT_FAILURE);
}

// 割り込み処理
// void signalHandler(int signal) {
//    canLoop = 0;
// }

// int main(int argc, char** argv) { return tflite::benchmark::Main(argc, argv); }
