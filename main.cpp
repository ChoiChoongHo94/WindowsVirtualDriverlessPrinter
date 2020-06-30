//#include <dns_sd.h>
//#include <cups/cups.h>
//#include <cups/http.h>
//#include <cups/ipp.h>
//#include <iostream>
//#include <fcntl.h>
//#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include "virtual_driverless_printer.h"
#include "ipp_client.h"
#include "logger.h"
#include "print_job.h"
#include "my_definitions.h"
#include "my_util.h"
#include <sstream>

/* Global */
std::shared_ptr<ConsoleLogger> CONSOLE_LOGGER = nullptr; // TODO: to be singleton?
std::shared_ptr<FileLogger> ERROR_LOGGER = nullptr;
const std::string DEFAULT_DIR = "TmaxVirtualDriverless";
const std::string SPOOL_DIR = "C:\\Temp\\" + DEFAULT_DIR + "\\";

/* externs for initialization */
// 'ipp_client.cpp'
extern void INIT_IPP_ACCESS_LOGGER(const std::string& filename);
// 'print_job.cpp'
extern HANDLE MUTEX_NEXT_JOB_ID;
extern int NEXT_JOB_ID;

/* c statics */
static void init_global_logger();
static void create_spool_dir();
static void create_default_dir();

int main() {
	init_global_logger();
	create_spool_dir();
	create_default_dir();
	INIT_IPP_ACCESS_LOGGER(DEFAULT_DIR + "\\ipp_access.log");
	
	MUTEX_NEXT_JOB_ID = CreateMutex(NULL, FALSE, NULL);
	NEXT_JOB_ID = 0;
	
	VirtualDriverlessPrinter vdprinter(/*"Windows Virtual Driverless Printer",*/ 8631);
	vdprinter.run();
}

static void init_global_logger() {
	CONSOLE_LOGGER = std::make_shared<ConsoleLogger>();
	ERROR_LOGGER = std::make_shared<FileLogger>(Util::get_userhome_dir() + "\\" + DEFAULT_DIR + "\\error.log");
}

static void create_spool_dir() {
	//std::string total_path = std::string("C:\\Temp\\") + DEFAULT_DIR;
	if (CreateDirectoryA(SPOOL_DIR.c_str(), NULL) != 0) {
		if (ERROR_ALREADY_EXISTS == GetLastError()) {
			CONSOLE_LOGGER->writeLog("Spool directory('" + SPOOL_DIR + "') is already exists(not creation).");
			return;
		}
		else {
			std::stringstream log_ss;
			log_ss << std::string(__FUNCTION__) << ": CreateDirectoryA failed! <- " << std::to_string(GetLastError());
			ERROR_LOGGER->writeLog(log_ss.str());
			CONSOLE_LOGGER->writeLog(log_ss.str());
			abort();
		}
	}
	CONSOLE_LOGGER->writeLog("Spool directory('" + SPOOL_DIR + "') is created.");
}

static void create_default_dir() {
	std::string total_path = Util::get_userhome_dir() + "\\" + DEFAULT_DIR;
	if (CreateDirectoryA(total_path.c_str(), NULL) != 0) {
		if (ERROR_ALREADY_EXISTS == GetLastError()) {
			CONSOLE_LOGGER->writeLog("Default directory('" + total_path + "') is already exists(not creation).");
			return;
		}
		else {
			std::stringstream log_ss;
			log_ss << std::string(__FUNCTION__) << ": CreateDirectoryA failed! <- " << std::to_string(GetLastError());
			ERROR_LOGGER->writeLog(log_ss.str());
			CONSOLE_LOGGER->writeLog(log_ss.str());
			abort();
		}
	}
	CONSOLE_LOGGER->writeLog("Default directory('" + total_path + "') is created.");
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