#include <iostream>
#include <sys/socket.h>

int main(){
    int socket_fd=socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd==-1)perror("socket_fd");
    else std::cout<<socket_fd<<std::endl;
    return 0;
}