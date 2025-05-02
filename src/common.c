#include "common.h"
#include <sys/stat.h>
#include <sys/types.h>


// 向所有在线客户端广播消息
void broadcast_message(server_ctx_t *ctx, const char *msg) {
    pthread_mutex_lock(&ctx->lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ctx->clients[i]) {
            send(ctx->clients[i]->sockfd, msg, strlen(msg), 0);
        }
    }

    pthread_mutex_unlock(&ctx->lock);
}

int register_client(server_ctx_t *ctx, client_t *c) {
    pthread_mutex_lock(&ctx->lock);

    // 判断聊天室满没满
    if (ctx->count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    // 判断昵称是否重复
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (ctx->clients[i] && strcmp(ctx->clients[i]->name, c->name) == 0) {
            pthread_mutex_unlock(&ctx->lock);
            return -2;
        }
    }
    // 注册
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!ctx->clients[i]) {
            ctx->clients[i] = c;
            ctx->count++;
            break;
        }
    }

    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

void unregister_client(server_ctx_t *ctx, client_t *c) {
    pthread_mutex_lock(&ctx->lock);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (ctx->clients[i] == c) {
            ctx->clients[i] = NULL;
            ctx->count--;
            break;
        }
    }

    pthread_mutex_unlock(&ctx->lock);
}

int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0; // 已存在目录
    /* 0755: drwxr-xr-x */
    return mkdir(path, 0755); // 仅创建最后一级；需要递归可自行改进
}