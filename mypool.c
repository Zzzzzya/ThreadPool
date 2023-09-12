//使用C实现一个线程池
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>

   //链表操作宏定义
//1.插入尾部
#define LIST_INSERT(item,head,tail) do {        \
    (item)->next=NULL;                          \
    (item)->prev=NULL;                          \
    if((tail)==NULL) {                          \
        (head)=(item);                          \
        (tail)=(item);                          \
    }                                           \
    else{                                       \
        (tail)->next=(item);                    \
        (item)->prev=(tail);                    \
        (tail)=(item);                          \
    }                                           \
}while(0)                                       \


//2.从头部取出
#define LIST_REMOVE(item,head,tail) do {        \
    if((item)==(tail)){                         \
        (head)=NULL;                            \
        (tail)=NULL;                            \
    }                                           \
    else{                                       \
        (item)->next->prev=NULL;                \
        (head)=(item)->next;                    \
    }                                           \
}while(0)


    //数据结构定义
//1.任务定义
typedef struct nTask{
    //双向链表
    struct nTask* prev;
    struct nTask* next;

    //任务函数 &&  任务函数所需数据
    void (*task_func)(struct nTask* task);
    void *user_data;
}nTask,*nTaskLink;

//2.工作队列定义
typedef struct nWorker{
    //双向链表
    struct nWorker* prev;
    struct nWorker* next;
    
    //workerid
    int workerid;

    //状态量 是否要被删除 1表示存在 0表示将被删除
    int is_exit;

    //所属的池
    struct pool* pool;

    //拥有的线程
    pthread_t threadid;
}nWorker,*nWorkerLink;

//3.线程池定义
typedef struct pool{
    //任务队列 头尾指针
    struct nTask* task_head;
    struct nTask* task_tail; 

    //工作队列 头尾指针
    struct nWorker* worker_head;
    struct nWorker* worker_tail;

    //锁
    pthread_mutex_t mutex;
    
    //条件变量
    pthread_cond_t cond;
}Pool,*PoolLink;

//data定义
typedef struct pair{
    int workerid;
    unsigned long threadid;
    int idx;
}pair;

//接口函数定义说明
static void* ThreadPoolCallBack(void *arg);//线程相应函数
int ThreadPoolInit(PoolLink pool,int num_of_workers);//线程池初始化函数
int ThreadPoolDestroy(PoolLink pool);//线程池销毁函数
int ThreadPoolTaskPush(PoolLink pool,nTaskLink task);//加入任务函数

//接口函数实现
int ThreadPoolInit(PoolLink pool,int num_of_workers) {
    //1.检查输入
    if(pool==NULL){
        printf("ERROR! The pool to be initial is NULL!\n");
        return -1;
    }

    if(num_of_workers<1){
        num_of_workers=1;
    }

    //2.初始化pool中各数据
    memset(pool,0,sizeof(pool));

    pthread_mutex_init(&pool->mutex,NULL);//初始化锁
    pthread_cond_init(&pool->cond,NULL);//初始化条件变量

    for(int i=0;i<num_of_workers;i++){
        nWorkerLink worker=(nWorkerLink)malloc(sizeof(nWorker));
        
        if(worker == NULL){
            perror("malloc worker:");
            return -2;
        }

        memset(worker,0,sizeof(worker));

        worker->is_exit=1;//该worker存在
        worker->workerid=i;//worker编号
        worker->pool=pool;//该worker所属该池

        int ret=pthread_create(&worker->threadid,NULL,ThreadPoolCallBack,worker);
        if(ret){
            perror("pthread_create:");
            return -3;
        }

        LIST_INSERT(worker,pool->worker_head,pool->worker_tail);
    }

    //初始化成功
    return 0;
}
int ThreadPoolDestroy(PoolLink pool){
    for(nWorkerLink worker=pool->worker_head;worker;worker=worker->next) {
        worker->is_exit=0; //需要被删除
    }


    //发送广播 让所有再等待的worker不再等待
    pthread_mutex_lock(&pool->mutex);
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    pool->task_head=NULL;
    pool->task_tail=NULL;
    pool->worker_head=NULL;
    pool->worker_tail=NULL;

    pool=NULL;
    return 0;
}
static void* ThreadPoolCallBack(void *arg) {
    nWorkerLink worker=(nWorkerLink)arg;
    
    while(1){  //只要没有接到销毁命令 worker就持续工作
        pthread_mutex_lock(&worker->pool->mutex);
        while(worker->pool->task_head == NULL) {
            if(worker->is_exit == 0) {
                break;
            }
            
            //如果当前任务队列没有任务 则该worker等待
            
            pthread_cond_wait(&worker->pool->cond,&worker->pool->mutex);
        }

        //出循环 说明要么要被删了 要么接到任务了

        if(worker->is_exit == 0){
            //是要被删了
            pthread_mutex_unlock(&worker->pool->mutex); //走之前别忘了解锁o
            break;
        }

        //倘若接到了任务 则取出该任务 按照该任务的任务函数完成任务
        nTaskLink task=worker->pool->task_head;
        LIST_REMOVE(task,worker->pool->task_head,worker->pool->task_tail);
        pthread_mutex_unlock(&worker->pool->mutex);

       
        task->task_func(task);

        //完成任务就释放
        free(task->user_data);
	    free(task);
    }

    free(worker);
    return (void*)0;
}
int ThreadPoolTaskPush(PoolLink pool,nTaskLink task) {
        
        pthread_mutex_lock(&pool->mutex);   //上锁  这里上锁在广播 是不是说明pthread_cond_wait里内置了unlock和lock 否则怎么拿到这把锁的
        LIST_INSERT(task,pool->task_head,pool->task_tail);
        pthread_cond_signal(&pool->cond);
        pthread_mutex_unlock(&pool->mutex);
}

#if 1
#define NUM_OF_WORKERS 20       //二十个工人
#define NUM_OF_TASKS 1000       //一千个任务

//主函数试用
void task_try(nTaskLink task){
    int idx=*(int*)task->user_data;
    printf("正在完成第%d号任务\n",idx);
}

int main()
{
    PoolLink pool=(PoolLink)malloc(sizeof(Pool));
    ThreadPoolInit(pool,NUM_OF_WORKERS);

    printf("ThreadPoolInit finish!\n");

    for(int i=0;i<NUM_OF_TASKS;i++){
        nTaskLink task=(nTaskLink)malloc(sizeof(nTask));

        if(!task){
            perror("malloc task:");
            exit(1);
        }

        memset(task,0,sizeof(task));

        task->task_func=task_try;
        
        task->user_data=malloc(sizeof(int));
        *(int*)task->user_data=i;

        ThreadPoolTaskPush(pool,task);
    }


    getchar();//主线程等待其他进程完毕
    ThreadPoolDestroy(pool);
    return 0;
}

#endif