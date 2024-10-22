#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<pthread.h>
#include<fcntl.h>
void syserr(char* str){
	perror(str);
	exit(1);
}

//任务类
struct job{
    void* (*callback_function)(void* arg);
    void *arg; 
    struct job* pNext;
};

//线程池类
struct pthreadPool{
    pthread_t* pthreads;    //数组方式存储所有线程id
    struct job* head;       //任务池头结点
    struct job* tail;       //任务池尾节点
    int nums;                //当前线程数
    int quque_max_num;      //最大线程数限制
    //先策划稿同步
    pthread_mutex_t mutex;  //互斥量
    pthread_cond_t queue_empty; //当前池空
    pthread_cond_t queue_not_empty; //当前池不空
    pthread_cond_t queue_not_full; //不满

    int queue_job_nums;      //当前任务数
    int queue_close;         //是否关闭任务池
    int pool_close;          //是否关闭线程池
};
//任务函数
void* work(void* arg);
//线程函数
void* pthread_function(void* arg);
//初始化线程池
struct pthreadPool* pthreadPool_init(int pthread_num,int queue_max_num);
//添加任务到线程池中
int add_job2pthreadPool(struct pthreadPool* pool,void* (*func)(void* arg),void* arg);
//销毁线程池
int destory_pthreadPool(struct pthreadPool* pool);
int main(){
	struct pthreadPool* pool = pthreadPool_init(5,20);
/*  这是错误的！！！！
    for (int i = 1; i <= 20; i++) {
        char str[50]; // 在每次循环中，局部定义一个字符数组
        sprintf(str, "第%d个任务", i); // 将任务信息格式化到 str 中
        add_job2pthreadPool(pool, work, str); // 将 str 传递给线程池
    }
如果 add_job2pthreadPool 中没有复制或深拷贝这个字符串的内容，
而只是存储了它的指针（str 的地址），那么当循环结束后，所有任务都会引用同一个 str 内存位置，
导致所有任务实际上指向的是最后一次迭代的 str 值（即 "第20个任务"）。
    
*/
    for (int i = 1; i <= 20; i++) {
        char* str = (char*)malloc(50 * sizeof(char)); // 为每个任务动态分配内存
        sprintf(str, "第%d个任务", i);
        add_job2pthreadPool(pool, work, str); // 假设 add_job2pthreadPool 负责释放内存
    }

    /*add_job2pthreadPool(pool,work,"第1个任务");
	add_job2pthreadPool(pool,work,"第2个任务");
	add_job2pthreadPool(pool,work,"第3个任务");
	add_job2pthreadPool(pool,work,"第4个任务");
	add_job2pthreadPool(pool,work,"第5个任务");
	add_job2pthreadPool(pool,work,"第6个任务");
	add_job2pthreadPool(pool,work,"第7个任务");
	add_job2pthreadPool(pool,work,"第8个任务");
	add_job2pthreadPool(pool,work,"第9个任务");
	add_job2pthreadPool(pool,work,"第10个任务");
	add_job2pthreadPool(pool,work,"第11个任务");
	add_job2pthreadPool(pool,work,"第12个任务");
	add_job2pthreadPool(pool,work,"第13个任务");
	add_job2pthreadPool(pool,work,"第14个任务");
	add_job2pthreadPool(pool,work,"第15个任务");
	add_job2pthreadPool(pool,work,"第16个任务");
	add_job2pthreadPool(pool,work,"第17个任务");
	add_job2pthreadPool(pool,work,"第18个任务");
	add_job2pthreadPool(pool,work,"第19个任务");
	add_job2pthreadPool(pool,work,"第20个任务");*/
    sleep(30);
    destory_pthreadPool(pool);
	return 0;
}
void* work(void* arg){
    char* str = (char*)arg;
    printf("任务函数:%s\n",str);
    sleep(3);
    printf("%s结束!\n",str);
}
void* pthread_function(void* arg){
    struct pthreadPool* pool = (struct pthreadPool*)arg;
    if(pool == NULL)return NULL;
    //获取具体任务 
    struct job* pJob = NULL;
    while(1){
        pthread_mutex_lock(&(pool->mutex));
        //监督线程的工作

        //如果队列为空就阻塞，队列非空就往下执行
        while((pool->queue_job_nums == 0) && !(pool->queue_close)){
            printf("队列为空，阻塞！\n");
            pthread_cond_wait(&(pool->queue_not_empty),&(pool->mutex));//避免死锁和忙等

        }

        //线程池是否关闭
        if(pool->queue_close){
            printf("线程池关闭了，结束线程！\n");
            pthread_mutex_unlock(&(pool->mutex));
            pthread_exit(NULL);
        }

        //当前任务数 -1
        pool->queue_job_nums--;
        pJob = pool->head;

        if(pool->queue_job_nums == 0){
            pool->head = pool->tail = NULL;
            printf("销毁线程函数！\n");
            pthread_cond_signal(&(pool->queue_empty));
        }   
        else{
            pool->head = pJob->pNext;
        }

        //队列不满，添加新任务
        if(pool->queue_job_nums < pool->quque_max_num){
            printf("队列不满，添加新任务。");
            pthread_cond_broadcast(&(pool->queue_not_full));
        }

        pthread_mutex_unlock(&(pool->mutex));
        //执行线程函数
        (*(pJob->callback_function))(pJob->arg);
        free(pJob);
        pJob = NULL;
    }
}
struct pthreadPool* pthreadPool_init(int pthread_num,int queue_max_num){ 
    //1. 开内存
    struct pthreadPool* pool = (struct pthreadPool*)malloc(sizeof(struct pthreadPool));
    if(pool == NULL)syserr("pthreadPool_init error %m");
    //2.成员赋值
    pool->nums = pthread_num;
    pool->quque_max_num = queue_max_num;
    pool->queue_job_nums = 0;
    pool->queue_close = pool->pool_close = 0;
    pool->pthreads = (pthread_t*)malloc(sizeof(pthread_t)*pthread_num);

    pthread_mutex_init(&(pool->mutex), NULL);
    pthread_cond_init(&(pool->queue_empty), NULL);
    pthread_cond_init(&(pool->queue_not_empty), NULL);
    pthread_cond_init(&(pool->queue_not_full), NULL);

    pool->head = pool->tail = NULL;

    //创建线程，并阻塞
    for(int i=0;i<pool->nums;i++){
        pthread_create(&(pool->pthreads[i]),NULL,pthread_function,(void*)pool);
    }
    //3.返回
    return pool;
}

int add_job2pthreadPool(struct pthreadPool* pool,void* (*func)(void* arg),void* arg){
    if(pool == NULL || func == NULL || arg == NULL){
        printf("add_job2pthreadPool 传参有问题!\n");
        return -1;
    }
    
    pthread_mutex_lock(&(pool->mutex));
    //阻塞

    //队列是否满
    while((pool->queue_job_nums >= pool->quque_max_num) && 
        !(pool->queue_close || pool->pool_close)){
            printf("队列已满，阻塞\n");
            pthread_cond_wait(&(pool->queue_not_full),&(pool->mutex));
    }

    //关闭
    if(pool->queue_close || pool->pool_close){
        pthread_mutex_unlock(&(pool->mutex));
        return -1;
    }  

    //往任务池中添加任务

    //1.开内存
    struct job* pJob = (struct job*)malloc(sizeof(struct job));
    if(pJob == NULL){
        printf("add_job2pthreadPool malloc job failed\n");
        pthread_mutex_unlock(&(pool->mutex));
        return -1;
    }
    
    //2.成员赋值
    pJob->callback_function = func;
    pJob->arg = arg;
    pJob->pNext = NULL;

    //3.入链 尾插法
    if(pool->head == NULL){
        pool->head = pool->tail = pJob;
        //发信号 队列非空
        pthread_cond_broadcast(&(pool->queue_not_full));
    }
    else{
        pool->tail->pNext = pJob;
        pool->tail = pJob;

        /*头插法
        pJob->pNext = pool->head;
        pool->head = pJob;
        */
    }
    //当前任务数增加
    pool->queue_job_nums++;

    pthread_mutex_unlock(&(pool->mutex));

}

int destory_pthreadPool(struct pthreadPool* pool){
    printf("销毁线程池！\n");

    if(pool){
        //释放id内存
        free(pool->pthreads);

        //如果还有任务
        struct job* pJob = pool->head;
        while(pJob){
            pool->head = pJob->pNext;
            free(pJob);
            pJob = pool->head;
        }

        //释放本身内存
        free(pool);
        return 0;
    }
    return -1;
}