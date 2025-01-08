//等权时间片轮转算法
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
int mutex = 0;//用于互斥锁,1为锁定
int idGenerate = 1;//自增ID
int nowTaskIndex = -1;//总是作为当前正在执行的进程的索引
typedef struct _process_info {
        int id;//进程ID
        jmp_buf context;
        void (*func)(void);
        int running;
        int priority;
        int t;
} ProcessInfo;

ProcessInfo processes[100] = {0};//进程列表简易实现
void timer(int wait_us);//定时向父进程发送SIGINT中断
void create_process(void (*process)(void),int pr);//创建一个进程
void switch_task(void);//尝试切换进程
void do_function_in_new_stack(void (*process)(void),void* stack,uint32_t stack_size);//此函数会切换栈，因此不会返回，需 要使用longjmp跳回
void handle_sigalrm(int sig);
void start_timer();
void hlt(void);//停止CPU直到下一次中断发生
void pexit();//终止进程自己
int select_task();//从进程列表里选择一个进程,返回索引

void pexit()
{
        //简单模拟，不释放资源，仅将自身从进程列表中清除
        processes[nowTaskIndex].id = 0;
        processes[nowTaskIndex].t = 0;//可用时间片清除
        hlt();
}

void hlt(void)
{
        sleep(10000000);//用sleep模拟hlt
}

void start_timer()
{
        if (fork())
        {
                return;
        }
        else timer(300000);
}

void handle_sigalrm(int sig)
{
        //立即取消对该信号的掩码
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set,SIGALRM);
        sigprocmask(SIG_UNBLOCK,&set,0);

        //      printf("SIGINT\n");
        switch_task();
        //      printf("SIGINT END\n");
}


void timer(int wait_us)
{
        int ppid = getppid();
        while (1)
        {
                usleep(wait_us);
                kill(ppid,SIGALRM);
        }
}
void create_process(void (*process)(void),int pr)
{
        //暂时阻止进程切换
        mutex = 1;
        for (int i=0;i<100;++i)
        {
                if (processes[i].id == 0)
                {
                        //初始化该进程块
                        ProcessInfo pcb = {0};
                        processes[i] = pcb;
                        processes[i].id = idGenerate++;
                        processes[i].func = process;
                        processes[i].t = processes[i].priority = pr;
                        printf("新进程已创建，ID=%d，优先级=%d\n",processes[i].id,processes[i].priority);
                        break;
                }
        }
        //恢复进程切换
        mutex = 0;
}

int select_task()
{
        int i = nowTaskIndex+1;
        while (i!=nowTaskIndex)
        {
                if (i>=100)
                {
                        if (nowTaskIndex < 0) return 0;
                        i = 0;
                        continue;
                }
                if (processes[i].id) return i;
                ++i;
        }
        return -1;
}
void switch_task(void)
{
        //消耗进程时间片
        if (nowTaskIndex !=-1 && processes[nowTaskIndex].t>0)
        {
                processes[nowTaskIndex].t -= 1;
                return;
        }
        int new_task_id = select_task();
        if (new_task_id != -1)
        {
                //该进程有效
                processes[nowTaskIndex].t = processes[nowTaskIndex].priority;//重置该任务时间片
                //保存当前进程上下文
                if (nowTaskIndex==-1?0:setjmp(processes[nowTaskIndex].context))
                {
                        //由其他进程跳转过来，直接返回
                        return;
                }
                else
                {
                        //要跳转到其他进程
                        //检查目标进程状态
                        //printf("jmp to %d\n",processes[i].id);
                        if (processes[new_task_id].running == 0)
                        {
                                //目标进程处于就绪状态，需要创建进程上下文并运行
                                processes[new_task_id].running = 1;
                                nowTaskIndex = new_task_id;
                                do_function_in_new_stack(processes[new_task_id].func,malloc(1024*1024),1024*1024);
                        }
                        else
                        {
                                //目标进程正在运行，直接跳转
                                nowTaskIndex = new_task_id;
                                longjmp(processes[new_task_id].context,1);
                        }
                }
        }
        //未找到可用进程，返回执行当前进程
        if (nowTaskIndex<0||processes[nowTaskIndex].id == 0)
        {
                //没有任何可用进程
                printf("无可用进程,HLT IF=0\n");
                signal(SIGALRM,SIG_IGN);
                hlt();
        }
        return;
}

void do_function_in_new_stack(void (*process)(void),void* stack,uint32_t stack_size)
{
        stack = (char*)stack + stack_size;//栈向下增长
                                                                          //切换栈并转去执行
        __asm__ volatile (
                        "mov %%rbx,%%rsp;\n\t"
                        "mov %%rbx,%%rbp;\n\t"
                        "call *%%rax;\n\t"
                        :
                        :"a"(process),"b"(stack)
                        );//RAX存放新函数地址，RBX存放新的栈地址
}

void pro(void)//模拟三个进程
{
        int i=0;
        while (1)
        {
                i+=1;
                usleep(100000);
                printf("P1: %d\n",i);
                if (i>15) printf("P1 Exit\n"),pexit();
        }
}
void pro2(void)//模拟三个进程
{
        int i=0;
        while (1)
        {
                ++i;
                printf("P2: Hello\n");
                usleep(100000);
                if (i>15) printf("P2 Exit\n"),pexit();
        }
}
void pro3(void)//模拟三个进程
{
        int i=0;
        while (1)
        {
                printf("P3: %d\n",--i);
                usleep(100000);
                if (i<-10)
                {
                        printf("P3 Exit\n");
                        pexit();
                }
        }
}
int main()
{
        //创建三个进程
        create_process(pro,1);
        create_process(pro2,0);
        create_process(pro3,1);
        //注册中断处理函数
        signal(SIGALRM,handle_sigalrm);
        //启动时间中断
        start_timer();
        hlt();
}
