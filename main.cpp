//#include <dns_sd.h>
//#include <cups/cups.h>
//#include <cups/http.h>
//#include <cups/ipp.h>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include "logger.h" // 'Logger' project in this solution
#include "print_job.h"
#include "virtual_driverless_printer.h"
#include "ipp_client.h"
#include "my_definitions.h"
#include "my_util.h"

/* Global */
std::shared_ptr<ConsoleLogger> CONSOLE_LOGGER = nullptr; // TODO: to be singleton?
std::shared_ptr<FileLogger> ERROR_LOGGER = nullptr;

/* externs for initialization */
// 'ipp_client.cpp'
extern void INIT_IPP_ACCESS_LOGGER(const std::string& filename);
// 'print_job.cpp'
extern HANDLE MUTEX_NEXT_JOB_ID;
extern int NEXT_JOB_ID;

/* c statics */
static constexpr char DEFAULT_DIR_NAME[] = "TmaxVirtualDriverless";
static void create_default_dir();
static void init_global_logger();

int main() {
	init_global_logger();
	create_default_dir();
	INIT_IPP_ACCESS_LOGGER(std::string(DEFAULT_DIR_NAME) + "\\ipp_access.log");
	
	MUTEX_NEXT_JOB_ID = CreateMutex(NULL, FALSE, NULL);
	NEXT_JOB_ID = 0;
	
	VirtualDriverlessPrinter vdprinter(/*"Windows Virtual Driverless Printer",*/ 8631);
	vdprinter.run();
}

static void create_default_dir() {
	std::string total_path = Util::get_userhome_dir() + "\\" + DEFAULT_DIR_NAME;
	if (CreateDirectoryA(total_path.c_str(), NULL) != 0) {
		if (ERROR_ALREADY_EXISTS == GetLastError()) {
			CONSOLE_LOGGER->writeLog("Default directory('" + total_path + "') is already exists(not creation).");
		}
		else {
			CONSOLE_LOGGER->writeLog("CreateDirectoryA failed! <- " + std::to_string(GetLastError()));
			abort();
		}
	}
	CONSOLE_LOGGER->writeLog("Default directory('" + total_path + "') is created.");
}

static void init_global_logger() {
	CONSOLE_LOGGER = std::make_shared<ConsoleLogger>();
	ERROR_LOGGER = std::make_shared<FileLogger>(Util::get_userhome_dir() + "\\" + DEFAULT_DIR_NAME + "\\error.log");
}

//static void thread_pool_test() {
//	auto func1 = []() {
//		std::cout << "func1 Thread ID: " << GetCurrentThreadId() << '\n';
//	};
//	auto func2 = []() {
//		std::cout << "func2 Thread ID: " << GetCurrentThreadId() << '\n';
//	};
//	auto func3 = []() {
//		std::cout << "func3 Thread ID: " << GetCurrentThreadId() << '\n';
//	};
//	ThreadPool thread_pool(1, 5);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func2);
//	thread_pool.submitWork(func3);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func2);
//	thread_pool.submitWork(func3);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func1);
//	thread_pool.submitWork(func2);
//	thread_pool.submitWork(func3);
//}