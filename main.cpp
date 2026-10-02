#include <iostream>
#include <sys/socket.h>
#include <cstdio>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <utility>

constexpr int PORT=6379;
constexpr int BACKLOG_FOR_SOCKET=16;
constexpr int BUFF_SIZE=1024;
constexpr char PING_REPLY[]="+PONG\r\n";
constexpr ssize_t PING_REPLY_LEN=sizeof(PING_REPLY)-1;
constexpr char PROTOCOL_ERR_REPLY[]="-ERR Protocol error\r\n";
constexpr ssize_t PROTOCOL_ERR_REPLY_LEN=sizeof(PROTOCOL_ERR_REPLY)-1;

enum class Command {PING,ECHO,SET,GET,UNKNOWN};

std::string bstring_transform(const std::string &input){
    std::string tinput="";
    size_t value_len=input.length();
    tinput+='$'+std::to_string(value_len)+"\r\n"+input+"\r\n";
    return tinput;
}

Command get_command_enum(const std::string& command){
    static const std::unordered_map<std::string,Command>commands_map={
        {"PING",Command::PING},{"ECHO",Command::ECHO},{"SET",Command::SET},{"GET",Command::GET}
    };
    auto it=commands_map.find(command);
    if(it!=commands_map.end())return it->second;
    return Command::UNKNOWN;
}

void to_upper(std::string& str){
    for(auto& s:str)s=(char)toupper((unsigned char)s);
}

std::string client_command_handler(std::vector<std::string>& client_commands_and_args,std::unordered_map<std::string, std::string>&db){
    std::string response="";
    size_t client_commands_and_args_size=client_commands_and_args.size();
    to_upper(client_commands_and_args[0]);
    switch (get_command_enum(client_commands_and_args[0]))
    {
    case Command::PING:
        if(client_commands_and_args_size>1){
            response="-ERR wrong number of arguments for 'PING' command\r\n";
        }else{
            response="+PONG\r\n";
        }
        break;
    case Command::ECHO:
        if(client_commands_and_args_size!=2){
            response="-ERR wrong number of arguments for 'ECHO' command\r\n";
        }else{
            response=bstring_transform(client_commands_and_args[1]);
        }
        break;
    case Command::SET:
        if(client_commands_and_args_size<3){
            response="-ERR wrong number of arguments for 'SET' command\r\n";
        }else if(client_commands_and_args_size>3){
            response="-ERR syntax error\r\n";
        }
        else{
            db[std::move(client_commands_and_args[1])]=std::move(client_commands_and_args[2]);
            response+="+OK\r\n";
        }
        break;
    case Command::GET:
        if(client_commands_and_args_size!=2){
            response="-ERR wrong number of arguments for 'GET' command\r\n";
        }else{
            auto it=db.find(client_commands_and_args[1]);
            if(it==db.end()){
                response="$-1\r\n";
                break;
            }
            response=bstring_transform(it->second);
        }
        break;
    case Command::UNKNOWN:
        response="-ERR unknown command '"+client_commands_and_args[0]+"'\r\n";
        break;
    }
    return response;
}

int client_data_parser(const std::string& client_data,std::vector<std::string>& client_commands_and_args){
      size_t client_data_size=client_data.length();
      if(client_data.length()==0)return 0;
      size_t start=0;
      if(client_data[start]!='*')return -1;
      size_t nloc=client_data.find("\r\n",start);
      if(nloc==std::string::npos)return 0;
      int len_client_data_arr=0;
      start++;
      while(start<nloc){
        if(client_data[start]-'0'<0 || client_data[start]-'0'>9)return -1;
        len_client_data_arr=len_client_data_arr*10+(client_data[start]-'0');
        start++;
      }
      start=nloc+2;
      for(int i=0;i<len_client_data_arr;i++){
        if(start>=client_data_size)return 0;
        if(client_data[start]!='$')return -1;
        else start++;
        size_t ntloc=client_data.find("\r\n",start);
        if(ntloc==std::string::npos)return 0;
        int len_client_data=0;
        while(start<ntloc){
            if(client_data[start]-'0'<0 || client_data[start]-'0'>9)return -1;
            len_client_data=len_client_data*10+(client_data[start]-'0');
            start++;
        }
        start=ntloc+2;
        if(start+len_client_data+2>client_data_size)return 0;
        if(client_data[start+len_client_data]!='\r' || client_data[start+len_client_data+1]!='\n')return -1;
        client_commands_and_args.push_back(client_data.substr(start,len_client_data));
        start+=len_client_data+2;
      }
      return start;
}

bool handle_client(int conn_fd,std::string& client_data,std::unordered_map<std::string, std::string>&db){
        std::vector<std::string> client_commands_and_args;
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
        std::cout<<'\n';
        client_data.append(rbuff,rval);
        std::cout<<"Client Data: "<<client_data<<"\n Client Data Length: "<<client_data.length()<<'\n';
        int pval=client_data_parser(client_data,client_commands_and_args);
        while(pval>0){
        if(!client_commands_and_args.empty()){
                std::string reponse_to_client=client_command_handler(client_commands_and_args,db);
                ssize_t response_to_client_len=reponse_to_client.length();
                ssize_t wval=write(conn_fd,reponse_to_client.data(),response_to_client_len);
                if(wval==-1){
                    perror("write");
                    return false;
                }
                if(wval<response_to_client_len){
                    std::cout<<"Short write happened at fd "<<conn_fd<<'\n';
                    return true;
                }
                std::cout<<wval<<" bytes got written on "<<conn_fd<<'\n';
            }
            client_data.erase(0,pval);
            client_commands_and_args.clear();
            pval=client_data_parser(client_data,client_commands_and_args);
        }
        if(pval==-1){
            ssize_t wval=write(conn_fd,PROTOCOL_ERR_REPLY,PROTOCOL_ERR_REPLY_LEN);
            if(wval==-1){
                perror("write");
                return false;
            }
            if(wval<PROTOCOL_ERR_REPLY_LEN){
                std::cout<<"Short write happened at fd "<<conn_fd<<'\n';
                return true;
            }
            std::cout<<wval<<" bytes got written on "<<conn_fd<<'\n';
            return false;
        }
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
    std::unordered_map<std::string,std::string>db;
    std::unordered_map<int,std::string>clients_data;
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
                bool is_client_connected=handle_client(fds[i].fd,clients_data[fds[i].fd],db);
                if(!is_client_connected){
                     clients_data.erase(fds[i].fd);
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
