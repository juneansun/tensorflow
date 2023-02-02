#include <android/log.h>

#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define  LOGW(...)  __android_log_print(ANDROID_LOG_WARN,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define DEBUGGING LOGD("%s:%d", __FILE__, __LINE__);

#define DEFAULT_SOCKET_NAME "/data/local/tmp/sched.sock"

void initializeSocket(const char *name);
void terminate(void);
void handleError(const char *msg);
int  read_data();
void write_data(int data);
