#include <sys/time.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <chrono>
#include <memory>
#include <string>
#include <cstdio>
#include <mutex>
#include <functional> 

#include "thread_pool.h"

std::string LogPrefix()
{
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	time_t unix_tm = ms/1000;

	char buf[64] = {0};
	struct tm t;
	localtime_r(&unix_tm, &t);
	std::snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d.%04d] [%u] ",
			t.tm_year + 1900,
			t.tm_mon + 1,
			t.tm_mday,
			t.tm_hour,
			t.tm_min,
			t.tm_sec,
			ms%1000,
			std::this_thread::get_id());

	return buf;
}

//互斥量，用来打日志，避免日志混乱
std::mutex g_log_mutex;

class Num
{
public:

	Num(int m):max_(m){};

	uint64_t summary()
	{
		uint64_t sum = 0;
		for(uint32_t i = 0; i < max_; i++)
		{
			sum += i;
		}

		return sum;
	}

private:

	const uint32_t max_;
};

void hello()
{
	g_log_mutex.lock();
	std::cout<<LogPrefix()<<"hello, world"<<std::endl;
	g_log_mutex.unlock();
}

void lambda(int n)
{
	g_log_mutex.lock();
	std::cout<<LogPrefix()<<"calling by lambda: para="<<n<<std::endl;
	g_log_mutex.unlock();
}

void delay(int id)
{
	g_log_mutex.lock();
	std::cout<<LogPrefix()<<"delay task, id=["<<id<<"]"<<std::endl;
	g_log_mutex.unlock();
}

int main()
{
	//生成一个线程池，包含4个线程
	auto pool = std::make_shared<ThreadPool>(2);

	Num n(0xFFFFFFF);

	//正常执行的任务
	pool->add_task(hello);
	//延时任务
	pool->delay_task(1000, [](){delay(0);});

	//future任务，等待返回值
	auto return_future = std::move(pool->add_future_task(std::mem_fn(&Num::summary), &n));


	//lambda表达式
	pool->add_task([](){lambda(999);});

	//等待future的执行结果，注意如果函数有异常，会在get()函数中抛出，这是C++11的标准决定的
	g_log_mutex.lock();
	std::cout<<LogPrefix()<<"Waiting for summary future task..."<<std::endl;
	g_log_mutex.unlock();
	uint64_t sum_val = return_future.get();
	g_log_mutex.lock();
	std::cout<<LogPrefix()<<"Summary result: "<<sum_val<<std::endl;
	g_log_mutex.unlock();

	sleep(3);

	//销毁线程池
	g_log_mutex.lock();
	std::cout<<LogPrefix()<<"Destroying thread pool..."<<std::endl;
	g_log_mutex.unlock();

	//等待所有[定时任务以外]的线程完成
	pool->stop();
	return 0;
}

