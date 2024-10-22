#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#define NUM  10

//线程函数参数
struct tp_work_arg_s{
	void* arg;
};

//线程的工作函数
struct tp_work_s{
	void* (*process_job)(void* arg);
};

typedef struct tp_work_arg_s tp_work_arg;
typedef struct tp_work_s tp_work;

//线程类
struct tp_thread_info_s{
	pthread_t			thread_id;		//线程id
	int					status;			//线程状态    1 忙  0 闲
	int					need_exit;		//退出标记    1 退出 0 不用退出

	pthread_mutex_t 	thread_lock;	//互斥
	pthread_cond_t		thread_cond;	//条件变量

	tp_work*			th_work;
	tp_work_arg*		th_arg;



	struct tp_thread_info_s*	next;
	struct tp_thread_info_s* 	prev;
};

//线程池类
struct tp_thread_pool_s{
	int					min_th_num;			//线程池中线程最少数量
	int					cur_th_num;			//当前线程数
	int					max_th_num;			//线程池中线程最大数量
	int					idle_th_num;		//当前空闲线程数

	pthread_mutex_t		tp_lock;			//互斥

	pthread_t 			manage_thread_id;	//管理线程id
	struct tp_thread_info_s*  thread_info;	//线程链表头节点
};

typedef struct tp_thread_info_s tp_thread_info;
typedef struct tp_thread_pool_s tp_thread_pool;

//创建线程池
tp_thread_pool* create_thread_pool(int min,int max);
//向线程池中添加任务
int tp_add_job(tp_thread_pool* pool,tp_work* worker,tp_work_arg* arg);
//创建线程池中的线程
tp_thread_info* create_thread(int num);
//初始化线程池
int init_thread_pool(tp_thread_pool* pool);
//管理线程的函数
void* tp_manager_thread(void* arg);
//初始化线程
int init_thread(tp_thread_pool* pool,tp_thread_info* info);
//通知管理线程增加一个线程
tp_thread_info* tp_add_pthread(tp_thread_pool* pool);
//工作函数
void* myWork(void* arg);



int main(){
	int num = NUM;
	//int working[NUM] = {0};
	int* working = (int*)malloc(sizeof(int)*NUM);
	//1. 创建线程池
	tp_thread_pool* pool = create_thread_pool(3,20);

	//2. 任务池
	tp_work* worker = (tp_work*)malloc(sizeof(tp_work));
	worker->process_job = myWork;


	for(int i=0;i<num;i++){
		tp_work_arg* arg = (tp_work_arg*) malloc(sizeof(tp_work_arg));
		working[i] = i+60;
		arg->arg = (void*)(working+i);

		printf("working[%d]:%d\n",i,*(int*)(arg->arg));
		tp_add_job(pool,worker,arg);
	}

	sleep(100);
}


//创建线程池
tp_thread_pool* create_thread_pool(int min,int max){
	//开内存
	tp_thread_pool* pool = (tp_thread_pool*)malloc(sizeof(tp_thread_pool));
	if(NULL == pool) {
		printf("create_thread_pool函数中开内存失败:%m\n");
		return NULL;
	}

	//赋值
	//内部修正
	if(min < 3) min = 3;
	if(max > 200) max = 300;

	pool->min_th_num = min;
	pool->max_th_num = max;

	pool->idle_th_num = pool->cur_th_num = min;

	pthread_mutex_init(&(pool->tp_lock),NULL);

	//以防万一
	if(pool->thread_info) free(pool->thread_info);

	//开内存
	pool->thread_info = create_thread(pool->cur_th_num);

	if(NULL == pool->thread_info){
		free(pool);
		return NULL;
	}

	init_thread_pool(pool);
	//返回
	return pool;
}
//初始化线程池
int init_thread_pool(tp_thread_pool* pool){
	tp_thread_info* thread_info;
	//创建并初始化线程池中的线程对象
	for(thread_info=pool->thread_info;
		thread_info!=NULL;
		thread_info = thread_info->next){
		if(init_thread(pool,thread_info)){
			printf("初始化线程成功：%lu\n",
				thread_info->thread_id);
		}else{
			printf("初始化线程失败!\n");
			return 0;
		}
	}



	//创建管理线程
	int ret = pthread_create(&(pool->manage_thread_id),
		NULL,
		tp_manager_thread,pool);
	if(-1 == ret){
		printf("创建管理线程失败:%m\n");
		return 0;
	}else{
		printf("创建管理线程成功，管理线程id:%lu\n",
			pool->manage_thread_id);
	}
	return 1;
}
//向线程池中添加任务
int tp_add_job(tp_thread_pool* pool,tp_work* worker,tp_work_arg* arg){
	//printf("向线程池中添加任务\n");
	tp_thread_info* info;
	tp_thread_info* new;
	//去线程池中找空闲线程，然后把任务分配给它来执行
	while(1){
		for(info = pool->thread_info;
			info!=NULL;
			info = info->next){

			if(0 == info->status){
				printf("线程%lu空闲\n",info->thread_id);

				//设置为非空闲
				info->status = 1;
				pool->idle_th_num--;

				pthread_mutex_unlock(&(info->thread_lock));

				info->th_work = worker;
				info->th_arg = arg;

				//唤醒该线程
				pthread_cond_signal(&(info->thread_cond));
			}//end of 	if(0 == info->status){

		}//end of for

		//没有空闲线程  新增线程
		pthread_mutex_lock(&(pool->tp_lock));

		new = tp_add_pthread(pool);

		if(new){
			new->th_work = worker;
			new->th_arg = arg;

			pthread_cond_signal(&(new->thread_cond));
			pthread_mutex_unlock(&(pool->tp_lock));
			return 1;
		}

		pthread_mutex_unlock(&(pool->tp_lock));
	}//end of while(1)

	return 1;
}
//创建线程池中的线程
tp_thread_info* create_thread(int num){
	if(num<=0) return NULL;
	//双链表

	tp_thread_info* head;
	tp_thread_info* cur;
	tp_thread_info* prev;

	//头节点
	head = (tp_thread_info*)malloc(sizeof(tp_thread_info));
	if(NULL == head) {
		printf("create_thread函数中申请内存失败:%m\n");
		return NULL;
	}
	num--;

	//头节点之后的节点
	cur = head;
	prev = NULL;

	while(num){
		cur->prev = prev;
		cur->next = (tp_thread_info*)malloc(sizeof(tp_thread_info));
		prev = cur;
		cur = cur->next;

		num--;
	}

	cur->next = NULL;

	return head;
}

//管理线程的函数
void* tp_manager_thread(void* arg){
	//空闲线程数过多 删掉一些  没有或者空闲线程数太少，创建一些空闲线程
	int cur,idle,min,exit_num;

	tp_thread_pool* this = (tp_thread_pool*)arg;
	//统计三次
	int idle_arg[3] = {0};
	int averIdle = 0;
	tp_thread_info* info = NULL;
	while(1){
		cur = this->cur_th_num;
		idle = this->idle_th_num;
		min = this->min_th_num;
		exit_num=0;
		idle_arg[2] = idle;

		averIdle = (idle_arg[0] + idle_arg[1] + idle_arg[2])/3;
		printf("*****************服务器当前状态****************\n");
		printf("总线程数:%d\n",cur);
		printf("当前空闲线程数:%d\n",idle);
		printf("平均空闲线程数:%d\n",averIdle);
		
		if(averIdle>(cur/2)){//空闲线程过多
			exit_num = cur/5;
		}
		//修正
		if(cur-exit_num < min){
			exit_num = cur-min;
		}
		printf("需要销毁%d个线程!\n",exit_num);

		//遍历链表 跳过第一个
		for(info = this->thread_info->next ;
			info!=NULL && exit_num>1;
			info = info->next){
			if(0!=info->status){
				pthread_mutex_unlock(&(info->thread_lock));
				continue;
			}

			info->need_exit = 1;//标记为退出线程
			pthread_mutex_unlock(&(info->thread_lock));
			exit_num--;
			pthread_cond_signal(&(info->thread_cond));
		}

		idle_arg[0] = idle_arg[1];
		idle_arg[1] = idle_arg[2];

		//每隔5s统计一次
		sleep(5);
	}

	return NULL;
}
//初始化线程 返回0表示失败
int init_thread(tp_thread_pool* pool,tp_thread_info* info){
	//锁
	pthread_mutex_init(&(info->thread_lock),NULL);
	pthread_cond_init(&(info->thread_cond),NULL);

	int r = pthread_create(&(info->thread_id),NULL,
		myWork,pool);
	if(-1 == r){
		printf("init_thread函数中创建并启动线程失败:%m\n");
		return 0;
	}

	//设置线程分离
	pthread_detach(info->thread_id);
	info->status = 0;

	return 1;
}
//通知管理线程增加一个线程
tp_thread_info* tp_add_pthread(tp_thread_pool* pool){
	if(pool->cur_th_num >= pool->max_th_num) return NULL;

	//添加一个线程节点到pool->thread_info末尾

	tp_thread_info* tail = pool->thread_info;
	while(tail->next) tail = tail->next;

	tp_thread_info* new = (tp_thread_info*)malloc(sizeof(tp_thread_info));
	if(NULL == new) return NULL;

	new->next = NULL;
	new->prev = tail;
	tail->next = new;

	pthread_mutex_init(&(new->thread_lock),NULL);
	pthread_cond_init(&(new->thread_cond),NULL);

	pool->cur_th_num++;

	int r = pthread_create(&(new->thread_id),NULL,myWork,pool);
	if(-1 == r) {
		printf("tp_add_pthread函数中创建并启动线程失败:%m\n");
		return NULL;
	}

	pthread_detach(new->thread_id);
	new->status = 0;
	pool->idle_th_num++;
	printf("tp_add_pthread:增加一个新线程成功:%lu\n",new->thread_id);
	return new;
}
//工作函数
void* myWork(void* arg){
	printf("work-------------start!\n");
	/*
	tp_work_arg* a = (tp_work_arg*)arg;
	printf("a:%p\n",a);
	printf("arg:%p\n",(int*)(a->arg));
	printf("val:%d\n",*(int*)(a->arg));
	int val = *((int*)(a->arg));

	
	printf("工作函数: %d\n",val);
	sleep(5);
	printf("工作函数%d结束!\n",val);
	*/
	sleep(5);
	printf("work------------------------end!\n");
}
