#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <string>

const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8888;

// 单个客户端逻辑
void clientTask(int id)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        std::cerr << "Client" << id << " socket create fail err:" << errno << "\n";
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0)
    {
        std::cerr << "Client" << id << " ip format error\n";
        close(sockfd);
        return;
    }

    // 连接服务器
    int ret = connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (ret < 0)
    {
        std::cerr << "Client" << id << " connect failed err:" << errno << "\n";
        close(sockfd);
        return;
    }
    std::cout << "Client" << id << " 连接成功\n";

    // 发送测试消息
    std::string sendMsg = "Test msg from client " + std::to_string(id) + "\n";
    ssize_t sendLen = send(sockfd, sendMsg.c_str(), sendMsg.size(), 0);
    if (sendLen <= 0)
    {
        std::cerr << "Client" << id << " send data fail\n";
        close(sockfd);
        return;
    }

    // 等待回显
    char buf[1024] = {0};
    ssize_t recvLen = recv(sockfd, buf, sizeof(buf)-1, 0);
    if (recvLen > 0)
    {
        std::cout << "Client" << id << " 收到回显：" << buf;
    }
    else if (recvLen == 0)
    {
        std::cout << "Client" << id << " 服务端主动关闭连接\n";
    }
    else
    {
        std::cerr << "Client" << id << " recv error\n";
    }

    sleep(1);
    close(sockfd);
    std::cout << "Client" << id << " 断开连接\n";
}

int main()
{
    const int CLIENT_NUM = 100;
    for (int i = 1; i <= CLIENT_NUM; ++i)
    {
        std::thread t(clientTask, i);
        t.detach(); // 线程分离，自动回收资源
    }
    std::cout << "已创建并分离100个客户端线程，主线程等待\n";

    // 主线程休眠足够时间，保证所有客户端跑完流程
    sleep(10);
    std::cout << "测试结束\n";
    return 0;
}