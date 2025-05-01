#include <pthread.h>
#include <sys/socket.h>
#define MAX_NAME_LEN 33
#define MAX_CLIENTS 100
#define SERVER_PORT 8888
#define MAX_MSG_LEN 512
#define EXIT_CMD    "//exit"

// 单个客户端状态
typedef struct {
    int       sockfd;
    char      name[MAX_NAME_LEN];
    pthread_t tid;
} client_t;

// 服务器状态
typedef struct {
    client_t *clients[MAX_CLIENTS];
    int       count;
    pthread_mutex_t lock;
} server_ctx_t;

// 注册、注销客户端接口
int  register_client(server_ctx_t *ctx, client_t *c);
void unregister_client(server_ctx_t *ctx, client_t *c);
void broadcast_message(server_ctx_t *ctx, const char *msg);