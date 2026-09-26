#include <iostream>
#include <sys/socket.h>
#include <cstdio>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <vector>

constexpr int PORT=6379;
constexpr int BACKLOG_FOR_SOCKET=16;
constexpr int BUFF_SIZE=1024;
constexpr char PING_REPLY[]="+PONG\r\n";
constexpr ssize_t PING_REPLY_LEN=sizeof(PING_REPLY)-1;

bool handle_client(int conn_fd){
        char rbuff[BUFF_SIZE];
        ssize_t rval=read(conn_fd,rbuff,sizeof(rbuff));
        if(rval==0){
           return false;
        }
        if(rval==-1){
            perror("read");
            return false;
        }
        std::cout<<"read function return value: "<<rval<<'\n';
        std::cout.write(rbuff,rval);
        ssize_t wval=write(conn_fd,PING_REPLY,PING_REPLY_LEN);
        if(wval==-1){
            perror("write");
            return false;
        }
        if(wval<PING_REPLY_LEN){
            std::cout<<"Short write happened at fd "<<conn_fd<<'\n';
            return true;
        }
        std::cout<<wval<<" bytes got written on "<<conn_fd<<'\n';
        return true;
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
    std::vector<struct pollfd>fds;
    struct pollfd p{};
    p.fd=listen_fd;
    p.events=POLLIN;
    fds.push_back(p);
    while(true){
        nfds_t fds_len=(nfds_t)fds.size();
        int poll_val=poll(fds.data(),fds_len,-1);
        if(poll_val==-1){
            perror("poll");
            close(listen_fd);
            return 1;
        }
        size_t fds_size=fds.size();
        for(size_t i=0;i<fds_size;i++){
            if(fds[i].fd==listen_fd){
                if(fds[i].revents & POLLIN){
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len=sizeof(client_addr);
                    int conn_fd=accept(listen_fd,(struct sockaddr *)&client_addr,&client_addr_len);
                    if(conn_fd==-1){
                        perror("accept");
                        close(listen_fd);
                        return 1;
                    }
                    std::cout<<"client socket fd is "<<conn_fd<<'\n';
                    struct pollfd c{};
                    c.fd=conn_fd;
                    c.events=POLLIN;
                    fds.push_back(c);
                }   
            }else if(fds[i].revents){
                bool is_client_connected=handle_client(fds[i].fd);
                if(!is_client_connected){
                     close(fds[i].fd);
                     fds[i].fd=-1;
                }
            }
        }
        std::vector<struct pollfd>filtered_fds;
        for(size_t i=0;i<fds.size();i++){
            if(fds[i].fd!=-1){
                filtered_fds.push_back(fds[i]);
            }
        }
        fds=filtered_fds;
    }
}
