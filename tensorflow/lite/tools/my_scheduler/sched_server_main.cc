#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <memory.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "sched_logger.h"

#define MAX_EVENTS 32
#define LOG_TAG "sched_server"

namespace tflite {
namespace benchmark {

void initializeSocket(const char *name);
void initializeEpoll(void);
void processNewConnection(const epoll_event *event);
void processExistConnection(const epoll_event *event);
void setNonblocking(int fd);
void terminate(void);
void handleError(const char *msg);

static int epfd = -1;
static epoll_event endpoint = { 0, };
static epoll_event events[MAX_EVENTS];

static int udsfd = -1;
static sockaddr_un addr = { 0 };

static std::atomic<int> type(0);
static std::atomic<int> num_client(0);

static std::mutex g_m_loop;
static std::mutex g_m_message;
static std::condition_variable cv;
static std::queue<int>message_queue;

static int check_client[4097] = { 0, };

static bool notify_flag = false;

static int processorPerClients[1024];

static int profiles[4][10] =        {{ 85168,    99999,  99999,  99999, 99999, 99999, 99999 },
                                { 11989,    12199,  14996,  20679, 25195, 30084, 39789 },
                                { 3510,     5657,   8430,   11281, 16960, 19773 },
                                { 2872,     6529,   9793,   13072, 16349, 19631, 22874, 26166 }};

static int processor_apps[4] = { 0, };

// XXX: code flag for experiment
#define HOW_MANY_TO_START_AT_ONCE 4

#define CPU_TYPE 0
#define GPU_TYPE 1
#define HEXAGON_TYPE 2
#define TPU_TYPE 3
#define OTHER_TYPE 4
// 0: CPU
// 1: GPU
// 2: Hexagon
// 3: TPU
// #define STATIC_PROCESSOR 0
#undef STATIC_PROCESSOR
//#define ROUNDROBIN


void resetNumClient() {
    LOGD("(JBD) ---- #of client: %d ----", (int) std::move(num_client));
    for (int i = 0 ; i < 4097; i++) {
        check_client[i] = 0;
    }
    LOGD("(JBD) disable notification until num_client is full-filled");
    notify_flag = false;
}

void push_message(const epoll_event* event) {
    LOGD("(JBD) %s:%d, entered <<<<< push_message", __func__, __LINE__);
    std::lock_guard<std::mutex> guard(g_m_message);

    int pid = 0;
    epoll_event cl = { 0 };
    int clfd = event->data.fd;

    int state = read(clfd, &pid, sizeof(int));
    if (state < 0 || state != sizeof(int)) {
        perror("read");
        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        LOGD("(JBD) Connection from %3d closed.\n", clfd);
        num_client--;
        LOGD("(JBD) #of client: %d", (int) std::move(num_client));

        if (num_client == 0)
            resetNumClient();
        return;
    }
    if (state == 0) {
        // socket is broken
        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        LOGD("(JBD) Connection from %3d closed.\n", clfd);
        num_client--;
        LOGD("(JBD) #of client: %d", (int) std::move(num_client));
        if (num_client == 0)
            resetNumClient();
        return;
    }

    if (pid < 0) {
        // Inference finished
        int type = processorPerClients[clfd];

        processor_apps[type]--;

    switch(type)
    {
        case CPU_TYPE:
            LOGD("CPU: count--[%d]", processor_apps[CPU_TYPE]);
            break;
        case GPU_TYPE:
            LOGD("GPU: count--[%d]", processor_apps[GPU_TYPE]);
            break;
        case HEXAGON_TYPE:
            LOGD("HEX: count--[%d]", processor_apps[HEXAGON_TYPE]);
            break;
        case TPU_TYPE:
            LOGD("TPU: count--[%d]", processor_apps[TPU_TYPE]);
            break;
    }

        return;
    }

    LOGD("(JBD) %s:%d, push message!!", __func__, __LINE__);
    message_queue.push(clfd);


    if (check_client[clfd] == 0) {
        check_client[clfd] = 1;
        num_client++;
        LOGD("(JBD) #of client: %d", (int) std::move(num_client));
    }

    if (num_client >= HOW_MANY_TO_START_AT_ONCE) {
        notify_flag = true;
    }

    if (notify_flag) {
        cv.notify_one();
    }

    LOGD("(JBD) %s:%d, leave >>>>>> push_message", __func__, __LINE__);

    return;
}

int pop_message() {
    LOGD("\t\t\t\t\t\t\t(JBD) %s:%d, entered pop_message", __func__, __LINE__);
    std::lock_guard<std::mutex> guard(g_m_message);

    if (!message_queue.empty()) {
        int msg = message_queue.front();
        LOGD("\t\t\t\t\t\t\t(JBD) %s:%d, pop message!!", __func__, __LINE__);
        message_queue.pop();
        return msg;
    } else {
        LOGD("\t\t\t\t\t\t\t(JBD) %s:%d, queue is empty", __func__, __LINE__);
        return -1;
    }
}

int findBestProcessor() {

    int candidate[4];
    int min_value = candidate[0];
    int min_index = 0;

    std::lock_guard<std::mutex> guard(g_m_message);

    candidate[0] = profiles[CPU_TYPE]       [ processor_apps[CPU_TYPE] ];
    candidate[1] = profiles[GPU_TYPE]       [ processor_apps[GPU_TYPE] ];
    candidate[2] = profiles[HEXAGON_TYPE]   [ processor_apps[HEXAGON_TYPE] ];
    candidate[3] = profiles[TPU_TYPE]       [ processor_apps[TPU_TYPE]  ];

    LOGD("[%d] vs [%d] vs [%d] vs [%d]",
            candidate[0],
            candidate[1],
            candidate[2],
            candidate[3]);

    for (int index = 0; index < 4; index++) {
        if (min_value > candidate[index]) {
            min_value = candidate[index];
            min_index = index;
        }
    }

    processor_apps[min_index]++;

    switch(min_index)
    {
        case CPU_TYPE:
            LOGD("CPU: count++[%d]", processor_apps[CPU_TYPE]);
            break;
        case GPU_TYPE:
            LOGD("GPU: count++[%d]", processor_apps[GPU_TYPE]);
            break;
        case HEXAGON_TYPE:
            LOGD("HEX: count++[%d]", processor_apps[HEXAGON_TYPE]);
            break;
        case TPU_TYPE:
            LOGD("TPU: count++[%d]", processor_apps[TPU_TYPE]);
            break;
    }
    return min_index;
}

void handleMessage() {

    std::unique_lock lk(g_m_loop); // this lock is for conditional variable signal wait
    // pop message
    while(true) {
        int clfd = pop_message();

        if (clfd < 0) {
            LOGD("\t\t\t\t\t\t\t(JBD) %s:%d, message NOT popped fd", __func__, __LINE__, clfd);
            LOGD("\t\t\t\t\t\t\t(JBD) %s:%d, wait for push notification", __func__, __LINE__);

            cv.wait(lk); // wait until new message arrive

            continue;
        } else {
            LOGD("\t\t\t\t\t\t\t(JBD) %s:%d, message poped", __func__, __LINE__);
        }

        LOGD("\t\t\t\t\t\t\t(JBD) client[%3d]: delegate[%2d]", clfd, (int) std::move(type));

#ifdef STATIC_PROCESSOR
        type = STATIC_PROCESSOR;
#else

#ifdef ROUNDROBIN
        type++;

        if (type == 4)
            type = 1;
#else
        type = findBestProcessor();
#endif // RaR? or optimal?
#endif // static?
        processorPerClients[clfd] = type;

        write(clfd, &type, sizeof(int));

    }
    return;
}

void processNewConnection(const epoll_event *event) {
    socklen_t size = sizeof(sockaddr_un);
    sockaddr_un client = { 0 };
    int clfd = accept(udsfd, (sockaddr*)&client, &size);
    if (clfd < 0) handleError("accept");

    epoll_event newcl = { 0 };
    newcl.events = EPOLLIN;
    newcl.data.fd = clfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, clfd, &newcl);
    LOGD("Connection from 0x%08X established.\n", clfd);
}

#if 0
int type = 1;
void processSchedConnection(const epoll_event *event) {
    int pid = 0;
    epoll_event cl = { 0 };
    int clfd = event->data.fd;

    int state = read(clfd, &pid, sizeof(int));
    if (state < 0 || state != sizeof(int)) {
        perror("read");
        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        LOGD("Connection from 0x%08X closed.\n", clfd);
        return;
    }
    if (state == 0) {
        // socket is broken
        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        LOGD("Connection from 0x%08X closed.\n", clfd);
        return;
    }

    LOGD("(JBD) client[%3d]: delegate[%2d]", clfd, type);
    write(clfd, &type, sizeof(int));
    type++;

    if (type == 4)
        type = 1;

    return;
}
#endif

void initializeSocket(const char *name) {
    DEBUGGING;
    udsfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (udsfd < 0) handleError("socket");

    memset(&addr, 0, sizeof(sockaddr_un));
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path + 1, name, 64);
    int addrlen = sizeof(sa_family_t) + strlen(name) + 1;
    if (bind(udsfd, (sockaddr*)&addr, addrlen) < 0) handleError("bind");
    if (listen(udsfd, MAX_EVENTS) < 0) handleError("listen");
}

void initializeEpoll(void) {
    epfd = epoll_create(MAX_EVENTS);
    if (epfd < 0) handleError("epoll_create");

    memset(&endpoint, 0, sizeof(epoll_event));
    endpoint.events = EPOLLIN;
    endpoint.data.fd = udsfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, udsfd, &endpoint);
}

void terminate(void) {
    if (epfd >= 0) close(epfd);
    if (udsfd >= 0) close(udsfd);
}

void handleError(const char *msg) {
    perror(msg);
    terminate();
    exit(EXIT_FAILURE);
}

// void signalHandler(int signal) {
// }
int Main(int argc, char** argv) {
    LOGW("server main");

    fprintf(stderr, "server main\n");

    num_client = 0;
    notify_flag = false;

    initializeSocket(DEFAULT_SOCKET_NAME);
    initializeEpoll();
    LOGD("epoll descriptor: 0x%016X\n", epfd);
    LOGD("socket descriptor: 0x%016X\n", udsfd);

    // if (signal(SIGINT, signalHandler) == SIG_ERR) handleError("signal");
    LOGD("Polling...(Ctrl+C to exit)\n");

    std::thread t1(handleMessage);

    while(1) {
        int epn = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (epn < 0) handleError("epoll_wait");

        for(int i = 0; i < epn; i++) {
            epoll_event *it = events + i;
            if (it->data.fd == udsfd) {
                processNewConnection(it);
            } else {
                // processSchedConnection(it);
                // push to message queue
                push_message(it);
            }
        }
    }

    terminate();
    t1.join();
    return EXIT_SUCCESS;
}
}  // namespace benchmark
}  // namespace tflite

int main(int argc, char** argv) { return tflite::benchmark::Main(argc, argv); }
