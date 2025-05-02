#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

// 文件信息
file_info_t files[MAX_FILES];
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;
int file_count = 0;
static const char *FILES_DIR = "server_files"; // 所有上传文件统一保存处

// 服务器上下文
static server_ctx_t ctx = {
    .clients = { NULL },
    .count   = 0,
    .lock    = PTHREAD_MUTEX_INITIALIZER // 静态初始化互斥锁
};
int num = 0; // 直接维护一个在线人数计数, 避免频繁访问服务器上下文导致锁定与解锁
static int listen_fd;
static int server_running = 1; // 控制主循环，0 时退出

/* ---------- 清空文件目录 ---------- */
static void purge_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return; // 目录不存在或权限不足

    struct dirent *ent;
    char path[512];

    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".")  == 0 ||
            strcmp(ent->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        unlink(path);
    }
    closedir(d);
}

/* ---------- stdin 监控线程 ---------- */
static void *stdin_monitor(void *arg) {
    char line[64];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, CLOSE_CMD) == 0) {
            if (num == 0) {
                printf("[LOG] 收到 //close 命令且无人在线，正在关闭服务器...\n");
                server_running = 0;
                shutdown(listen_fd, SHUT_RDWR);  // 让 accept() 立即返回
                close(listen_fd);
                break;
            } else {
                printf("[LOG] 收到 //close, 但当前在线 %d 人，请等待所有用户退出后再关闭\n", num);
            }
        }
    }
    return NULL;
}

/* ---------- 用户处理线程 ---------- */
void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buf[MAX_MSG_LEN];
    char announce[MAX_MSG_LEN + MAX_NAME_LEN + 32];
    char name_buf[MAX_NAME_LEN + 2];
    int reg;
    const char *prompt = "请输入昵称(不超过32字符): \n";
    send(client->sockfd, prompt, strlen(prompt), 0); // 发送提示

    /* ---------- 昵称处理与注册 ---------- */
    while (1) {
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
        printf("[LOG] 用户“%s”已加入, fd=%d, 当前用户数: %d\n",
               client->name, client->sockfd, num);
        break;
    }

    // 广播加入消息
    snprintf(announce, sizeof announce,
             "%s 加入了聊天室, 当前用户数: %d\n", client->name, num);
    broadcast_message(&ctx, announce);

    /* ---------- 主循环：文件与消息 ---------- */
    while (1) {
        memset(buf, 0, sizeof buf);
        ssize_t n = recv(client->sockfd, buf, MAX_MSG_LEN - 1, 0);
        if (n <= 0 || strcmp(buf, EXIT_CMD) == 0) break;

        /* ---------- 保存文件请求 ---------- */
        if (strncmp(buf, SAVE_FILE_CMD, strlen(SAVE_FILE_CMD)) == 0) {
            int req_id = atoi(buf + strlen(SAVE_FILE_CMD)); // 提取编号
            pthread_mutex_lock(&file_lock);

            if (req_id >= 0 && req_id < file_count) {
                file_info_t *fi = &files[req_id]; // 定位服务器文件信息表
                FILE *fp = fopen(fi->filepath, "rb");
                if (fp) {
                    // 计算文件大小
                    fseek(fp, 0, SEEK_END);
                    long fsize = ftell(fp);
                    rewind(fp);

                    // 先发回带头部的文件描述
                    char header[256];
                    snprintf(header, sizeof(header), SENDFILE_HDR"%s|%ld\n", fi->filename, fsize);
                    send(client->sockfd, header, strlen(header), 0);

                    // 再分块回传文件体，1024字节一块
                    char sendbuf[1024];
                    size_t r;
                    while ((r = fread(sendbuf, 1, sizeof(sendbuf), fp)) > 0)
                        send(client->sockfd, sendbuf, r, 0);
                    fclose(fp);
                    printf("[LOG] 已向 %s 发送文件 %s, 编号: %d)\n", client->name, fi->filename, req_id);
                }else{
                    printf("[LOG] 用户 %s 请求编号为 %d 的文件, 发生错误\n", client->name, req_id);
                    const char *errbuf = "编号错误或文件不存在！\n";
                    send(client->sockfd, errbuf, strlen(errbuf), 0);
                }
            }else{
                printf("[LOG] 用户 %s 请求编号为 %d 的文件, 发生错误\n", client->name, req_id);
                const char *errbuf = "编号错误或文件不存在！\n";
                send(client->sockfd, errbuf, strlen(errbuf), 0);
            }

            pthread_mutex_unlock(&file_lock);
            continue; // 跳过常规广播
        }

        /* ---------- 上传文件请求 ----------*/
        if (strncmp(buf, "[FILE]", 6) == 0) {
            // 解析文件名称和长度
            char filename[128];
            long filesize;
            sscanf(buf + 6, "%127[^|]|%ld", filename, &filesize);
        
            char filepath[256];
            pthread_mutex_lock(&file_lock);
            int fid = file_count++;
            // 更新文件地址为：文件保存目录/文件名_编号
            snprintf(filepath, sizeof(filepath), "%s/%s_%d", FILES_DIR, filename, fid);
            pthread_mutex_unlock(&file_lock);
            
            // 写文件
            FILE *fp = fopen(filepath, "wb");
            long received = 0;
            char filebuf[1024];
            ssize_t r;
            while (received < filesize && (r = recv(client->sockfd, filebuf, sizeof(filebuf), 0)) > 0) {
                fwrite(filebuf, 1, r, fp);
                received += r;
            }
            fclose(fp);
            
            // 更新文件列表
            pthread_mutex_lock(&file_lock);
            files[fid].fileid = fid;
            strcpy(files[fid].filename, filename);
            strcpy(files[fid].sender, client->name);
            strcpy(files[fid].filepath, filepath);
            pthread_mutex_unlock(&file_lock);
        
            char announce[256];
            snprintf(announce, sizeof(announce), "%s: [FILE]%s id: %d\n", client->name, filename, fid);
            broadcast_message(&ctx, announce);
            printf("[LOG] 已保存来自用户 %s 的文件 %s id: %d\n", client->name, filename, fid);

            continue; // 跳过常规广播
        }
        
        
        printf("[LOG] 来自用户 %s, fd=%d: %s\n",
               client->name, client->sockfd, buf);
        snprintf(announce, sizeof announce, "%s: %s\n", client->name, buf);
        broadcast_message(&ctx, announce);
    }

    /* ---------- 用户退出 ---------- */
    unregister_client(&ctx, client);
    num--;
    snprintf(announce, sizeof announce, "%s 离开了聊天室, 当前用户数: %d\n", client->name, num);
    broadcast_message(&ctx, announce);
    printf("[LOG] 用户“%s”已退出, fd=%d, 当前用户数: %d\n", client->name, client->sockfd, num);
    close(client->sockfd);
    free(client);
    return NULL;
}

int main() {
    int conn_fd;
    struct sockaddr_in serv_addr;

    /* --------- Socket 初始化三件套 ---------- */
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(SERVER_PORT);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr,
             sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // // 确保 server_files/ 存在
    // if (ensure_dir(FILES_DIR) != 0) {
    //     perror("mkdir files");
    //     exit(EXIT_FAILURE);
    // }

    /* ----------- 文件目录准备 ----------- */
    struct stat st;
    if (stat(FILES_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        // 目录已存在, 清空旧文件
        purge_dir(FILES_DIR);
    } else {
        // 目录不存在, 创建
        if (ensure_dir(FILES_DIR) != 0) {
            perror("mkdir files");
            exit(EXIT_FAILURE);
        }
    }


    printf("聊天室服务器已启动，监听端口 %d \n", SERVER_PORT);

    // 启动 stdin 监控线程
    pthread_t mon_tid;
    pthread_create(&mon_tid, NULL, stdin_monitor, NULL);

    /* ---------- 处理新的接入请求 --------- */
    while (server_running) {
        conn_fd = accept(listen_fd, NULL, NULL);
        if (conn_fd < 0) {
            if (!server_running) break; // 收到关闭指令后跳出
            perror("accept");
            continue;
        }
        client_t *new_client = malloc(sizeof(client_t));
        new_client->sockfd = conn_fd;
        printf("[LOG] 新的连接请求, fd=%d\n", conn_fd);
        // 启动新的用户处理线程
        if (pthread_create(&new_client->tid, NULL,
                           handle_client, new_client)) {
            perror("pthread_create");
            close(new_client->sockfd);
            free(new_client);
            continue;
        }
        pthread_detach(new_client->tid);
    }

    // 等待监控线程结束
    pthread_join(mon_tid, NULL);
    return 0;
}