#ifndef  __THREAD_POOL_H__
#define  __THREAD_POOL_H__

#include <vector>
#include <chrono>
#include <deque>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <memory>
#include <condition_variable>
#include <functional>

class ThreadPool 
{
public:

    typedef std::function<void()> Task;

public:
    
    ThreadPool(int);
    ~ThreadPool();

    void stop();

    bool is_stopped();

    //获取队列中的任务数量(定时任务除外)
    int pending() const;

    //添加任务
    template<class F, class... Args>
    void add_task(F&& f, Args&&... args);

    //批量添加任务
    //无法添加则返回false
    template<class TaskContainer>
    bool add_batch_task(TaskContainer&& task_list);

    //添加任务，调用方需要等待完成
    template<class F, class... Args>
    auto add_future_task(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

    //添加优先执行任务
    template<class F, class... Args>
    void add_priority_task(F&& f, Args&&... args);

    //添加延时任务
    template<class F, class... Args>
    void delay_task(int64_t delay_ms, F&& f, Args&&... args);

    //批量添加延迟任务
    //无法添加则返回false
    template<class TaskContainer>
    bool delay_batch_task(int64_t delay_ms, TaskContainer&& task_list);

private:

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    //当前时刻的毫秒数
    static int64_t Milliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

private:

    //表示一个延时任务
    class TimeTask
    {
    public:
        TimeTask(int64_t exec_tm, Task&& task)
            :exec_tm_(exec_tm), 
            task_(std::move(task))
        {
            //NULL
        }

        TimeTask(const TimeTask& t)
            :exec_tm_(t.exec_tm_), 
            task_(t.task_)
        {
            //NULL
        }

        TimeTask(TimeTask&& t)
            :exec_tm_(t.exec_tm_), 
            task_(std::move(t.task_))
        {
            //NULL
        }

        TimeTask& operator=(TimeTask&& t)
        {
            exec_tm_ = t.exec_tm_;
            task_ = std::move(t.task_);
            return *this;
        }

        bool operator<(const TimeTask& item) const 
        {
            return exec_tm_ > item.exec_tm_;
        }

    public:

        int64_t exec_tm_;
        Task task_;
    };

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;

    //线程
    std::vector<std::thread> workers_;

    //任务队列
    std::deque<Task> tasks_;

    //延时执行队列
    std::priority_queue<TimeTask> delay_tasks_;
    
    //退出标志
    std::atomic_bool stop_{false};
};

//----------------------------------------------------------------------

inline ThreadPool::ThreadPool(int threads)
{
    //初始化线程回调函数
    for(int i = 0;i<threads;++i)
    {
        workers_.emplace_back(
            [this]
            {
                for(;;)
                {
                    Task task;

                    std::unique_lock<std::mutex> _lock(queue_mutex_);
                    queue_condition_.wait(_lock,
                        [this]{ return stop_.load() || !tasks_.empty() || !delay_tasks_.empty();});

                    if(stop_.load() && tasks_.empty()) return;
                    
                    //执行延时任务
                    if(!delay_tasks_.empty())
                    {
                        auto now_time = ThreadPool::Milliseconds();
                        auto wait_tm = delay_tasks_.top().exec_tm_ - now_time;
                        if(wait_tm <= 0)
                        {
                            task = delay_tasks_.top().task_;
                            delay_tasks_.pop();

                            //执行任务
                            _lock.unlock();
                            if(task) task();
                            _lock.lock();
                        }
                        else if(tasks_.empty() && !stop_.load())
                        {
                            queue_condition_.wait_for(_lock, std::chrono::milliseconds(wait_tm));
                        }
                    }
                    
                    //执行一般任务
                    if(!tasks_.empty())
                    {
                        task = std::move(tasks_.front());
                        tasks_.pop_front();

                        //执行任务
                        _lock.unlock();
                        if(task) task();
                        _lock.lock();
                    }
                }//for
            }//lambda
        );
    }

    return;
}

//停止线程
inline ThreadPool::~ThreadPool()
{
    this->stop();
}

//停止并等待线程结束
inline void ThreadPool::stop()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_.store(true);
        queue_condition_.notify_all();
    }

    for(std::thread &worker : workers_)
    {
        if(worker.joinable())
            worker.join();
    }

    return;
}

inline bool ThreadPool::is_stopped()
{
    return stop_.load();
}

//添加任务
template<class F, class... Args>
void ThreadPool::add_task(F&& f, Args&&... args)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    if(stop_.load())
    {
        lock.unlock();
        f(args...);
    }
    else
    {
        if(sizeof...(args) != 0)
            tasks_.emplace_back(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        else
            tasks_.emplace_back(std::move(f));

        queue_condition_.notify_one();
    }

    return;
}

//批量添加任务
//无法添加则返回false
template<class TaskContainer>
bool ThreadPool::add_batch_task(TaskContainer&& task_list)
{
    if(task_list.empty()) return false;

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if(stop_.load()) return false;

    for(auto& task : task_list)
    {
        tasks_.emplace_back(std::move(task));
    }
    task_list.clear();

    queue_condition_.notify_all();
    return true;
}

//添加任务，调用方需要等待完成
template<class F, class... Args>
auto ThreadPool::add_future_task(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared< std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...) );
    std::future<return_type> res = task->get_future();

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if(stop_.load())
    {
        lock.unlock();
        (*task)();
    }
    else
    {
        tasks_.emplace_back([task](){ (*task)();});
        queue_condition_.notify_one();
    }
    return res;
}

//添加优先任务
template<class F, class... Args>
void ThreadPool::add_priority_task(F&& f, Args&&... args)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    if(stop_.load())
    {
        lock.unlock();
        f(args...);
    }
    else
    {
        if(sizeof...(args) != 0)
            tasks_.emplace_front(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        else
            tasks_.emplace_front(std::move(f));

        queue_condition_.notify_one();
    }
    
    return;
}

//添加延时任务
template<class F, class... Args>
void ThreadPool::delay_task(int64_t delay_ms, F&& f, Args&&... args)
{
    auto exec_tm = Milliseconds() + delay_ms;

    std::unique_lock<std::mutex> lock(queue_mutex_);

    if(sizeof...(args) != 0)
    {
        delay_tasks_.emplace(ThreadPool::TimeTask(exec_tm, 
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)));
    }
    else
    {
        delay_tasks_.emplace(ThreadPool::TimeTask(exec_tm, std::move(f)));
    }
    queue_condition_.notify_one();
    
    return;
}

//批量添加延迟任务
//无法添加则返回false
template<class TaskContainer>
bool ThreadPool::delay_batch_task(int64_t delay_ms, TaskContainer&& task_list)
{
    if(task_list.empty()) return false;

    auto exec_tm = Milliseconds() + delay_ms;
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if(stop_.load()) return false;

    for(auto& task : task_list)
    {
        delay_tasks_.emplace(ThreadPool::TimeTask(exec_tm, std::move(task)));
    }
    queue_condition_.notify_all();

    return true;
}

//获取队列中的任务数量(定时任务除外)
inline int ThreadPool::pending() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

#endif
