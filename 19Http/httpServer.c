/*************************************************************************
    > File Name: httpServer.c
    > Author: 威桑
    > Mail: 1428206861@qq.com
    > Created Time: 2024年10月22日 星期二 15时26分47秒
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

void handle_request(int cfd);
int create_listenfd();
void syserr(char *str)
{
    perror(str);
    exit(1);
}
int main(int argc, char *argv[])
{
    // 1.建立tcp连接
    int sfd = create_listenfd();
    while (1)
    {
        // 2.接收客户端连接
        int cfd = accept(sfd, NULL, NULL);
        if (cfd == -1)
            syserr("服务器崩溃");
        printf("客户端连接成功:%d\n", cfd);
        // 3.处理客户端发来的请求
        handle_request(cfd);
    }

    return 0;
}
int create_listenfd()
{
    // 1.1 创建socket
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
        syserr("socket build error");
    printf("socket build success\n");
    // 1.2 设置socket
    int n = 1;
    int r = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
    if (r == -1)
        syserr("setsockopt error");
    printf("setsockopt success\n");
    // 1.3 绑定
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        syserr("bind");
    printf("bind successful\n");
    // 1.4 监听
    if (listen(sfd, 100) == -1)
        syserr("listen error");
    printf("listen...\n");
    return sfd;
}
void handle_request(int cfd)
{
    // 接收请求
    char buf[1024 * 1024] = {0};
    int nread = read(cfd, buf, 1024 * 1024);
    printf("%d : %s\n", nread, buf);
    // 解析请求

    // 获取文件名
    char filename[20] = {0};
    sscanf(buf, "GET /%s", filename);
    printf("filename : %s\n", filename);

    // 根据文件名获取mime文件类型，用来放到响应头中 告诉浏览器服务器发送的是什么类型的文件
    char *mime = NULL;
    if (strstr(filename, ".html"))
    {
        mime = "text/html";
    }
    else if (strstr(filename, ".jpg"))
    {
        mime = "image/jpeg";
    }

    // 制作响应
    char response[1024 * 1024] = {0};
    // 响应头
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type:%s\r\n\r\n", mime);
    int headlen = strlen(response);
    // 数据
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
        syserr("打开文件失败\n");
    printf("打开文件成功\n");

    int filelen = read(fd, response + headlen, sizeof(response) - headlen);

    // 发送响应
    write(cfd, response, headlen + filelen);
    close(fd);
    sleep(1);
}