#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int sockfd;  // 全局，以便在 send 线程结束后关闭

// 发送线程：读取 stdin，每行发送到服务器
void *send_handler(void *arg) {
    char buf[MAX_MSG_LEN];
    while (fgets(buf, sizeof(buf), stdin)) {
        buf[strcspn(buf, "\r\n")] = '\0';  // 去除换行
        if (send(sockfd, buf, strlen(buf), 0) < 0) {
            perror("send");
            break;
        }
        if (strcmp(buf, EXIT_CMD) == 0) {
            // 向服务器通知“退出”，并中断读 recv
            shutdown(sockfd, SHUT_RDWR);
            break;
        }
    }
    return NULL;
}

// 接收线程：不断从服务器 recv 并打印
void *recv_handler(void *arg) {
    char buf[MAX_MSG_LEN + MAX_NAME_LEN + 32];
    ssize_t n;
    while ((n = recv(sockfd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    // 创建 socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // 连接服务器
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(SERVER_PORT);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // 接收服务器的昵称提示并发送昵称
    char prompt_buf[MAX_MSG_LEN];
    ssize_t n = recv(sockfd, prompt_buf, sizeof(prompt_buf)-1, 0);
    if (n <= 0) {
        perror("recv prompt");
        close(sockfd);
        return EXIT_FAILURE;
    }
    prompt_buf[n] = '\0';
    printf("%s", prompt_buf);

    char name_buf[MAX_NAME_LEN + 2];
    while (fgets(name_buf, sizeof(name_buf), stdin)) {
        name_buf[strcspn(name_buf, "\r\n")] = '\0';
        if (send(sockfd, name_buf, strlen(name_buf), 0) < 0) {
            perror("send nickname");
            close(sockfd);
            return EXIT_FAILURE;
        }
        // 等待服务器反馈：注册成功则会广播加入，重复或过长会再次提示
        n = recv(sockfd, prompt_buf, sizeof(prompt_buf)-1, 0);
        if (n <= 0) {
            perror("recv after nickname");
            close(sockfd);
            return EXIT_FAILURE;
        }
        prompt_buf[n] = '\0';
        // 如果提示中包含“加入了聊天室”，说明注册成功，打印并跳出
        if (strstr(prompt_buf, "加入了聊天室")) {
            printf("%s", prompt_buf);
            break;
        }
        // 否则可能是“昵称重复”或“过长”，打印后重输
        printf("%s", prompt_buf);
    }

    // 启动发送和接收线程
    pthread_t tid_send, tid_recv;
    if (pthread_create(&tid_send, NULL, send_handler, NULL) != 0) {
        perror("pthread_create send");
        close(sockfd);
        return EXIT_FAILURE;
    }
    if (pthread_create(&tid_recv, NULL, recv_handler, NULL) != 0) {
        perror("pthread_create recv");
        shutdown(sockfd, SHUT_RDWR);
        pthread_join(tid_send, NULL);
        close(sockfd);
        return EXIT_FAILURE;
    }

    // 等待线程结束
    pthread_join(tid_send, NULL);
    pthread_join(tid_recv, NULL);

    // 关闭 socket 并退出
    close(sockfd);
    return EXIT_SUCCESS;
}