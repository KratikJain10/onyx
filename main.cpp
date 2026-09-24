#include <iostream>
#include <sys/socket.h>
#include <cstdio>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>

constexpr int PORT=6379;
constexpr int BACKLOG_FOR_SOCKET=16;
constexpr int BUFF_SIZE=1024;
constexpr char PING_REPLY[]="+PONG\r\n";
constexpr ssize_t PING_REPLY_LEN=sizeof(PING_REPLY)-1;

void clientConnectionHandling(int conn_fd){
    while(true){
            char rbuff[BUFF_SIZE];
            ssize_t rval=read(conn_fd,rbuff,sizeof(rbuff));
            if(rval==0){
                break;
            }
            if(rval==-1){
                perror("read");
                break;
            }
            std::cout<<"read function return value: "<<rval<<'\n';
            std::cout.write(rbuff,rval);
            ssize_t wval=write(conn_fd,PING_REPLY,PING_REPLY_LEN);
            if(wval==-1){
                perror("write");
                break;
            }
            if(wval<PING_REPLY_LEN){
                std::cout<<"Short write happened at fd "<<conn_fd<<'\n';
                break;
            }
            std::cout<<wval<<" bytes got written on "<<conn_fd<<'\n';
        }
        close(conn_fd);
}

int main(){
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd==-1){
        perror("socket");
        return 1;
    }
    std::cout<<"listening socket fd is "<<listen_fd<<'\n';
    int sock_opt_val=1;
    if(setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&sock_opt_val,sizeof(sock_opt_val))==-1){
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if( bind(listen_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))==-1){
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if(listen(listen_fd,BACKLOG_FOR_SOCKET)==-1){
        perror("listen");
        close(listen_fd);
        return 1;
    }
    std::cout<<"server is listening on port: "<<PORT<<'\n';
    while(true){
        struct sockaddr_in client_addr;
        socklen_t client_addr_len=sizeof(client_addr);
        int conn_fd=accept(listen_fd,(struct sockaddr *)&client_addr,&client_addr_len);
        if(conn_fd==-1){
            perror("accept");
            close(listen_fd);
            return 1;
        }
        std::cout<<"client socket fd is "<<conn_fd<<'\n';
        clientConnectionHandling(conn_fd);
    }
}
