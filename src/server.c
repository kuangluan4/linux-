#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <asm-generic/fcntl.h>

#define PORT 8888
#define BUF_SIZE 1024
#define MAX_CLIENTS 100

int main(void){
    int lfd = socket(AF_INET,SOCK_STREAM,0);
    if(lfd == -1){
        perror("socket");
        return -1;
    }//创建socket失败
    printf("socket created sucessfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;//ipv4;
    server_addr.sin_port = htons(PORT);//要经过网络传输，所以要转换成网络字节序
    inet_pton(AF_INET,"0.0.0.0",&server_addr.sin_addr);

    int ret = bind(lfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(ret == -1){
        perror("bind");
        return -1;
    }
    printf("bind sucessfully\n");

    ret = listen(lfd,MAX_CLIENTS);
    if(ret == -1){
        perror("listen");
        return -1;
    }
    printf("listen sucessfully\n");

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int cfd = accept(lfd,(struct sockaddr*)&client_addr,&client_addr_len);
    if(cfd == -1){
        perror("accept");
        close(lfd);
        return -1;
    }
    printf("accept sucessfully\n");

    char client_ip[16];
    inet_ntop(AF_INET,&client_addr.sin_addr,client_ip,sizeof(client_ip));
    //ip放到client_ip中，sizeof(client_ip)是为了防止越界
    int client_port = ntohs(client_addr.sin_port);//字节从网络-》主机
    printf("client connected: %s:%d\n",client_ip,client_port);

    char buf[BUF_SIZE];
    
    while(1){
        memset(buf,0,sizeof(buf));
        int len = recv(cfd,buf,BUF_SIZE - 1,0);
        //最后一个参数是flags，0表示默认行为
        if (len <= 0){
            if (len == 0) {
                printf("客户端正常关闭连接\n");
            } 
            else {
                perror("recv");
            }
            break;   // 退出循环，关闭当前连接
        }
        printf("message from client:%s\n",buf);
        const char *reply = "hello from server!";
        send(cfd,reply,strlen(reply),0);
    }

    close(cfd);
    close(lfd);
    printf("server closed\n");

    return 0;
}

