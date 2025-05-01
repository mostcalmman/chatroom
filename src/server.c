#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static server_ctx_t ctx = {
    .clients = { NULL },
    .count   = 0,
    .lock    = PTHREAD_MUTEX_INITIALIZER // 静态初始化互斥锁
};
int num = 0;


void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buf[MAX_MSG_LEN];
    char announce[MAX_MSG_LEN + MAX_NAME_LEN + 32];
    char name_buf[MAX_NAME_LEN + 2];
    int reg;
    const char *prompt = "请输入昵称(不超过32字符): \n";
    send(client->sockfd, prompt, strlen(prompt), 0);
    while(1){
        // 读取昵称并注册
        ssize_t n = recv(client->sockfd, name_buf, sizeof(name_buf)-1, 0);
        if (n <= 0) {
            close(client->sockfd);
            free(client);
            return NULL;
        }
        name_buf[n] = '\0';
        name_buf[strcspn(name_buf, "\r\n")] = '\0';


        // 长度校验
        size_t len = strlen(name_buf);
        if (len == 0 || len >= MAX_NAME_LEN) {
            const char *err = "昵称不能为空或过长，请重新输入\n";
            send(client->sockfd, err, strlen(err), 0);
            continue;  // 重新循环
        }

        // 拷贝到 client->name，并尝试注册
        strncpy(client->name, name_buf, MAX_NAME_LEN);
        // client->name[MAX_NAME_LEN-1] = '\0'; // 保证终止
        reg = register_client(&ctx, client);

        if (reg == -1) {
            // 聊天室已满
            const char *full = "聊天室已满，稍后再试\n";
            send(client->sockfd, full, strlen(full), 0);
            close(client->sockfd);
            free(client);
            return NULL;
        } else if (reg == -2) {
            // 昵称重复，提示重输
            const char *dup = "昵称重复，请重新输入\n";
            send(client->sockfd, dup, strlen(dup), 0);
            continue;  // 重新循环
        }
        // 注册成功
        num++;
        printf("[LOG] 用户“%s”已加入, fd=%d, 当前用户数: %d\n", client->name, client->sockfd,num);
        break;

    }

    // 广播加入消息
    snprintf(announce, sizeof announce, "%s 加入了聊天室, 当前用户数: %d\n", client->name, num);
    broadcast_message(&ctx, announce);

    // 消息循环
    while (1) {
        memset(buf, 0, sizeof buf);
        ssize_t n = recv(client->sockfd, buf, MAX_MSG_LEN - 1, 0);
        if (n <= 0 || strcmp(buf, EXIT_CMD) == 0) break;
        printf("[LOG] 来自用户 %s, fd=%d: %s\n", client->name,client->sockfd, buf);
        snprintf(announce, sizeof announce, "%s: %s\n", client->name, buf);
        broadcast_message(&ctx, announce);
    }

    // 注销并广播退出
    unregister_client(&ctx, client);
    num--;
    snprintf(announce, sizeof announce, "%s 离开了聊天室, 当前用户数: %d\n", client->name, num);
    broadcast_message(&ctx, announce);
    printf("[LOG] 用户“%s”已退出, fd=%d, 当前用户数: %d\n", client->name, client->sockfd, num);
    close(client->sockfd);
    free(client);
    
    return NULL;
}

// void socket_init(int listen_fd, struct sockaddr_in serv_addr){
//     // socket初始化三部曲
//     if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//         perror("socket");
//         exit(EXIT_FAILURE);
//     }

//     memset(&serv_addr, 0, sizeof(serv_addr));
//     serv_addr.sin_family      = AF_INET;
//     serv_addr.sin_addr.s_addr = INADDR_ANY;
//     serv_addr.sin_port        = htons(SERVER_PORT);

//     if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
//         perror("bind");
//         close(listen_fd);
//         exit(EXIT_FAILURE);
//     }

//     if (listen(listen_fd, MAX_CLIENTS) < 0) {
//         perror("listen");
//         close(listen_fd);
//         exit(EXIT_FAILURE);
//     }
// }

int main() {
    int listen_fd, conn_fd;
    struct sockaddr_in serv_addr;

    // socket初始化三部曲
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(SERVER_PORT);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("聊天室服务器已启动，监听端口 %d\n", SERVER_PORT);

    // 循环接入客户端
    while (1) {
        conn_fd = accept(listen_fd, NULL, NULL);
        if (conn_fd < 0) {
            perror("accept");
            continue;
        }
        client_t *new_client = malloc(sizeof(client_t));
        new_client->sockfd = conn_fd;
        printf("[LOG] 新的连接请求, fd=%d\n", conn_fd);
        // 创建线程处理
        if (pthread_create(&new_client->tid, NULL, handle_client, new_client)) {
            perror("pthread_create");
            close(new_client->sockfd);
            free(new_client);
            continue;
        }
        pthread_detach(new_client->tid);
    }

    close(listen_fd);
    return 0;
}
