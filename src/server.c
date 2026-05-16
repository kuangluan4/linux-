#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <asm-generic/fcntl.h>
#include <stdint.h>

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

    char filename[256] = {0};//初始化a全为0
    int i = 0;
    
    while(i<sizeof(filename) - 1){
        char c;
        int n = recv(cfd,&c,1,0);
        //第三个参数是接收的字节数，这里是1个字节
        if(n <=0){
            printf("server failed to receive file\n");
            close(cfd);
            close(lfd);
            return -1;
        }
        if(c == '\n')break;//这里指 文件名e结尾是\n
        filename[i++] = c;
    }
    filename[i] = '0';
    printf("filename: %s\n",filename);
//uint32_t是无符号32位整数类型，file_size_net是网络字节序的文件大小
//recv函数接收数据，参数分别是：socket描述符、接收缓冲区、接收字节数、标志
//发送前用htonl将主机字节序的文件大小转换为网络字节序，接收时用ntohl将网络字节序转换为主机字节序
    uint32_t file_size_net;
    int n = recv(cfd,&file_size_net,sizeof(file_size_net),0);
    if(n != sizeof(file_size_net)){
        printf("server failed to receive file size\n");
        close(cfd);
        close(lfd);
        return -1;
    }
    uint32_t file_size = ntohl(file_size_net);
    //转化为主机字节

    char save_name[512];
    sprintf(save_name,"recv_%s",filename);
    FILE *fp = fopen(save_name,"wb");
    //文件以二进制写入的方式打开
    if(fp == NULL){
        perror("fopen");
        close(cfd);
        close(lfd);
        return -1;
    }

    uint32_t received = 0;
    char buf[BUF_SIZE];
    while(received <file_size){
        int need  = (file_size - received) > BUF_SIZE ?BUF_SIZE : (file_size - received);
        int n = recv(cfd,buf,need,0);
        if(n <= 0){
            printf("server failed to receive file data\n");
            break;
        }
        fwrite(buf,1,n,fp);
        received += n;//移动n个字节
        printf("received %u/%u bytes\r",received,file_size);
        fflush(stdout);//刷新输出缓冲区，确保进度信息及时显示   
    }
    printf("\nfile received sucess\n");
    fclose(fp);

    close(cfd);
    close(lfd);
    printf("server closed\n");

    return 0;
}

