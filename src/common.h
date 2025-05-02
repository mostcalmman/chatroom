#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#define MAX_NAME_LEN    33
#define MAX_CLIENTS     100
#define SERVER_PORT     8888
#define MAX_MSG_LEN     512
#define MAX_FILES       1000
#define EXIT_CMD        "//exit"
#define CLOSE_CMD       "//close"
// 发送和保存的命令，注意含有空格
#define SEND_FILE_CMD   "//sendfile "
#define SAVE_FILE_CMD   "//savefile "
#define FILE_HDR        "[FILE]"        // 文件标识头
#define SENDFILE_HDR    "[SENDFILE]"    // 服务器回传文件标识头

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

// 文件信息
typedef struct {
    int fileid;
    char filename[128];
    char sender[MAX_NAME_LEN];
    char filepath[256];
} file_info_t;


// 注册, 注销用户
int  register_client(server_ctx_t *ctx, client_t *c);
void unregister_client(server_ctx_t *ctx, client_t *c);
// 消息广播
void broadcast_message(server_ctx_t *ctx, const char *msg);
// 确保文件目录存在
int ensure_dir(const char *path);