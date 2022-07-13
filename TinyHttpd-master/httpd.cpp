#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <iostream>
#include "threadpool.h"

//宏定义，是否是空格
#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: zer's http/0.1.0\r\n"

//每次收到请求，创建一个线程来处理接受到的请求，把client_sock转成地址作为参数传入pthread_create
void *accept_request(void* client);

//错误请求
void bad_request(int);

//读取文件
void cat(int, FILE *);

//无法执行
void cannot_execute(int);

//错误输出
void error_die(const char *);

//执行cgi脚本
void execute_cgi(int, const char *, const char *, const char *);

//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int, char *, int);

//返回http头
void headers(int, const char *);

//没有发现文件
void not_found(int);

//如果不是CGI文件，直接读取文件返回给请求的http客户端
void serve_file(int, const char *);

//开启tcp连接，绑定端口等操作
int startup(u_short *);

//如果不是Get或者Post，就报方法没有实现
void unimplemented(int);

void *accept_request(void* from_client)
{
    // 客户端文件描述符
    int client = *(int *)from_client;

    char buf[1024];
    int numchars;
    // 存放请求的方法
    char method[255];
    // url存放
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    // 看是否是cgi动态请求
    int cgi = 0;
    // 指针，指向url
    char *query_string = NULL;

    //读http 请求的第一行数据（request line），把请求方法存进 method 中
    //请求方法（GET）+ url + http版本
    // get_line放入buf中，返回i==buf的长度
    numchars = get_line(client, buf, sizeof(buf));

    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        //提取其中的请求方式
        method[i] = buf[j];
        i++;
        j++;
    }
    // 添加一个\0
    method[i] = '\0';

    //如果请求的方法不是 GET 或 POST 任意一个的话就直接发送 response 告诉客户端没实现该方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return NULL;
    }

    //如果是 POST 方法就将 cgi 标志变量置一(true)
    if (strcasecmp(method, "POST") == 0)
    {
        cgi = 1;
    }
    i = 0;
    //跳过所有的空白字符(空格)
    // 刚刚读完了请求方法，现在读url
    while (ISspace(buf[j]) && (j < sizeof(buf)))
    {
        j++;
    }

    //然后把 URL 读出来放到 url 数组中
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';


    //如果这个请求是一个 GET 方法的话
    //GET请求url可能会带有?,有查询参数
    //GET是通过url进行传输的
    if (strcasecmp(method, "GET") == 0)
    {
        //用一个指针指向 url
        // query_string为指针
        query_string = url;
        //去遍历这个 url，跳过字符 ？前面的所有字符，如果遍历完毕也没找到字符 ？则退出循环
        while ((*query_string != '?') && (*query_string != '\0'))
        {
            query_string++;
        }

        /* 如果有?表明是动态请求, 开启cgi */
        // 区别url和查询
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
        // query_string这时指向？的下一个位置，也就是动态请求的内容
    }
    //将前面分隔两份的前面那份字符串，拼接在字符串htdocs的后面之后就输出存储到数组 path 中。相当于现在 path 中存储着一个字符串
    // output path字符串
    sprintf(path, "httpdocs%s", url);

    //如果 path 数组中的这个字符串的最后一个字符是以字符 / 结尾的话，就拼接上一个"index.html"的字符串。首页的意思
    // 默认主页
    if (path[strlen(path) - 1] == '/')
    {
        strcat(path, "test.html");
    }

    //在系统上去查询该文件是否存在
    // 用来将参数path 所指的文件状态, 复制到参数st 所指的结构中
    if (stat(path, &st) == -1)
    {
        //如果不存在，那把这次 http 的请求后续的内容(head 和 body)全部读完并忽略
        while ((numchars > 0) && strcmp("\n", buf))
        {
            numchars = get_line(client, buf, sizeof(buf));
        }
        //然后返回一个找不到文件的 response 给客户端
        not_found(client);
    }
    else
    {
        //文件存在，那去跟常量S_IFMT相与，相与之后的值可以用来判断该文件是什么类型的
        if ((st.st_mode & S_IFMT) == S_IFDIR)//S_IFDIR代表目录
            //如果请求参数为目录, 自动打开test.html
        {
            strcat(path, "/test.html");
        }

        //文件可执行, 不论是属于用户/组/其他这三者类型的，就将 cgi 标志变量置1
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH))
            //S_IXUSR:文件所有者具可执行权限
            //S_IXGRP:用户组具可执行权限
            //S_IXOTH:其他用户具可读取权限
        {
            cgi = 1;
        }

        if (!cgi)
        {
            //如果不需要 cgi 机制的话
            serve_file(client, path);
        }
        else
        {
            //如果需要则调用
            execute_cgi(client, path, method, query_string);
        }
    }

    close(client);
    printf("connection close....client: %d \n",client);
    return NULL;
}



void bad_request(int client)
{
    char buf[1024];
    //发送400
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}


void cat(int client, FILE *resource)
{
    //发送文件的内容
    char buf[1024];
    //从文件文件描述符中读取指定内容
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        // send n buf to client fd
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}


void cannot_execute(int client)
{
    char buf[1024];
    //发送500
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}


void error_die(const char *sc)
{
    //基于当前的errno值，在标准错误上产生一条错误消息
    perror(sc);
    exit(1);
}


//执行cgi动态解析
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    //缓冲区
    char buf[1024];
    //两根管道
    // 目的是实现全双工
    int cgi_output[2];
    int cgi_input[2];

    //进程pid和状态
    pid_t pid;
    int status;

    int i;
    char c;

    //读取的字符数
    int numchars = 1;
    //http的content_length
    int content_length = -1;
    //默认字符
    buf[0] = 'A';
    buf[1] = '\0';
    //如果是 http 请求是 GET 方法的话读取并忽略请求剩下的内容
    if (strcasecmp(method, "GET") == 0)
    {
        //读取数据，把整个header都读掉，以为Get写死了直接读取index.html，没有必要分析余下的http信息了
        while ((numchars > 0) && strcmp("\n", buf))
        {
            numchars = get_line(client, buf, sizeof(buf));
        }
    }
    else
    {
        //只有 POST 方法才继续读内容
        //先读取POST的header部分，目的是读取最后一句，获得body的长度
        numchars = get_line(client, buf, sizeof(buf));
        //这个循环的目的是读出指示 body 长度大小的参数，并记录 body 的长度大小。
        //其余的 header 里面的参数一律忽略
        //注意这里只读完 header 的内容，body 的内容没有读
        //POST请求，就需要得到Content-Length，Content-Length：这个字符串一共长为15位，所以
        //取出头部一句后，将第16位设置结束符，进行比较
        //第16位置为结束
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
            {
                //记录 body 的长度大小
                //内存从第17位开始就是长度，将17位开始的所有字符串转成整数就是content_length
                content_length = atoi(&(buf[16]));
            }

            numchars = get_line(client, buf, sizeof(buf));
        }
        //如果 http 请求的 header 没有指示 body 长度大小的参数，则报错返回
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    //下面这里创建两个管道，用于两个进程间通信
    if (pipe(cgi_output) < 0) { //建立output管道
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {  //建立input管道
        cannot_execute(client);
        return;
    }

    //       fork后管道都复制了一份，都是一样的
    //       子进程关闭2个无用的端口，避免浪费
    //       ----------写数据--------->【output】
    //       <---------读数据----------【input】

    //       父进程关闭2个无用的端口，避免浪费
    //       <---------读数据----------【output】
    //       ----------写数据--------->【input】

    //创建一个子进程
    //fork进程，子进程用于执行CGI,用于处理
    //父进程用于收数据以及发送子进程处理的回复数据,用来send
    if ( (pid = fork()) < 0 )
    {
        cannot_execute(client);
        return;
    }
    // 返回值等于0的是子进程
    if (pid == 0)  /* 子进程: 运行CGI 脚本 */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        //将子进程的输出由标准输出重定向到 cgi_output 的管道写端上
        // 复制文件描述符,并生成一个新的文件描述符
        // dup2返回的是不小于第二个参数的最小正整数，这里是1，对应的是标准输出文件描述符，就相当于将cgi_output[1]内容输出
        dup2(cgi_output[1], 1);
        //将子进程的输出由标准输入重定向到 cgi_input 的管道读端上
        // dup2返回的是不小于第二个参数的最小正整数，这里是0，对应的是标准输入文件描述符，就相当于将cgi_output[0]内容输入
        dup2(cgi_input[0], 0);

        // 关闭子进程中的管道的对应端口
        // 子进程往output中写数据，从input中读数据
        close(cgi_output[0]);//关闭了cgi_output中的读通道
        close(cgi_input[1]);//关闭了cgi_input中的写通道

        //构造一个环境变量
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        //将这个环境变量加进子进程的运行环境中
        putenv(meth_env);

        //根据http 请求的不同方法，构造并存储不同的环境变量
        // GET存QUERY_STRING环境变量
        if (strcasecmp(method, "GET") == 0)
        {
            //存储QUERY_STRING
            // QUERY_STRING用来存储?之后的信息
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        {
            /* POST */
            //POST存储CONTENT_LENGTH
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        //最后将子进程替换成另一个进程并执行 cgi 脚本
        // 当进程调用一种exec函数时，该进程完全由新程序代换，而新程序则从其main函数开始执行。因为调用exec并不创建新进程，所以前后的进程ID并未改变。
        // execl只是用另一个新程序替换了当前进程的正文、数据、堆和栈段。
        execl(path, path, NULL);//执行CGI脚本
        exit(0);
    }
    // 返回值大于0的是父进程
    else
    {
        // 关闭父进程中的管道的对应端口（注意和子进程关闭的不是同一个，因为是复制了空间）
        // 父进程则关闭 cgi_output管道的写端和 cgi_input 管道的读端
        // 父进程从管道output中读数据，往管道intput中写数据
        close(cgi_output[1]);
        close(cgi_input[0]);
        //如果是 POST 方法的话就继续读 body 的内容，并写到 cgi_input 管道里让子进程去读
        if (strcasecmp(method, "POST") == 0)
        {
            for (i = 0; i < content_length; i++)
            {
                //得到post请求数据，写到input管道中，供子进程使用
                recv(client, &c, 1, 0);
                // 写到cgi_input[1]上
                write(cgi_input[1], &c, 1);
            }
        }

        //读取cgi脚本返回数据
        //从 cgi_output 管道中读子进程的输出，并发送到客户端去
        //从output管道读到子进程处理后的信息，然后send出去
        // （相当于解析完成之后send的过程）
        while (read(cgi_output[0], &c, 1) > 0)
        {
            //发送给浏览器(客户端)
            send(client, &c, 1, 0);
        }

        //运行结束关闭
        close(cgi_output[0]);
        close(cgi_input[1]);

        //等待子进程返回退出
        waitpid(pid, &status, 0);
    }
}

//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        // 1是缓冲区大小
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                //偷窥一个字节，如果是\n就读走
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                    //不是\n（读到下一行的字符）或者没读到，置c为\n 跳出循环,完成一行读取
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buf[i] = '\0';
    return(i);
}

//加入http的headers
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */
    //发送HTTP头
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

//如果资源没有找到得返回给客户端下面的信息
void not_found(int client)
{
    char buf[1024];
    //返回404
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

//如果不是CGI文件，也就是静态文件，直接读取文件返回给请求的http客户端
// 静态文件响应给客户端
void serve_file(int client, const char *filename)
{
    //filename是path
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    //默认字符
    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))
    {
        numchars = get_line(client, buf, sizeof(buf));
    }

    //打开文件
    resource = fopen(filename, "r");
    if (resource == NULL)
    {
        not_found(client);
    }
    else
    {
        //打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
        headers(client, filename);
        //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
        cat(client, resource);
    }
    fclose(resource);//关闭文件句柄
}

//启动服务端
int startup(u_short *port)
{
    int httpd = 0,option;
    struct sockaddr_in name;
    //设置http socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
    {
        error_die("socket");//连接失败
    }

    socklen_t optlen;
    optlen = sizeof(option);
    option = 1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    //如果传进去的sockaddr结构中的 sin_port 指定为0，这时系统会选择一个临时的端口号
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    {
        error_die("bind");//绑定失败
    }
    if (*port == 0)  /*动态分配一个端口 */
    {
        socklen_t  namelen = sizeof(name);
        //调用getsockname()获取系统给 httpd 这个 socket 随机分配的端口号
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
        {
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }

    //最初的 BSD socket 实现中，backlog 的上限是5
    if (listen(httpd, 1024) < 0)
    {
        error_die("listen");
    }
    return(httpd);
}

//如果方法没有实现，就返回此信息
void unimplemented(int client)
{
    char buf[1024];
    //发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 6379;//默认监听端口号 port 为6379
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);

#if 0
    pthread_t newthread;
    server_sock = startup(&port);

    printf("http server_sock is %d\n", server_sock);
    printf("http running on port %d\n", port);
    while (1)
    {
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);

        printf("New connection....  ip: %s , port: %d\n",inet_ntoa(client_name.sin_addr),ntohs(client_name.sin_port));
        if (client_sock == -1)
        {
            error_die("accept");
        }

        //每次收到请求，创建一个线程来处理接受到的请求
        //把client_sock转成地址作为参数传入pthread_create
        if (pthread_create(&newthread , NULL, accept_request, (void*)&client_sock) != 0)
        {
            perror("pthread_create");
        }
    }
#else   //使用线程池+epoll
    //创建线程池
    ThreadPool pool(8);
    printf("Create threadpool success!\n");
    //服务器监听的文件描述符
    // 监听队列的长度并不是能与多少个client通信而是一次性可以处理多少个client
    server_sock = startup(&port);

    //创建epoll事件
    int epfd, nfds;
    //生成用于处理accept的epoll专用的文件描述符
    epfd = epoll_create(200);
    //ev是存放要处理的事件，告诉内核需要监听什么事件
    //参数 events 用来从内核得到事件的集合
    // epoll_event的data成员指定内核应该保存的数据，并在epoll_wait准备好时返回，可用于标识触发event的fd
    struct epoll_event ev, events[1024];
    ev.data.fd = server_sock;
    //设置要处理的事件类型
    ev.events = EPOLLIN | EPOLLET;
    //注册epoll事件
    //将事件与文件描述符绑定，事件触发文件描述符
    // (也就是将事件参数连接到对应的socket对象)
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &ev);
    printf("httpd running on port: %d\n", port);

    while(true)
    {
        //等待epoll事件的发生
        //events存放就绪的触发事件，发生的事件队列
        //等待事件（也就是有数据过来）产生,触发对应的文件描述符就绪
        nfds = epoll_wait(epfd, events, 50, 500);
        if(nfds < 0){
            printf("epoll  failure\n");
            break;
        }
        //处理所有就绪的文件描述符
        for(int i = 0; i < nfds; i++)
        {
            // 检测触发就绪事件中是否有触发的
            // 用监听文件描述符来判断是否有新的连接
            // 监听套接字就是专门用于监听的
            if(events[i].data.fd == server_sock)
            {
                client_sock = accept(server_sock, (sockaddr*)&client_name, &client_name_len);
                if(client_sock < 0)
                {
                    perror("client_sock < 0!");
                    exit(1);
                }
                char* str = inet_ntoa(client_name.sin_addr);
                printf("accept a connnection from %s!\n", str);
 
                ev.data.fd = client_sock;
                //设置用于注册的读操作事件
                ev.events = EPOLLIN | EPOLLET;
                // 注册ev
                // 客户端建立连接会形成一个新的套接字,tcp连接
                // 监听客户端套接字，看之后会不会有读写操作通过网口传递给服务器
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
            }
            // EPOLLIN表示对应的文件描述符可以读
            // 非监听套接字,一定是已建立连接的套接字,用于通信
            // 事件是从就绪队列中取出来的，那么就一定是已准备好的文件描述符的就绪事件
            else if(events[i].events & EPOLLIN)
            {
                //如果是已经连接的用户，并且收到数据，那么进行读入
                std::cout<< "ready for accept!" << std::endl;
                std::cout<< "main thread ID:" << std::this_thread::get_id() << std::endl;
                pool.enqueue(accept_request, (void*)&client_sock);
                std::cout<< "end!" << std::endl;
            }
        }
    }
#endif

    close(server_sock);
    return(0);
}
