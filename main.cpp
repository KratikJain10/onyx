#include <iostream>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#define PORT 6379

int main(){
    int socket_fd=socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd==-1){
        perror("socket");
        return 1;
    }
    std::cout<<"sockets fd is "<<socket_fd<<'\n';
    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if( bind(socket_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))==-1){
        perror("bind");
        close(socket_fd);
        return 1;
    }
    if(listen(socket_fd,16)==-1){
        perror("listen");
        close(socket_fd);
        return 1;
    }
    std::cout<<"server is listening on port: "<<PORT<<"\n";
    close(socket_fd);
    return 0;
}