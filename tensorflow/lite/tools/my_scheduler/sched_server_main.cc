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

enum MODEL {
    TFLITE_MOBILE,      // 0
    TFLITE_INCEPTION,
    TFLITE_BERT,
    TFLITE_EFFICIENT,
    TFLITE_RESIDUAL,
    TFLITE_YAM,
    NUM_TFLITE_MODEL,
};

enum PROCESOR_TYPE {
    CPU_TYPE,
    GPU_TYPE,
    HEXAGON_TYPE,
    TPU_TYPE,
    NUM_PROCESSOR_TYPE,
};

typedef struct Message_ {
    int clfd;
    int model_idx;
    uint64_t release_time;
    int pid;
} MESSAGE;
static std::queue<MESSAGE>message_queue;

static int check_client[4097] = { 0, };

static bool notify_flag = false;

static int processorPerClients[1024];

static int profiles[NUM_TFLITE_MODEL][NUM_PROCESSOR_TYPE][10] = {
    {   // MoblieNet
        { 85168,    99999,    99999,    99999, 99999, 99999, 99999 }, // CPU
        { 11189,    12199,    14996,    20679, 25195, 30084, 39789 }, // GPU
        { 3510,     5657,     8430,     11281, 16960, 19773 }, // Hexagon
        { 2872,     6529,     9793,     13072, 16349, 19631, 22874, 26166 } // TPU
    },
    {   // InceptionNet
        { 564661,   616192,   641378,   680000,   720000,  760000 }, // CPU
        { 134514,   260589,   384482,   518373,   655177,  782517,  910000,  1040000 }, // GPU
        { 18006,    31939,    50308,    67031,    85702,   104356,  119000,  136000 }, // Hexagon
        { 24639,    42326,    60307,    80296,    100000,  120000,  140000,  160000 } // TPU
    },
    {   // BERT - TPU not available
        { 2420000,  0xffffff, 641378,   680000,   720000,     760000,     0xffffff,   0xffffff }, // CPU
        { 339236,   0xffffff, 0xffffff, 0xffffff, 0xffffff,   0xffffff,   0xffffff,   0xffffff }, // GPU
        { 204426,   0xffffff, 50308,    67031,    85702,      104356,     119000,     136000 }, // Hexagon
        { 2260000,  0xffffff, 60307,    80296,    100000,     120000,     140000,     160000 } // TPU
    },
    {   // EfficientNet
        { 178528,   0xffffff, 641378,   680000,  720000,  760000 }, // CPU
        { 11056,    0xffffff, 384482,   518373,  655177,  782517,  910000,  1040000 }, // GPU
        { 4884,     0xffffff, 50308,    67031,   85702,   104356,  119000,  136000 }, // Hexagon
        { 7888,     0xffffff, 60307,    80296,   100000,  120000,  140000,  160000 } // TPU
    },
    {   // ResNet - TPU not available
        { 508629,   0xffffff, 641378,   680000,  720000,  760000 }, // CPU
        { 74594,    0xffffff, 384482,   518373,  655177,  782517,  910000,  1040000 }, // GPU
        { 22115,    0xffffff, 50308,    67031,   85702,   104356,  119000,  136000 }, // Hexagon
        { 0xffffff, 0xffffff, 60307,    80296,   100000,  120000,  140000,  160000 } // TPU
    },
    {   // YamNet - TPU not available
        { 17839,    0xffffff, 641378,   680000,  720000,  760000 }, // CPU
        { 6145,     0xffffff, 384482,   518373,  655177,  782517,  910000,  1040000 }, // GPU
        { 6678,     0xffffff, 50308,    67031,   85702,   104356,  119000,  136000 }, // Hexagon
        { 11769,    0xffffff, 60307,    80296,   100000,  120000,  140000,  160000 } // TPU
    },
};

static int model_adder[2][4] = {
    { 100000, 3000, 2200, 2800 },
    { 50000, 130000, 20000, 20000 }
};


static int processor_apps[4] = { 0, };

class Processor {
    public:
        Processor(int pID): processorID(pID) {
            mNumClient[0] = mNumClient[1] = 0;

#if 0 // DEBUG
            LOGD("(JBD) processor: %d, mobile[%d], inception[%d]", processorID, mNumClient[0], mNumClient[1]);
#endif
        };
        ~Processor() {};

        int getNumClient() {
            return mNumClient[TFLITE_MOBILE] +
                mNumClient[TFLITE_INCEPTION] +
                mNumClient[TFLITE_BERT] +
                mNumClient[TFLITE_EFFICIENT] +
                mNumClient[TFLITE_RESIDUAL] +
                mNumClient[TFLITE_YAM];
        };

        void incClient(int model_idx) {
            mNumClient[model_idx]++;
        };

        void decClient(int model_idx) {
            mNumClient[model_idx]--;
        };

        int getWCET(int model_idx) {
            int wcet = 0;

            for(int i = 0; i < NUM_TFLITE_MODEL; i++) {
#if 0 // DEBUG
                LOGD("(JBD) mNumClient[%d]: %d", i, mNumClient[i]);
#endif
                if (mNumClient[i] > 0) {
#if 0 // DEBUG
                    LOGD("(JBD) adding :%d", profiles[i][processorID][mNumClient[i] - 1]);
#endif
                    wcet += profiles[i][processorID][mNumClient[i] - 1];
                }
            }

            return wcet + profiles[model_idx][processorID][0];
        };

    private:
        int processorID;
        int mNumClient[NUM_TFLITE_MODEL];
};

static Processor *p[4];

// XXX: code flag for experiment, max on pixel4 is 7
#define HOW_MANY_TO_START_AT_ONCE 1

// 0: CPU
// 1: GPU
// 2: Hexagon
// 3: TPU
// #define STATIC_PROCESSOR HEXAGON_TYPE
//
// #define ROUNDROBIN
//
#define GREEDY

typedef struct Packet_ {
    int request;
    int model_idx;
    uint64_t release_time;
    int pid;
}PACKET;

void resetNumClient() {
    LOGD("-------------------------- # of client: %d --------------------------", (int) std::move(num_client));
    for (int i = 0 ; i < 4097; i++) {
        check_client[i] = 0;
    }
    LOGD("disable notification until num_client is full-filled");
    notify_flag = false;
}

#define INVOKE 0
#define FINISH 1
void push_message(const epoll_event* event) {
    std::lock_guard<std::mutex> guard(g_m_message);

    PACKET pkt;
    epoll_event cl = { 0 };
    int clfd = event->data.fd;

    int state = read(clfd, &pkt, sizeof(PACKET));

    if (state <= 0 || state != sizeof(PACKET)) {
        perror("sched_server read failed");

        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        if (num_client > 0)
            num_client--;
        LOGD("# of client--: %d", (int) std::move(num_client));

        if (num_client == 0)
            resetNumClient();
        return;
    }


    switch (pkt.request)
    {
        case FINISH:// client send -1 after inference finished
        {
#ifdef GREEDY
            processor_apps[processorPerClients[clfd]]--;
            p[processorPerClients[clfd]]->decClient(pkt.model_idx);
#if 0 // DEBUG
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
#endif
#endif
            break;
        }

        case INVOKE:
        {
            // XXX: PUSH MESSAGE HERE!!!
            message_queue.push( { clfd, pkt.model_idx, pkt.release_time, pkt.pid } );


            if (check_client[clfd] == 0) {
                check_client[clfd] = 1;
                num_client++;
                LOGD("# of client++: %d", (int) std::move(num_client));
            }

            if (num_client >= HOW_MANY_TO_START_AT_ONCE) {
                notify_flag = true;
            }

            if (notify_flag) {
                cv.notify_one();
            }

            break;
        }

        default:
            LOGW("something wrong with packet");
    }
    return;
}

int pop_message(MESSAGE &msg) {
    std::lock_guard<std::mutex> guard(g_m_message);

    if (!message_queue.empty()) {
        msg = message_queue.front(); // is this copy possible?
        message_queue.pop();
        return 0;
    } else {
        return -1;
    }
}

int greedySchedule_legacy(int model_idx) {

    int candidate[4];

    std::lock_guard<std::mutex> guard(g_m_message);

    candidate[0] = profiles[model_idx][CPU_TYPE]    [ processor_apps[CPU_TYPE] ];
    candidate[1] = profiles[model_idx][GPU_TYPE]    [ processor_apps[GPU_TYPE] ];
    candidate[2] = profiles[model_idx][HEXAGON_TYPE][ processor_apps[HEXAGON_TYPE] ];
    candidate[3] = profiles[model_idx][TPU_TYPE]    [ processor_apps[TPU_TYPE]  ];

    int min_value = candidate[0];
    int min_index = 0;


    for (int index = 0; index < 4; index++) {
        if (min_value > candidate[index]) {
            min_value = candidate[index];
            min_index = index;
        }
    }

    processor_apps[min_index]++;

#if 0 // DEBUG
    LOGD("[%d] vs [%d] vs [%d] vs [%d]",
            candidate[0],
            candidate[1],
            candidate[2],
            candidate[3]);
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
#endif
    return min_index;
}


int greedySchedule(int model_idx) {

    int candidate[NUM_PROCESSOR_TYPE];

    std::lock_guard<std::mutex> guard(g_m_message);

    candidate[CPU_TYPE]     = p[CPU_TYPE]->getWCET(model_idx);
    candidate[GPU_TYPE]     = p[GPU_TYPE]->getWCET(model_idx);
    candidate[HEXAGON_TYPE] = p[HEXAGON_TYPE]->getWCET(model_idx);
    candidate[TPU_TYPE]     = p[TPU_TYPE]->getWCET(model_idx);

    int min_value = candidate[0];
    int min_index = 0;

    for (int index = 0; index < 4; index++) {
        if (min_value > candidate[index]) {
            min_value = candidate[index];
            min_index = index;
        }
    }

    p[min_index]->incClient(model_idx);

#if 0 // DEBUG
    LOGD("[%d] vs [%d] vs [%d] vs [%d]",
            candidate[0],
            candidate[1],
            candidate[2],
            candidate[3]);
    switch(min_index)
    {
        case CPU_TYPE:
            LOGD("CPU: count++[%d]", p[CPU_TYPE]->getNumClient());
            break;
        case GPU_TYPE:
            LOGD("GPU: count++[%d]", p[GPU_TYPE]->getNumClient());
            break;
        case HEXAGON_TYPE:
            LOGD("HEX: count++[%d]", p[HEXAGON_TYPE]->getNumClient());
            break;
        case TPU_TYPE:
            LOGD("TPU: count++[%d]", p[TPU_TYPE]->getNumClient());
            break;
    }
#endif
    return min_index;
}

int expectedMaxBenefit(int model_idx, uint64_t release_time) {
    int min_idx = 0;

    return min_idx;
}

void handleMessage() {

    std::unique_lock lk(g_m_loop); // this lock is for conditional variable signal wait
    // forever loop
    while(true) {
        MESSAGE msg;

        int err = pop_message(msg);
        if (err) { // message queue is empty
            cv.wait(lk); // wait until new message arrive
            continue; // woke up, continue and try message pop again
        }

#ifdef STATIC_PROCESSOR
        type = STATIC_PROCESSOR;
#else
#ifdef ROUNDROBIN
        type++;

        if (type == 4)
            type = 1;
#else
#ifdef GREEDY
        type = greedySchedule(msg.model_idx);
#endif // GREEDY
#endif // ROUNDROBIN
#endif // STATIC_PROCESSPR
        processorPerClients[msg.clfd] = type;

        write(msg.clfd, &type, sizeof(int));
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

    LOGD("client[%3d]: delegate[%2d]", clfd, type);
    write(clfd, &type, sizeof(int));
    type++;

    if (type == 4)
        type = 1;

    return;
}
#endif

void initializeSocket(const char *name) {
    D_FUNC;
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
    std::thread t2(handleMessage);
    std::thread t3(handleMessage);
    std::thread t4(handleMessage);

    p[0] = new Processor(CPU_TYPE);
    p[1] = new Processor(GPU_TYPE);
    p[2] = new Processor(HEXAGON_TYPE);
    p[3] = new Processor(TPU_TYPE);

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
    t2.join();
    t3.join();
    t4.join();
    return EXIT_SUCCESS;
}
}  // namespace benchmark
}  // namespace tflite

int main(int argc, char** argv) { return tflite::benchmark::Main(argc, argv); }
