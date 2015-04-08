#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

// Threadsafe FIFO class.

template <class T>
class Queue
{
public:
    bool isEmpty()
    {
        return _Queue.empty();
    }

    T pop()
    {
        std::unique_lock<std::mutex> mlock(_Mutex);
        while (_Queue.empty())
        {
            _Cond.wait(mlock);
        }
        auto item = _Queue.front();
        _Queue.pop();
        return item;
    }

    void push(const T& item)
    {
        std::unique_lock<std::mutex> mlock(_Mutex);
        _Queue.push(item);
        mlock.unlock();
        _Cond.notify_one();
    }


private:
    std::queue<T>           _Queue;
    std::mutex              _Mutex;
    std::condition_variable _Cond;
};
