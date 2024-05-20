#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <signal.h>

#include "wrap.h"
#include "type.h"

#define IPADDR "127.0.0.1"
#define PORT 8080

/* 不允许访问的资源 */
static char* intercept_list[3] = {
    "dirhead.html",
    "dirtail.html",
    NULL
};

/**
 * 描述：十六进制转十进制
*/
int htod(char c)
{
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return c - '0';
}

/**
 * 描述：处理字符串避免中文乱码
 * 参数：
 *      from        待处理的字符串
 * 返回：
 *      将处理好的字符串复制给from
*/
void strdecode(char* from)
{
    char to[128] = "";
    int len = strlen(from);
    int j = 0;
    for (int i = 0; i < len; )
    {
        if (from[i] == '%' && i + 2 < len)
        {
            char a = from[i + 1], b = from[i + 2];
            if ( ((a >= 'A' && a <= 'F') || (a >= 'a' && a <= 'f') || (a >= '0' && a <= '9')) 
            && ((b >= 'A' && b <= 'F') || (b >= 'a' && b <= 'f') || (b >= '0' && b <= '9')) )
            {
                to[j++] = htod(from[i + 1]) * 16 + htod(from[i + 2]);
                i += 3;   
                continue;
            }
        }
        to[j++] = from[i++];
    }
    to[j++] = 0;
    memcpy(from, to, j);
    // Printf("%s\n", to);
}

/**
 * 描述：发送响应头
 * 参数：
 *      cfd             套接字
 *      version         http版本，如HTTP/1.1
 *      status_code     状态码
 *      info            状态码信息
 *      content_type    Content-Type
 *      content_len     Content_Length
*/
void send_header(int cfd, const char* version, int status_code, const char* info, const char* content_type, off_t content_len)
{
    // 发送状态行
    int len;
    char buf[1024] = "";
    len = sprintf(buf, "%s %d %s\r\n", version, status_code, info);
    send(cfd, buf, len, 0);

    // 发送响应头
    bzero(buf, sizeof(buf));
    len = sprintf(buf, "Content-Type: %s\r\n", content_type);
    send(cfd, buf, len, 0);
    if (content_len > 0)
    {
        bzero(buf, sizeof(buf));
        len = sprintf(buf, "Content-Length: %ld\r\n", content_len);
        send(cfd, buf, len, 0);
    }

    // 空行
    send(cfd, "\r\n", 2, 0);
}

/**
 * 描述：发送响应体
 * 参数：
 *      cfd     套接字
 *      tar     请求资源路径
*/
void send_body(int cfd, const char* tar)
{
    int fd = open(tar, O_RDONLY);
    char buf[1024] = "";
    int len = 0;
    while ((len = read(fd, buf, sizeof(buf))) > 0)
        send(cfd, buf, len, 0);
    if (len < 0)
        Perror("read error");
}

/**
 * 描述：处理http请求
 * 参数：
 *      r_header        请求头
 *      ev              epoll_event      
*/
void request_handler(const char* r_header, struct epoll_event* ev)
{
    if (0 == strlen(r_header))
        return;

    char r_method[128] = ""; // 请求方法
    char r_target[128] = ""; // 请求资源
    char r_version[128] = ""; // http版本
    sscanf(r_header, "%[^ ] %[^ ] %[^\r\n]", r_method, r_target, r_version);

    // Printf("%d", ev->data.fd);
    strdecode(r_target);
    if (0 == strcasecmp("get", r_method)) // 处理get请求
    {
        char* tar = r_target + 1;
        if (0 == *tar) // 默认访问index.html
            tar = "index.html";       

        struct stat s;
        if (stat(tar, &s) < 0) // 文件不存在, 404
        {
            tar = "error404.html";
            send_header(ev->data.fd, "HTTP/1.1", 200, "404	Not Found", get_content_type(tar), 0);
            send_body(ev->data.fd, tar);
        }

        else 
        {
            int intercept = 1; // 拦截标志，防止访问不允许访问的资源
            for (int i = 0; NULL != intercept_list[i]; ++i)
            {
                if (0 == strcmp(tar, intercept_list[i]))
                {
                    intercept = 0;
                    break;
                }
            }

            if (S_ISDIR(s.st_mode) && intercept) // 目录
            {
                send_header(ev->data.fd, "HTTP/1.1", 200, "OK", get_content_type("*.html"), 0);
                send_body(ev->data.fd, "dirhead.html");

                // 列表
                int n;
                struct dirent **namelist = NULL;
                if (-1 == (n = scandir(tar, &namelist, NULL, alphasort)))
                    Perror("scandir error");
                char buf[1024];
                for (int i = 0; i < n; ++i)
                {
                    int size = sprintf(buf, "<li><a href=/%s/%s>%s</a></li>", tar, namelist[i]->d_name, namelist[i]->d_name);
                    send(ev->data.fd, buf, size, 0);
                    free(namelist[i]);
                }
                free(namelist);

                send_body(ev->data.fd, "dirtail.html");

            }
            else if (S_ISREG(s.st_mode) && intercept) // 文件
            {
                send_header(ev->data.fd, "HTTP/1.1", 200, "OK", get_content_type(tar), s.st_size);
                send_body(ev->data.fd, tar);
            }
            else // 不允许访问的资源，403
            {
                tar = "error403.html";
                send_header(ev->data.fd, "HTTP/1.1", 200, "403	Forbidden", get_content_type(tar), 0);
                send_body(ev->data.fd, tar);
            }
        }
    }
} 

int main()
{
    // 忽略SIGPIPE信号
    if (SIG_ERR == signal(SIGPIPE, SIG_IGN))
        perr_exit("signal ereor");

    // 切换工作目录
    char* currpwd = getenv("PWD");
    strcat(currpwd, "/../serverResources");
    chdir(currpwd);
    Printf("currpwd: %s\n", getenv("PWD"));

    // 创建套接字
    int sockid = Socket(AF_INET, SOCK_STREAM, 0);

    // 设置端口复用
    int opt = 1;
    if (-1 == setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
        perr_exit("setsockopt error");

    // 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(IPADDR);
    Bind(sockid, (struct sockaddr*)&addr, sizeof(addr));

    // 监听
    Listen(sockid, 128);

    // 创建句柄
    int epfd = Epoll_create(1);

    // 监听连接
    struct epoll_event conn_event;
    conn_event.data.fd = sockid;
    conn_event.events = EPOLLIN;
    Epoll_ctl(epfd, EPOLL_CTL_ADD, sockid, &conn_event);

    struct epoll_event readylist[1024];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);

    Printf("The server listens to %s, the listening port is %d\n", IPADDR, PORT);
    while (1)
    {
        int size = Epoll_wait(epfd, readylist, 1024, -1);
        for (int i = 0; i < size; ++i)
        {
            int fd = readylist[i].data.fd;
            uint32_t e = readylist[i].events;
            if (fd == sockid && (e & EPOLLIN)) // 新连接
            {
                int acsockid = Accept(fd, (struct sockaddr*)&cliaddr, &len);
                // Printf("New connecton %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));


                // 设为非阻塞
                int flags = fcntl(acsockid, F_GETFL);
                if (-1 == flags)
                    Perror("fcntl error");
                flags |= O_NONBLOCK;
                if (-1 == fcntl(acsockid, F_SETFL, flags))
                    Perror("fcntl error");


                // 上树，设置为边沿触发
                struct epoll_event ee;
                ee.data.fd = acsockid;
                ee.events = EPOLLIN | EPOLLET;
                Epoll_ctl(epfd, EPOLL_CTL_ADD, acsockid, &ee);
            }
            else if (fd != sockid && (e & EPOLLIN)) // 处理http请求
            {
                int n;
                char buf[1024];
                bzero(buf, sizeof(buf));
                Readline(fd, buf, sizeof(buf));

                // 处理请求
                request_handler(buf, &readylist[i]);

                while ((n = Readline(fd, buf, sizeof(buf))) > 0)
                {

                }

                if (!(-1 == n && errno == EAGAIN))
                    Perror("read error");
                Epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                Close(fd);
            }
        } 
    }

}