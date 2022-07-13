## TinyHttpd项目简介
结合《Linux高性能服务器编程》和tinyhttpd的C++轻量级HttpServer，基于epoll事件驱动I/O，采用高效的Reactor模型/线程池进行客户端连接任务管理，在阿里云1G运存云服务器上，利用http_load测试，支持1020的静态与动态http请求。

## chmod

1. cd httpdocs
2. sudo chmod 7770 post.html
3. sudo chmod 777 test.html
4. sudo chmod a+x post.cgi

## build

1. 解压缩 unzip TinyHttpd-master.zip
2. cd TinyHttpd-master
3. make
4. ./myhttp

## tech

1. 动态请求解析技术CGI
2. Reactor模式
3. epoll I/O多路复用
4. 线程池
5. socket网络编程相关知识
6. http报文格式
7. http请求命令 get/post
8. 进程通信（管道pipe）

## 改进方向

1.增加内存池管理者线程功能
2.将管道可以改进成消息队列或者共享内存的模式
3.尝试使用memset减少内核态和用户态之间的通信，看能不能做到更快的速度

## 参考文章

1. [C++实现的高并发web服务器](https://github.com/Fizell/webServer)
2. [高性能HTTP服务器:设计和思路](https://blog.csdn.net/qq_41111491/article/details/104288554)
3. [TinyHttpd精读](https://www.cnblogs.com/nengm1988/p/7816618.html)
4. [20分钟彻底了解epoll](https://www.debugger.wiki/article/html/1554202800691456)
5. [linux下搭建wenserver](https://github.com/qinguoyi/TinyWebServer)
