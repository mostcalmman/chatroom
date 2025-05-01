#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 8888
#define MAX_MSG_LEN 512
#define EXIT_CMD    "//exit"

int main(int argc, char *argv[]) {
    // 输入格式检查
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in serv_addr;
    char buf[MAX_MSG_LEN];

    // 创建 socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 填写服务器地址
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(SERVER_PORT);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("已连接到服务器 %s:%d\n", argv[1], SERVER_PORT);

    // 发送/接收循环（阻塞）
    while (1) {
        // 接收服务器回复
        ssize_t n = recv(sockfd, buf, MAX_MSG_LEN - 1, 0);
        if (n <= 0) {
            printf("服务器已断开\n");
            break;
        }
        buf[n] = '\0';
        printf("服务器消息: %s", buf);
        
        // 从 stdin 读取一行
        if (!fgets(buf, MAX_MSG_LEN, stdin)) {
            break;
        }
        // 去掉末尾换行
        buf[strcspn(buf, "\n")] = '\0';

        // 发送
        send(sockfd, buf, strlen(buf), 0);

        // 退出命令
        if (strcmp(buf, EXIT_CMD) == 0) {
            printf("发送退出命令，断开连接\n");
            break;
        }

        
    }

    close(sockfd);
    return 0;
}
