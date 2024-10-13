#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<pthread.h>
#include<fcntl.h>
#include<signal.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<netinet/ip.h>
#include<netinet/ip_icmp.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<errno.h>
void syserr(const char* str){
    printf("%s\n",str);
    exit(-1);
}
//最大发送包的数量
#define MAX_PACKETS 4
//包的大小
#define PACKET_SIZE 4096
//最大延迟时间
#define MAX_ALARM_TIME 5

//ICMP结构体大小
#define DATA_LEN 56 
int nsend = 0; //当前发送的包的数量
int nrecved = 0; //当前接收的包的数量

//要发送的包
char sendPacket[PACKET_SIZE];
//要接收的包
char recvPacket[PACKET_SIZE];

struct sockaddr_in fromAddr = {0};
struct timeval recvTimeVal;

//计算校验和 crc32
u_short get_cksum(u_short* icmp,int packetSize){
    int nleft = packetSize;
    int sum = 0;
    u_short* w = icmp;
    u_short ans = 0;
    while(nleft > 1){
        sum += *w++;
        nleft-=2;
    }
    if(nleft == 1){
        *(unsigned char*)(&ans) = *(unsigned char*)w;
        sum += ans; 
    }
    sum = (sum >> 16) + (sum&0xffff);
    sum+=(sum>>16);
    ans = ~sum;
    return ans;
}
//打包
int pack(int nsend){
    struct icmp* pIcmp = (struct icmp*)sendPacket; 
    pIcmp->icmp_type = ICMP_ECHO;
    pIcmp->icmp_code = 0;
    pIcmp->icmp_cksum = 0;
    pIcmp->icmp_id = getuid();
    pIcmp->icmp_seq = nsend;

    int packetSize = 8 + DATA_LEN;
    struct timeval* tval = (struct timeval*)pIcmp->icmp_data;
    gettimeofday(tval,NULL);//发送时间设置到附加数据里面去
    pIcmp->icmp_cksum = get_cksum((u_short*)pIcmp,packetSize);
    return packetSize;
}

//发包
void send_packet(int sfd, struct sockaddr_in* dst_addr,int len){
    int packetSize;
    int r;
    while(nsend < MAX_PACKETS){
        nsend++;
        packetSize = pack(nsend);
        r = sendto(sfd,sendPacket,packetSize,0,(struct socketaddr *)dst_addr,len);
        if(r < 0){
            printf("第%d个包发送失败%m\n",nsend);
            continue;
        }
        printf("第%d个包发送成功\n",nsend);sleep(1);
    }

}

void sigalrmHand(int s){
    printf("------------------ping--------------------\n");
    printf("%d个包发送成功 %d个包接收成功 %%%f丢失！\n",nsend,nrecved,((nsend-nrecved)*1.0)/nsend*100);
    printf("------------------ping--------------------\n");
    exit(-1);
}
void tv_sub(struct timeval* out, struct timeval* in){
    if(out->tv_usec < in->tv_usec) {
        --out->tv_sec;
        out->tv_usec += 1000000;
    }
    out->tv_sec -= in->tv_sec;
     
}
//解包
int unpack(int len,int uid){
    //获取ip地址头长度
    struct ip* ip = (struct ip*)recvPacket;
    int ipHeaderLen = ip->ip_hl << 2;

    //往后偏移，得到icmp地址头
    char* buf = recvPacket;
    struct icmp* icmp = (struct icmp*)(buf+ipHeaderLen);
    len -= ipHeaderLen;
    if(len < 8){
        printf("ICMP包大小小于8\n");
        return -1;
    }
    struct timeval* tvsend;
    if((ICMP_ECHOREPLY == icmp->icmp_type) && (uid == icmp->icmp_id)){
        tvsend = (struct timeval*)icmp->icmp_data;
        tv_sub(&recvTimeVal,tvsend);
        printf("%d byte from %s : icmp_seq : %u ttl = %d rtt = %.3f md\n",
        len,inet_ntoa(fromAddr.sin_addr),icmp->icmp_seq,ip->ip_ttl
        ,recvTimeVal.tv_sec*1000.0 + recvTimeVal.tv_usec/1000);
    }
    else{
        return -1;
    }
    return 0;
}

//收包
void recv_packet(int sfd){  
    int r;
    int recvLen = 0;
    signal(SIGALRM,sigalrmHand);
    while(nrecved < nsend){
        alarm(MAX_ALARM_TIME);
        r = recvfrom(sfd,recvPacket,PACKET_SIZE-1,0,(struct sockaddr *)&fromAddr, &recvLen);
        if(r < 0){
            if(EINTR == errno)continue;
            printf("接收失败:%m\n");
            continue;
        }
        gettimeofday(&recvTimeVal,NULL);
        if(unpack(r,getuid()) == -1){
            continue;
        }
        nrecved++;
    }
}


int main(int argc, char* argv[]){

    //icmp 协议只有root用户权限可以

    if(argc < 2)syserr("请输入目的主机ip地址!");

    //1. 创建socket
    struct protoent* protocal = getprotobyname("icmp");
    if(protocal == NULL)syserr("getprotobyname failed");
    int sfd = socket(AF_INET,SOCK_RAW,protocal->p_proto);
    if(sfd == -1)syserr("create socket failed");
    printf("create socket succeeded\n");

    //2.设置套接字
    int size = 1024 * 50;
    int r = setsockopt(sfd,SOL_SOCKET,SO_RCVBUF,&size,sizeof size);
    if(r == -1)syserr("setsockopt failed");
    printf("setsockopt succeeded\n");

    //3.设置接收机ip
    struct sockaddr_in dst_addr = {0};
    dst_addr.sin_family = AF_INET;

    in_addr_t iAddr = inet_addr(argv[1]);
    struct hostent* host = NULL;
    if(iAddr == INADDR_NONE){
        //不是一个ip地址
        //检查是否是一个域名
        host = gethostbyname(argv[1]);
        if(host == NULL)syserr("错误的ip地址!");
        memcpy(&(dst_addr.sin_addr),host->h_addr_list[0],host->h_length);
        printf("%s\n",inet_ntoa(dst_addr.sin_addr));
    }
    else{
        dst_addr.sin_addr.s_addr = iAddr;
    }

    //4. 打包，发包
    send_packet(sfd,&dst_addr,sizeof dst_addr);

    //5.收包，解包
    recv_packet(sfd);

    //5. 关闭socket
    close(sfd);
    printf("socket closed\n");
    return 0;
}