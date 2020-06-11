#include "thread_pool.h"
#include <iostream>

static void CALLBACK call_back(PTP_CALLBACK_INSTANCE, PVOID, PTP_WORK);

ThreadPool::ThreadPool() {
	thread_pool_ = CreateThreadpool(nullptr);
	if (thread_pool_ == nullptr) {
		std::cerr << "Thread pool creation is failed! <- " << GetLastError() << '\n';
		abort();
	}

	InitializeThreadpoolEnvironment(&environment_);
	SetThreadpoolCallbackPool(&environment_, thread_pool_);
	SetThreadpoolCallbackLibrary(&environment_, GetModuleHandle(nullptr));

	cleanup_group_ = CreateThreadpoolCleanupGroup();
	if (cleanup_group_ == nullptr) {
		DestroyThreadpoolEnvironment(&environment_);
		CloseThreadpool(thread_pool_);
		std::cerr << "Thread pool clean-up group creation is failed! <- " << GetLastError() << '\n';
		abort();
	}
	SetThreadpoolCallbackCleanupGroup(&environment_, cleanup_group_, nullptr);
}

ThreadPool::ThreadPool(int min_threads, int max_threads) : ThreadPool() {
	SetThreadpoolThreadMaximum(thread_pool_, max_threads);
	SetThreadpoolThreadMinimum(thread_pool_, min_threads);
}

ThreadPool::~ThreadPool() {
	CloseThreadpoolCleanupGroupMembers(cleanup_group_, false, nullptr);
	CloseThreadpoolCleanupGroup(cleanup_group_);
	DestroyThreadpoolEnvironment(&environment_);
	CloseThreadpool(thread_pool_);
}

bool ThreadPool::submitWork(std::function<void()> thread_pool_work) {
	auto context = new std::function<void()>(thread_pool_work);
	auto work = CreateThreadpoolWork(call_back, context, &environment_);
	if (work == nullptr) {
		return false;
	}

	SubmitThreadpoolWork(work);
	return true;
}

static void CALLBACK call_back(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work) {
	auto thread_pool_work = static_cast<std::function<void()>*>(context);
	(*thread_pool_work)();
	CloseThreadpoolWork(work);
	// TODO: logging
}