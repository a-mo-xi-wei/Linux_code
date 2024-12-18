/*************************************************************************
    > File Name: server.c
    > Author: 威桑
    > Mail: 1428206861@qq.com 
    > Created Time: 2024年12月18日 星期三 10时39分31秒
 ************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<pthread.h>
#include<fcntl.h>
#include<errno.h>
#include<arpa/inet.h>
#include<sys/socket.h>
//#include<sys/types.h>

#define SIZE 1024
#define HEAD "HTTP/1.0 200 OK\r\nContent-Type:%s;charset=utf-8\r\nContent-Length:%d\r\n\r\n"
#define TAIL "\r\n\r\n"

char LOGBUF[SIZE];

//存储日志文档
void saveLog(char* str);

int createSocket(const int port);

void acceptSocket(const int sfd);

void* http_thread(void* arg);

void get_http_command(char* buff,char* command);

int make_http_content(char* command,char** content);

int get_file_content(char* filename,char** content);

char* get_file_type(char* command);

int main(int argc,char* argv[]){
	int port = atoi(argv[1]);

    int sfd = createSocket(port);
    acceptSocket(sfd);
	return 0;
}
void saveLog(char *str)
{
    FILE* fp = fopen("myLog.txt","a+");
    printf(str,fp);
    fclose(fp);
}
int createSocket(const int port)
{
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sfd == -1){
        memset(LOGBUF,0,sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : socket error %s\n",__FILE__,__LINE__,strerror(errno));
        saveLog(LOGBUF);
        return -1;
    }
    printf("socket ok!\n");
    int n = 1;
    int r = setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&n,sizeof(n));
    if(r == -1){
        memset(LOGBUF,0,sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : setsockopt error %s\n",__FILE__,__LINE__,strerror(errno));
        saveLog(LOGBUF);
        return -1;
    }
    printf("setsockopt ok!\n");
    struct sockaddr_in sAddr = {0};
    sAddr.sin_port = htons(port);
    sAddr.sin_family = AF_INET;
    sAddr.sin_addr.s_addr = INADDR_ANY;

    r = bind(sfd, (struct sockaddr*)&sAddr, sizeof(sAddr));
    if(r == -1){
        memset(LOGBUF,0,sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : bind error %s\n",__FILE__,__LINE__,strerror(errno));
        saveLog(LOGBUF);
        return -1;
    }
    printf("bind ok!\n");
    r = listen(sfd,100);
    if(r == -1){
        memset(LOGBUF,0,sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : listen error %s\n",__FILE__,__LINE__,strerror(errno));
        saveLog(LOGBUF);
        return -1;
    }
    printf("listen ok!\n");
    return sfd;

}

void acceptSocket(const int sfd){
    struct sockaddr_in cAddr;
    int len = sizeof cAddr;
    pthread_t thrd_t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    memset(&cAddr,0,len);
    int cfd;
    while(1){
        cfd = accept(sfd, (struct sockaddr *)&cAddr, &len);
        if(cfd == -1){
            memset(LOGBUF,0,sizeof(LOGBUF));
            sprintf(LOGBUF,"%s , %d : accept error %s\n",__FILE__,__LINE__,strerror(errno));
            saveLog(LOGBUF);
            return;
        }
        printf("有客户端连上服务器了：%d %s %d!\n",cfd,
                inet_ntoa(cAddr.sin_addr),htons(cAddr.sin_port));
        int* tmp = (int*)malloc(sizeof(int));
        *tmp = cfd;
        pthread_create(&thrd_t, &attr, http_thread, tmp);
    }
    //释放线程资源
    pthread_detach(thrd_t);

}

void *http_thread(void *arg)
{
    if(arg == NULL)return NULL;
    printf("线程启动!\n");
    int cfd = *(int*)arg;
    free((int*)arg);
    char buff[SIZE];
    int r = recv(cfd,buff,SIZE,0);
    if(r < 0){
        memset(LOGBUF,0,sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : recv error %s\n",__FILE__,__LINE__,strerror(errno));
        saveLog(LOGBUF);
    }
    else{
        printf("接收到：%s\n",buff);
        char command[SIZE] = {0};
        get_http_command(buff,command);
        printf("command: %s\n",command);

        char* content = NULL;
        int ilen = make_http_content(command,&content);
   
        printf("要发回去的响应：%s\n",content);

        if(ilen > 0){
            send(cfd,content,ilen,0);
            free(content);
        }
    }
    close(cfd);
    printf("线程结束");
    return NULL;
}

//解析除http请求中get后面的内容
void get_http_command(char *buff, char *command)
{
    char* p_end= buff;
    char* p_start = buff;
    while(*p_start){
        if('/' == *p_start)break;
        p_start++;
    }
    p_start++;

    p_end = strchr(buff,'\n');
    while(p_end != p_start){
        if(' ' == *p_end)break;
        p_end--;
    }
    strncpy(command,p_start,p_end - p_start);
}
//根据请求制作响应
int make_http_content(char *command, char **content)
{
    int fileLen ;
    char* file_buf;
    char headBuf[SIZE] = {0};

    if(command[0] == '0'){
        fileLen = get_file_content("index.html",&file_buf);
    }
    else{
        fileLen = get_file_content(command, &file_buf);
    }

    if(fileLen == 0){
        printf("get file content error! \n");
        return 0;
    }
    
    sprintf(headBuf,HEAD,get_file_type(command),fileLen);

    int headLen = strlen(headBuf);
    int tailLen = strlen(TAIL);
    int sumLen = headLen + fileLen + tailLen;

    *content = (char*)malloc(sumLen);
    if(*content == NULL){
        memset(LOGBUF,0,sizeof(LOGBUF));
            sprintf(LOGBUF,"%s , %d : accept error %s\n",__FILE__,__LINE__,strerror(errno));
            saveLog(LOGBUF);
        return 0;
    }

    char* tmp = *content;
    memcpy(tmp, headBuf,headLen);
    memcpy(tmp+headLen,file_buf,fileLen);
    memcpy(tmp+headLen+fileLen,TAIL,tailLen);

    printf("tmp:%s\n",tmp);

    return sumLen;
}

int get_file_content(char *filename, char **content)
{
    if(filename == NULL)return 0;
    FILE* fp = fopen(filename, "rb");
    if(fp == NULL){
        memset(LOGBUF, 0, sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : fopen %s error %s\n",__FILE__,__LINE__,filename,strerror(errno));
        saveLog(LOGBUF);
        return 0;
    }
    fseek(fp,0,SEEK_END);
    int fileLen = ftell(fp);
    rewind(fp);//fseek(fp,0,SEEK_SET)

    *content = (char*)malloc(fileLen);
    if(*content == NULL){
        memset(LOGBUF, 0, sizeof(LOGBUF));
        sprintf(LOGBUF,"%s , %d : malloc error %s\n",__FILE__,__LINE__,strerror(errno));
        saveLog(LOGBUF);
        return 0;
    }
    fread(*content,fileLen,1,fp);
    fclose(fp);
    return fileLen;
}

char *get_file_type(char *command)
{
    //获取后缀
    char text[56] = {0};
    char* p = command;
    while(*p){
        if('.'  == *p)break;
        p++;
    }
    p++;
    strncpy(text,p,sizeof (text));
    if(strncmp(text,"bmp",3) == 0)
        return "image/bmp";
    if(strncmp(text,"gif",3) == 0)
        return "image/gif";
    if(strncmp(text,"ico",3) == 0)
        return "image/ico";
    if(strncmp(text,"jpg",3) == 0)
        return "image/jpg";
    
    return "text/html";
}
