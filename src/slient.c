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

int main(void){
    const char *local_filename = "src/test.txt";
    FILE *fp = fopen(local_filename,"rb");
    if(fp == NULL){
        perror("open file fail");
        return -1;
    }

    fseek(fp,0,SEEK_END);
    long file_size = ftell(fp);
    fseek(fp,0,SEEK_SET);
    //fseek和ftell函数用于获取文件大小，首先将文件指针移动到文件末尾，然后使用ftell获取当前文件指针的位置，即文件大小，最后将文件指针重新移动到文件开头

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

    const char *filename_only = "test.txt";
    send(cfd,filename_only,strlen(filename_only),0);
    send(cfd,"\n",1,0);

    uint32_t file_size_net = htonl(file_size);
    //网络字节序，后面server端用ntohl转换成主机字节序
    send(cfd,&file_size_net,sizeof(file_size_net),0);

        char buf[BUF_SIZE];
    size_t send_total = 0;
    while (send_total < file_size) {
        size_t need = (file_size - send_total) > BUF_SIZE ? BUF_SIZE : (file_size - send_total);
        size_t n = fread(buf, 1, need, fp);
        if (n == 0) break;
        int s = send(cfd, buf, n, 0);
        if (s <= 0) {
            perror("send");
            break;
        }
        send_total += s;
        printf("已发送: %zu / %ld 字节\r", send_total, file_size);
        fflush(stdout);
    }
    printf("\n文件发送完成！\n");

    close(cfd);
    return 0;

    
}