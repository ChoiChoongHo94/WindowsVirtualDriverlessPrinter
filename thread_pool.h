#pragma once
#include <windows.h>
#include <functional>

// refered to 
// 1) "https://dorodnic.com/blog/2015/10/17/windows-threadpool/"
// 2) "https://docs.microsoft.com/ko-kr/windows/win32/procthread/using-the-thread-pool-functions?redirectedfrom=MSDN"

class ThreadPool {
public:
	ThreadPool();
	ThreadPool(int min_threads, int max_threads);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	ThreadPool(ThreadPool&& rhs) = delete;
	ThreadPool& operator=(ThreadPool&& rhs) = delete;

	~ThreadPool();

	bool submitWork(std::function<void()> thread_pool_work);
	
private:
	PTP_POOL thread_pool_; // actual thread pool resource
	_TP_CALLBACK_ENVIRON_V3 environment_; // for connecting 'work object' to my thread pool
	PTP_CLEANUP_GROUP cleanup_group_; // for cleaning things up neatly when done
};