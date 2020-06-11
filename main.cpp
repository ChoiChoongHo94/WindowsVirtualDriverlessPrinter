//#include <dns_sd.h>
#include <cups/cups.h>
#include <cups/http.h>
#include <cups/ipp.h>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include "logger.h"
#include "print_job.h"
#include "virtual_driverless_printer.h"
#include "ipp_client.h"
#include "my_definitions.h"

static void thread_pool_test();

extern void INIT_ACCESS_LOGGER(const std::string& filename); // in 'ipp_client.cpp'

//used in PrintJob
HANDLE MUTEX_JOB_ID;
int NEXT_JOB_ID;

int main() {
	ConsoleLogger console_logger;
	console_logger.writef("%s %d.", "ipp_access_testlog: ", 100);
	
	//MUTEX_JOB_ID = CreateMutex(NULL, FALSE, NULL);
	//NEXT_JOB_ID = 0;
	//
	//IPPClient::s_initAccessLogger("ipp_access.txt");
	//
	//VirtualDriverlessPrinter vdprinter("Virtual IPP Everywhere Printer", 8631);
	//vdprinter.run();
}

static void thread_pool_test() {
	auto func1 = []() {
		std::cout << "func1 Thread ID: " << GetCurrentThreadId() << '\n';
	};
	auto func2 = []() {
		std::cout << "func2 Thread ID: " << GetCurrentThreadId() << '\n';
	};
	auto func3 = []() {
		std::cout << "func3 Thread ID: " << GetCurrentThreadId() << '\n';
	};
	ThreadPool thread_pool(1, 5);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func2);
	thread_pool.submitWork(func3);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func2);
	thread_pool.submitWork(func3);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func1);
	thread_pool.submitWork(func2);
	thread_pool.submitWork(func3);
}