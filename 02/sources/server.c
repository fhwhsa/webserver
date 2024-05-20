#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <event.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <event2/listener.h>
#include <event.h>
#include <signal.h>
#include <getopt.h>
#include "wrap.h"

extern char *optarg;
extern int optind, opterr, optopt;

uint16_t port = 8080;
int readTimeout = 10;
char* ipAddr = "127.0.0.1";

/* 不允许访问的资源 */
static char* intercept_list[3] = {
    "dirhead.html",
    "dirtail.html",
    NULL
};

static const struct table_entry {
	const char *extension;
	const char *content_type;
} content_type_table[] = {
	{ ".txt", "text/plain; charset=UTF-8" },
	{ ".c", "text/plain; charset=UTF-8" },
	{ ".h", "text/plain; charset=UTF-8" },
	{ ".html", "text/html; charset=UTF-8" },
	{ ".htm", "text/htm; charset=UTF-8" },
	{ ".css", "text/css; charset=UTF-8" },
	{ ".gif", "image/gif" },
	{ ".jpg", "image/jpeg" },
	{ ".jpeg", "image/jpeg" },
	{ ".png", "image/png" },
	{ ".pdf", "application/pdf" },
	{ ".ps", "application/postscript" },
    { ".mp3", "audio/mp3" },
	{ NULL, NULL },
};

/**
 * 描述：获取内容类型
 * 参数：
 *      filename        内容名称
 * 返回：
 *      filename对应的Content-Type
*/
const char* get_content_type(char* filename)
{
    char* dot;
    dot = strrchr(filename, '.');

    if (dot == NULL)
        return "application/octet-stream";
    
    const struct table_entry *it;
    for (it = &content_type_table[0]; it->extension; ++it)
    {
        if (0 == strcmp(dot, it->extension))
            return it->content_type;
    }

    return "application/octet-stream";
}

/**
 * 描述：十六进制转十进制
*/
int htod(char c);

/**
 * 描述：处理字符串避免中文乱码
 * 参数：
 *      from        待处理的字符串
 * 返回：
 *      将处理好的字符串复制给from
*/
void strdecode(char* from);

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
void send_header(struct bufferevent *bev, const char* version, int status_code, const char* info, const char* content_type, off_t content_len);

/**
 * 描述：发送响应体
 * 参数：
 *      cfd     套接字
 *      tar     请求资源路径
*/
void send_body(struct bufferevent *bev, const char* tar);

/**
 * 描述：处理http请求
 * 参数：
 *      r_header        请求头
 *      ev              epoll_event      
*/
void request_handler(const char* r_header, struct bufferevent *bev);

/**
 * 描述：监听套接字回调
*/
void listener_cb(struct evconnlistener *, evutil_socket_t,
     struct sockaddr *, int socklen, void *);

/**
 * 描述：读回调
*/
void read_cb(struct bufferevent *, void *);

/**
 * 描述：异常回调
*/
void event_cb(struct bufferevent *, short, void *);

int main(int argc, char** argv)
{
    int c;
    while (-1 != (c = getopt(argc, argv, "p:i:t:h")))
    {
        switch (c)
        {
        case 'p':
            port = atoi(optarg);
            if (0 == port)
            {
                Printf("Option p parameter is incorrect\n");
                return 1;
            }
            break;
        case 'i':
            ipAddr = optarg;
            while (' ' == *ipAddr)
                ++ipAddr;
            break;
        case 't':
            readTimeout = atoi(optarg);
            if (0 == readTimeout)
            {
                Printf("Option t parameter is incorrect\n");
                return 1;
            }
            break;
        case 'h':
            Printf("usage: command\n\t[-p Listening port]\n\t"
                "[-i Listening address]\n\t[-t The read timeout for a bufferevent]\n\t[-h help]\n");
            return 0;
        case '?':
            Printf("unknow option:%c\n",optopt);
            break;
        }
    }

    // 忽略SIGPIPE信号
    if (SIG_ERR == signal(SIGPIPE, SIG_IGN))
        perr_exit("signal ereor");

    // 切换工作目录
    char* currpwd = getenv("PWD");
    strcat(currpwd, "/../serverResources");
    chdir(currpwd);

    struct event_base* ebase = event_base_new();
    if (!ebase)
    {
        fprintf(stderr, "Could not initialize libevent!");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ipAddr);
    struct evconnlistener *listener = evconnlistener_new_bind(ebase, listener_cb, ebase,
        LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1, (struct sockaddr*)&addr, sizeof(addr));
    if (!listener)
    {
        fprintf(stderr, "Could not create a listener!\n");
        return 1;
    }

    Printf("The server listens to %s, the listening port is %d, and the read timeout is %ds\n", ipAddr, port, readTimeout);

    event_base_dispatch(ebase);

    evconnlistener_free(listener);
    event_base_free(ebase);

    printf("server done\n");
    
    return 0;
}

int htod(char c)
{
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return c - '0';
}

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

void send_header(struct bufferevent *bev, const char* version, int status_code, const char* info, const char* content_type, off_t content_len)
{
    // 发送状态行
    int len;
    char buf[1024] = "";
    len = sprintf(buf, "%s %d %s\r\n", version, status_code, info);
    bufferevent_write(bev, buf, len);

    // 发送响应头
    bzero(buf, sizeof(buf));
    len = sprintf(buf, "Content-Type: %s\r\n", content_type);
    bufferevent_write(bev, buf, len);
    if (content_len > 0)
    {
        bzero(buf, sizeof(buf));
        len = sprintf(buf, "Content-Length: %ld\r\n", content_len);
        bufferevent_write(bev, buf, len);
    }

    // 空行
    bufferevent_write(bev, "\r\n", 2);
}

void send_body(struct bufferevent *bev, const char* tar)
{
    int fd = open(tar, O_RDONLY);
    char buf[1024] = "";
    int len = 0;
    while ((len = read(fd, buf, sizeof(buf))) > 0)
        bufferevent_write(bev, buf, len);
    if (len < 0)
        Perror("read error");
}

void request_handler(const char* r_header, struct bufferevent *bev)
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
            send_header(bev, "HTTP/1.1", 200, "404	Not Found", get_content_type(tar), 0);
            send_body(bev, tar);
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
                send_header(bev, "HTTP/1.1", 200, "OK", get_content_type("*.html"), 0);
                send_body(bev, "dirhead.html");

                // 列表
                int n;
                struct dirent **namelist = NULL;
                if (-1 == (n = scandir(tar, &namelist, NULL, alphasort)))
                    Perror("scandir error");
                char buf[1024];
                for (int i = 0; i < n; ++i)
                {
                    int size = sprintf(buf, "<li><a href=/%s/%s>%s</a></li>", tar, namelist[i]->d_name, namelist[i]->d_name);
                    bufferevent_write(bev, buf, size);
                    free(namelist[i]);
                }
                free(namelist);

                send_body(bev, "dirtail.html");

            }
            else if (S_ISREG(s.st_mode) && intercept) // 文件
            {
                send_header(bev, "HTTP/1.1", 200, "OK", get_content_type(tar), s.st_size);
                send_body(bev, tar);
            }
            else // 不允许访问的资源，403
            {
                tar = "error403.html";
                send_header(bev, "HTTP/1.1", 200, "403	Forbidden", get_content_type(tar), 0);
                send_body(bev, tar);
            }
        }
    }
} 

void listener_cb(struct evconnlistener *evlistener, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *arg)
{
    struct event_base* ebase = (struct event_base*)arg;

    // struct sockaddr_in* inaddr = (struct sockaddr_in*)addr;
    // char *addrinfo = (char*)malloc(sizeof(char));
    // sprintf(addrinfo, "%s:%d", inet_ntoa(inaddr->sin_addr), ntohs(inaddr->sin_port));
    // Printf("The new connection %s \n", addrinfo);

    struct bufferevent* bev = bufferevent_socket_new(ebase, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev)
    {
        fprintf(stderr, "Error create bufferent!\n");
        // event_base_loopbreak(ebase);
        return;
    }

    // 长时间未响应断开
    struct timeval timeout_read = {readTimeout, 0};
    bufferevent_set_timeouts(bev, &timeout_read, NULL); 

    bufferevent_setcb(bev, read_cb, NULL, event_cb, NULL);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
}

void read_cb(struct bufferevent *bev, void *arg)
{
    // Printf("read_cb %d\n", (int)arg);
    char* buf = NULL;
    size_t len;
    struct evbuffer* input = bufferevent_get_input(bev);
    buf = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF_STRICT);

    request_handler(buf, bev);
    while ((buf = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF_STRICT)))
    {
    }

    // bufferevent_free(bev);
}

void event_cb(struct bufferevent *bev, short what, void *arg)
{
    // Printf("event_cb %d", (int)arg);
    if (what & BEV_EVENT_EOF)
        Printf("Connection closed\n");
    else if (what & BEV_EVENT_TIMEOUT)
        Printf("Timeout closed\n");
    else if (what & BEV_EVENT_ERROR)
        Perror("Got an error on the connection");
    else;
    bufferevent_free(bev);
}