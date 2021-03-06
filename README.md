# ThreadPool：一个基于C++11的功能完备的线程池
==========
## 主要功能
> * add_task: 添加常规任务，不需要等待返回；
> * add_batch_task：添加批量常规任务，类似于事务。批量添加的用途是，避免在添加的同时，部分任务已经开始执行；
> * add_future_task：添加任务，函数返回一个future，让调用方是当地等待函数返回的处理结果，注意，如果函数有异常抛出，会在get()时抛出异常，调用方需要了解并适当catch异常，这是c++标准规定的；
> * add_priority_task：优先执行任务，这个任务不需要排队，直接放入待执行队列的最前端，线程池有闲暇马上优先执行；
> * delay_task：添加延时任务，毫秒级，通常用于某些定时任务；
> * delay_batch_task：添加批量延时任务，这个在我的项目中多数用于[任务池]的开发，关于任务池，稍后会开源；
> * pending：返回正在排队执行的任务（延时任务除外），可以理解为线程池的压力

## 还需要完善的地方
> * 动态调整线程的数量，比如在运行时改变线程的大小，根据线程的压力，动态加开线程，但是不超过指定的最大值；如果线程池压力不大，则动态地关闭一些线程以节省资源。
> * 这个功能一直比较少用，所以一直没有加上。我在项目里一般都是为线程池开足够的线程。当时考虑的问题是，如果有突发的大量请求，可能会导致线程池瞬间创建大量线程，有可能带来不稳定。

## 对网友问题的回应
> * 【有网友提出，在add_priority_task时，如果线程池被关闭，则在调用方当前线程直接执行函数，这样不妥】针对这个问题，特此回应：首先，程序有退出机制，调用方(线程的使用者)一般会在观察到某个退出标志位后，退出程序，那么他们很可能等待已经添加到线程池的任务去回调(如修改状态等)。如果线程池已经退出，还有调用方在添加任务，那么已经无线程可以干活了，那么调用方就要主动干活（没有洗碗工，厨师需要自己动手洗碗），因为，调用方可能需要在退出前调用这个函数来修改某些状态才可以安然退出。如果线程池退出后，直接把任务丢进队列但未被执行，那么在业务逻辑上很可能会被死锁。另外，我考虑过try-catch方式，在线程池退出而且另一个线程添加任务时抛异常，这样会导致性能损失。

## 例子

```c++

//测试对象回调
class Num
{
public:

        Num(uint32_t m):max_(m){};

        //模拟某种复杂计算
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

//使用一般方式回调
void hello()
{
        do_something_1();
}

//使用lambda方式回调
void lambda(int n)
{
        do_something_2();
}

//延时任务
void delay(int id)
{
        do_something_3();
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
        std::cout<<LogPrefix()<<"Waiting for summary future task..."<<std::endl;
        uint64_t sum_val = return_future.get();
        std::cout<<LogPrefix()<<"Summary result: "<<sum_val<<std::endl;

        sleep(3);

        //销毁线程池
        std::cout<<LogPrefix()<<"Destroying thread pool..."<<std::endl;

        //等待所有[定时任务以外]的线程完成
        pool->stop();
        return 0;
}
```

## 运行方式

```shell
 g++ -o example example.cc -std=c++11 -pthread && ./example
```

## 输出结果
```
[2018-12-21 18:27:30.0534] [1959212800] hello, world
[2018-12-21 18:27:30.0534] [1959221024] Waiting for summary future task...
[2018-12-21 18:27:30.0534] [1959212800] calling by lambda: para=999
[2018-12-21 18:27:31.0355] [1959221024] Summary result: 36028796616310785
[2018-12-21 18:27:31.0534] [1959212800] delay task, id=[0]
[2018-12-21 18:27:34.0355] [1959221024] Destroying thread pool...
```
