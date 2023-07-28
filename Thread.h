#ifndef PTHREAD_POOL_H
#define PTHREAD_POOL_H

#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <queue>
#include <vector>
#include <stdexcept>
using namespace std;

class Thread_Pool
{
public:
    Thread_Pool(size_t size);

    ~Thread_Pool();

    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args) -> future<typename std::result_of<F(Args...)>::type>;

private:
    thread t;
    bool stop;
    vector<thread> m_worker;
    queue<function<void()>> m_tasks;
    mutex m_mutex;
    condition_variable m_cond;
};

template <class F, class... Args>
auto Thread_Pool::enqueue(F &&f, Args &&...args) -> future<typename std::result_of<F(Args...)>::type>
{
    using result_type = typename result_of<F(Args...)>::type;

    auto task = make_shared<packaged_task<result_type()>>(bind(std::forward<F>(f), std::forward<Args>(args)...));

    future<result_type> res = task->get_future();

    {
        unique_lock<mutex> lock(m_mutex);
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");
        m_tasks.emplace(
            [task]()
            {
                (*task)();
            });
    }
    m_cond.notify_one();
    return res;
}
Thread_Pool::Thread_Pool(size_t size) : stop(false)
{
    for (int i = 0; i < size; i++)
    {
        m_worker.emplace_back(
            [this]()
            {
                std::function<void()> task;
                while (true)
                {
                    {
                        unique_lock<mutex> lock(m_mutex);
                        m_cond.wait(lock, [this]
                                    { return stop || !m_tasks.empty(); });
                        if (stop && m_tasks.empty())
                            return;
                        task = std::move(this->m_tasks.front());
                        m_tasks.pop();
                    }
                    task();
                }
            });
    }
}

Thread_Pool::~Thread_Pool()
{
    {
        std::unique_lock<mutex> lock(m_mutex);
        stop = true;
    }
    m_cond.notify_all();
    for (std::thread &worker : m_worker)
        worker.join();
}

#endif