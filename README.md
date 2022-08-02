# Simple-HTTP-Server
这是一个基于线程池的，简单的Web Server项目实现。

## 编译运行
`git clone https://github.com/Liu-huaian/Simple-HTTP-Server`     
 `cd Simple-HTTP-Server/bin`  
 `cmake ..`  
 `make`  
 
## 项目简介
本项目基于Linux操作系统，使用C++语言实现了一个简单的HTTP服务器，支持用户通过GET请求获取静态资源和通过POST请求访问CGI。为了达到高性能服务器的要求，本项目主要采用了以下设计：
* 采用Proactor模式，主线程使用Epoll负责处理客户端的连接请求、请求读取和响应写入，子线程负责逻辑业务的处理;
* 采用固定线程池，对HTTP请求进行并发处理；
* 采用有限状态机对HTTP请求进行高效解析;
* 采用多进程和管道，实现CGI请求；
* 设计异步阻塞队列，实现高效日志系统。

## 参考书籍
《Linux高性能服务器编程》游双 著  
《C++服务器开发精髓》张远龙 著

## 小结与展望
本项目是一个简单的HTTP服务器的实现，支持的功能较为有限，但很完善的帮助复习了Linux系统编程和网络编程的知识点。
结合Golang中的一些现有的Web框架，本项目存在以下不足，可以在将来进一步改进：
1. **本项目不支持路由管理。** 本项目对于HTTP请求处理逻辑较为简单，根据请求类型（GET/POST）和url来判断文件是否存在，并执行相应的逻辑处理，并没有专门的路由管理。最简单粗暴的路由设计是采用Hash
实现url和回调处理函数之间的映射，根据url调用相应的处理函数，但这种路由并不是RESTful的，不支持动态路由。后续将考虑引入**前缀树**实现根据路由动态查找回调函数。
2. **本项目线程池固定。** 本项目采用固定线程池，默认线程数目为8，构成消费者-生产者模型。然而，在实际业务逻辑处理中，依然会存在大量的系统调用导致线程阻塞，例如文件信息的读取、文件内容读取和
数据库等操作，因此对于并发量特别大的场景会显得非常吃力。后序可以考虑引入动态线程池和协程，当线程进入系统调用时，调用新的线程和待处理协程结合，继续进行逻辑业务处理。
3. **本项目CGI容易阻塞线程运行**本项目中，子线程在fork出CGI程序后，会通过管道传递给CGI程序必要的CGI信息，然后阻塞等待CGI程序处理数据并通过管道返回。当有大量CGI请求时，不仅会消耗大量的资源，
而且线程池中的线程将因为持续等待管道通信而阻塞，导致服务器瘫痪。后序考虑直接由CGI进程将处理信息返回给请求的客户端。
4. **本项目不支持资源缓存。** 本项目中对于静态资源的每次请求都需要重新读取静态文件，涉及系统调用，造成不必要的开销。后续考虑使用Redis作为缓存数据库，对高频访问的静态资源进行缓存，加快读取速度。