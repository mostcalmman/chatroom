#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char *DL_DIR = "downloads";   /* 本地下载目录 */
static int sockfd;  // 全局, 以便在 send 线程结束后关闭

// 发送线程: 读取 stdin, 每行发送到服务器
void *send_handler(void *arg) {
    char buf[MAX_MSG_LEN];
    while (fgets(buf, sizeof(buf), stdin)) {
        buf[strcspn(buf, "\r\n")] = '\0';  // 去除换行
        /* ---------- 处理 //sendfile ---------- */
        if (strncmp(buf, SEND_FILE_CMD, strlen(SEND_FILE_CMD)) == 0) {
            char *path = buf + strlen(SEND_FILE_CMD) + 1;   /* 跳过空格 */
            FILE *fp = fopen(path, "rb");
            if (!fp) {
                fprintf(stderr, "无法打开文件 %s\n", path);
                continue;
            }
            /* 获取纯文件名 */
            char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;

            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            rewind(fp);

            /* 1) 发送文件头 */
            char header[256];
            snprintf(header, sizeof(header), FILE_HDR"%s|%ld", filename, fsize);
            if (send(sockfd, header, strlen(header), 0) < 0) {
                perror("send header");
                fclose(fp);
                break;
            }
            /* 2) 发送文件体 */
            char chunk[1024];
            size_t r;
            while ((r = fread(chunk, 1, sizeof(chunk), fp)) > 0)
                send(sockfd, chunk, r, 0);
            fclose(fp);
            printf("已发送文件 %s (%ld 字节)\n", filename, fsize);
            continue;                   /* 不再走默认 send 路径 */
        }

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

// 接收线程: 不断从服务器 recv 并打印
void *recv_handler(void *arg) {
    char buf[MAX_MSG_LEN + MAX_NAME_LEN + 32];
    ssize_t n;
    while ((n = recv(sockfd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        /* ---------- 接收服务器回传的文件 ---------- */
        if (strncmp(buf, SENDFILE_HDR, strlen(SENDFILE_HDR)) == 0) {
            char filename[128];
            long fsize;
            /* 头格式: [SENDFILE]filename|size\n */
            sscanf(buf + strlen(SENDFILE_HDR), "%127[^|]|%ld", filename, &fsize);

            /* 确保下载目录存在 */
            ensure_dir(DL_DIR);
            char dlpath[256];
            snprintf(dlpath, sizeof(dlpath), "%s/%s", DL_DIR, filename);
            FILE *fp = fopen(dlpath, "wb");
            if (!fp) {
                fprintf(stderr, "无法保存文件到 %s\n", dlpath);
                /* 丢弃文件体 */
                char drop[1024];
                long dropped = 0;
                while (dropped < fsize)
                    dropped += recv(sockfd, drop, sizeof(drop), 0);
                continue;
            }

            long received = 0;
            char filebuf[1024];
            while (received < fsize) {
                ssize_t r = recv(sockfd, filebuf, sizeof(filebuf), 0);
                if (r <= 0) break;
                fwrite(filebuf, 1, r, fp);
                received += r;
            }
            fclose(fp);
            printf("已保存文件 %s (%ld 字节)\n", dlpath, fsize);
            continue;                   /* 文件处理完毕，等待下一条消息 */
        }

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