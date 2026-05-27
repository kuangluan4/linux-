int main(){
    int lfd = socket(AF_INET,SOCK_STREAM,0);

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET,"0.0.0.0",&server_addr.sin_addr);

    int ret = bind(lfd,(struct sockaddr_in*)&server_addr,sizeof(server_addr));
    ret = listen(lfd,MAX_CLIENT);
    accept(lfd,(struct sockaddr_in*)&client_addr,&client_addr_len);
    server_addr.sin_port //网络端口
    server_addr.sin_addr //ip地址
    server_addr.sin_family //协议
    
}