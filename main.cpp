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
#include <chrono>
#include <random>
#include <cassert>
#include <netinet/tcp.h>

constexpr int PORT=6379;
constexpr int BACKLOG_FOR_SOCKET=16;
constexpr int BUFF_SIZE=1024;
constexpr char PING_REPLY[]="+PONG\r\n";
constexpr ssize_t PING_REPLY_LEN=sizeof(PING_REPLY)-1;
constexpr char PROTOCOL_ERR_REPLY[]="-ERR Protocol error\r\n";
constexpr ssize_t PROTOCOL_ERR_REPLY_LEN=sizeof(PROTOCOL_ERR_REPLY)-1;
constexpr ssize_t POLL_TIMEOUT=100;
constexpr std::chrono::milliseconds EXP_CYCLE_INTERVAL{100};
constexpr std::chrono::milliseconds MAX_EXP_CYCLE_RUNTIME{25};
constexpr bool DEBUG_MODE=false;

enum class Command {PING,ECHO,SET,GET,UNKNOWN};
enum class Optional_arguments {EX,PX,UNKNOWN};

size_t random_num_gen(size_t cap){
    size_t rn;
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, cap - 1);
    rn = pick(rng);
    return rn;
}

long long string_to_int(const std::string &s,size_t st,size_t en){
    long long res=0;
    if(en==st)return -1;
    if(en-st>18)return -1;
    while(st<en){
         if(s[st]-'0'<0 || s[st]-'0'>9)return -1;
            res=res*10+(s[st]-'0');
        st++;
    }
    return res;
}

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

Optional_arguments get_optionalarg_enum(const std::string& command){
    static const std::unordered_map<std::string,Optional_arguments>optional_arguments_map={
        {"EX",Optional_arguments::EX},{"PX",Optional_arguments::PX}
    };
    auto it=optional_arguments_map.find(command);
    if(it!=optional_arguments_map.end())return it->second;
    return Optional_arguments::UNKNOWN;
}

void to_upper(std::string& str){
    for(auto& s:str)s=(char)toupper((unsigned char)s);
}

int set_expiry_handler(std::vector<std::string>& client_commands_and_args,std::unordered_map<std::string,std::chrono::steady_clock::time_point>&expiries){
    switch (get_optionalarg_enum(client_commands_and_args[3]))
    {
    case Optional_arguments::EX:
        {
        long long expiryarg=string_to_int(client_commands_and_args[4],0,client_commands_and_args[4].size());
        if(expiryarg==0)return -1;
        if(expiryarg==-1)return -2;
        auto expiry=std::chrono::steady_clock::now()+std::chrono::seconds(expiryarg);
        expiries[client_commands_and_args[1]]=expiry;
        }
        break;
    case Optional_arguments::PX:
        {
        long long expiryarg=string_to_int(client_commands_and_args[4],0,client_commands_and_args[4].size());
        if(expiryarg==0)return -1;
        if(expiryarg==-1)return -2;
        auto expiry=std::chrono::steady_clock::now()+std::chrono::milliseconds(expiryarg);
        expiries[client_commands_and_args[1]]=expiry;
        }
        break;
    case Optional_arguments::UNKNOWN:
        return -3;
        break;
    }
    return 0;
}

std::string client_command_handler(std::vector<std::string>& client_commands_and_args,std::unordered_map<std::string, std::string>&db,std::unordered_map<std::string,std::chrono::steady_clock::time_point>&expiries){
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
        if(client_commands_and_args_size<3 || client_commands_and_args_size==4){
            response="-ERR wrong number of arguments for 'SET' command\r\n";
        }else if(client_commands_and_args_size>5){
            response="-ERR syntax error\r\n";
        }
        else{
            if(client_commands_and_args_size==5)
            {
                int set_expiry_handler_err_no=set_expiry_handler(client_commands_and_args,expiries);
                switch (set_expiry_handler_err_no)
                {
                case -1:
                    response="-ERR invalid expire time in 'set' command\r\n";
                    return response;
                    break;
                case -2:
                    response="-ERR value is not an integer or out of range\r\n";
                    return response;
                    break;
                case -3:
                    response="-ERR syntax error\r\n";
                    return response;
                    break;
                default:
                    break;
                }
            }else{
                expiries.erase(client_commands_and_args[1]);
            }
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
            auto ite=expiries.find(client_commands_and_args[1]);
            if(ite==expiries.end() || ite->second>std::chrono::steady_clock::now()){
               response=bstring_transform(it->second);
            }
            else{
               response="$-1\r\n";
               expiries.erase(ite);
               db.erase(it);
            }
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
      start++;
      long long len_client_data_arr=string_to_int(client_data,start,nloc);
      if(len_client_data_arr==-1)return -1;
      start=nloc+2;
      for(int i=0;i<len_client_data_arr;i++){
        if(start>=client_data_size)return 0;
        if(client_data[start]!='$')return -1;
        else start++;
        size_t ntloc=client_data.find("\r\n",start);
        if(ntloc==std::string::npos)return 0;
        long long len_client_data=string_to_int(client_data,start,ntloc);
        if(len_client_data==-1)return -1;
        start=ntloc+2;
        if(start+len_client_data+2>client_data_size)return 0;
        if(client_data[start+len_client_data]!='\r' || client_data[start+len_client_data+1]!='\n')return -1;
        client_commands_and_args.push_back(client_data.substr(start,len_client_data));
        start+=len_client_data+2;
      }
      return start;
}

bool handle_client(int conn_fd,std::string& client_data,std::unordered_map<std::string, std::string>&db,std::unordered_map<std::string,std::chrono::steady_clock::time_point>&expiries){
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
        if(DEBUG_MODE){
            std::cout<<"read function return value: "<<rval<<'\n';
            std::cout.write(rbuff,rval);
            std::cout<<'\n';
        }
        client_data.append(rbuff,rval);
        if(DEBUG_MODE)std::cout<<"Client Data: "<<client_data<<"\n Client Data Length: "<<client_data.length()<<'\n';
        int pval=client_data_parser(client_data,client_commands_and_args);
        std::string data_to_write="";
        bool is_error=false;
        while(pval>0){
            if(!client_commands_and_args.empty()){
                std::string reponse_to_client=client_command_handler(client_commands_and_args,db,expiries);
                data_to_write+=reponse_to_client;
            }
            client_data.erase(0,pval);
            client_commands_and_args.clear();
            pval=client_data_parser(client_data,client_commands_and_args);
        }
        if(pval==-1){
            data_to_write+=PROTOCOL_ERR_REPLY;
            is_error=true;
        }
        ssize_t data_to_write_len=data_to_write.length();
        if(data_to_write_len!=0){
            ssize_t wval=write(conn_fd,data_to_write.data(),data_to_write_len);
            if(wval==-1){
                perror("write");
                return false;
            }
            if(wval<data_to_write_len){
                std::cout<<"Short write happened at fd "<<conn_fd<<'\n';
                return false;
            }
            if(DEBUG_MODE)std::cout<<wval<<" bytes got written on "<<conn_fd<<'\n';
        }
        return !is_error;
        return true;
}

void expiration_handler(std::unordered_map<std::string,std::string>&db,std::unordered_map<std::string,std::chrono::steady_clock::time_point>&key_expiries){
    if(key_expiries.empty())return;
    size_t tot_expiries=key_expiries.size();
    size_t tot_key_expired_cnt=0;
    size_t bucket_len=key_expiries.bucket_count();
    size_t random_num=random_num_gen(bucket_len);
    size_t iter_count=20;
    size_t max_key_tolook_cnt=0;
    size_t tot_bkt_tolook_cnt=100;
    std::chrono::steady_clock::time_point start_time=std::chrono::steady_clock::now();
    while(tot_bkt_tolook_cnt>0 && iter_count && std::chrono::steady_clock::now()-start_time<=MAX_EXP_CYCLE_RUNTIME){
        for(auto it=key_expiries.begin(random_num);it!=key_expiries.end(random_num) && iter_count && std::chrono::steady_clock::now()-start_time<=MAX_EXP_CYCLE_RUNTIME;){
            if(it->second<=std::chrono::steady_clock::now()){
                auto itdb=db.find(it->first);
                std::string key=it->first;
                it++;
                key_expiries.erase(key);
                assert(db.end()!=itdb);
                db.erase(itdb);
                tot_key_expired_cnt++;
                max_key_tolook_cnt++;
            }else{
                it++;
            }
            iter_count--;
        }
        tot_bkt_tolook_cnt--;
        random_num=random_num_gen(bucket_len);    
        if(iter_count==0 && max_key_tolook_cnt>5){
            iter_count=20;
            tot_bkt_tolook_cnt=100;
            max_key_tolook_cnt=0;
        }
    }
    
    if(tot_key_expired_cnt>0 && DEBUG_MODE)std::cout<<"expiration cycle deleted expired key : "<<tot_key_expired_cnt<<" total remaining : "<<tot_expiries-tot_key_expired_cnt<<'\n';
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
    std::chrono::steady_clock::time_point expcycle_last_run_time=std::chrono::steady_clock::now();
    std::vector<struct pollfd>fds;
    std::unordered_map<std::string,std::string>db;
    std::unordered_map<int,std::string>clients_data;
    std::unordered_map<std::string,std::chrono::steady_clock::time_point>key_expiries;
    struct pollfd p{};
    p.fd=listen_fd;
    p.events=POLLIN;
    fds.push_back(p);
    while(true){
        nfds_t fds_len=(nfds_t)fds.size();
        int poll_val=poll(fds.data(),fds_len,POLL_TIMEOUT);
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
                    if(DEBUG_MODE)std::cout<<"client socket fd is "<<conn_fd<<'\n';
                    int client_sock_opt_val=1;
                    if(setsockopt(conn_fd,IPPROTO_TCP,TCP_NODELAY,&client_sock_opt_val,sizeof(client_sock_opt_val))==-1){
                        perror("setsockopt");
                        close(conn_fd);
                        continue;
                    }
                    struct pollfd c{};
                    c.fd=conn_fd;
                    c.events=POLLIN;
                    fds.push_back(c);  
                }   
            }else if(fds[i].revents){
                bool is_client_connected=handle_client(fds[i].fd,clients_data[fds[i].fd],db,key_expiries);
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

        if(std::chrono::steady_clock::now()-expcycle_last_run_time>=EXP_CYCLE_INTERVAL){
            expcycle_last_run_time=std::chrono::steady_clock::now();
            expiration_handler(db,key_expiries);
        }
    }
}
