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

int main(void){
    int cfd = socket(AF_INET,SOCK_STREAM,0);
    if(cfd == -1){
        perror("socket");
        return -1;
    }
    printf("socket created sucessfully!\n");

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET,"127.0.0.1",&server_addr.sin_addr);
//回环地址，表示连接到本机
    int ret = connect(cfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(ret == -1){
        perror("connect");
        close(cfd);
        return -1;
    }
    printf("connect to server sucessfully\n");
    printf("please input a message:");

    char buf[BUF_SIZE];
    while(1){
        fgets(buf,BUF_SIZE,stdin);
        
        send(cfd,buf,strlen(buf),0);
        memset(buf,0,sizeof(buf));
        int len = recv(cfd,buf,sizeof(buf),0);
        if(len <= 0){
            printf("server closed the connection\n");
            break;
        }
        printf("message from server: %s\n",buf);
        printf("please input a message:");
    }

    close(cfd);
    return 0;

    
}