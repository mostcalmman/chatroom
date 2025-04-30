#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 8888
#define MAX_MSG_LEN 512
#define EXIT_CMD    "//exit"

int main() {
    int listen_fd, conn_fd;
    struct sockaddr_in serv_addr;
    char buf[MAX_MSG_LEN];

    // 创建 socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 绑定地址
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // 任意可用的本地网络接口
    serv_addr.sin_port        = htons(SERVER_PORT);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 监听
    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("服务器已启动，监听端口 %d\n", SERVER_PORT);

    // 接收单个客户端连接
    if ((conn_fd = accept(listen_fd, NULL, NULL)) < 0) {
        perror("accept");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("客户端已连接\n");

    // 消息收发循环
    while (1) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = recv(conn_fd, buf, MAX_MSG_LEN - 1, 0);
        if (n <= 0) {
            printf("客户端已断开\n");
            break;
        }
        buf[n] = '\0';

        // 客户端退出命令
        if (strcmp(buf, EXIT_CMD) == 0) {
            printf("收到退出命令，关闭连接\n");
            break;
        }

        // 打印并原样返回
        printf("服务器接收到消息：%s\n", buf);
        send(conn_fd, buf, n, 0);
    }

    close(conn_fd);
    close(listen_fd);
    return 0;
}
