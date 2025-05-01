#include "common.h"
#include <string.h>

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