//#include <dns_sd.h>
#include <cups/cups.h>
#include <cups/http.h>
#include <cups/ipp.h>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include "print_job.h"
#include "virtual_driverless_printer.h"
#include "my_definitions.h"
#include "thread_pool.h"

//PrintJob
HANDLE MUTEX_JOB_ID;
int NEXT_JOB_ID;

int main() {
	MUTEX_JOB_ID = CreateMutex(NULL, FALSE, NULL);
	NEXT_JOB_ID = 0;
	VirtualDriverlessPrinter vdprinter("Virtual IPP Everywhere Printer", 8631);
	vdprinter.run();

	/* ThreadPool test */
	//auto func1 = []() { std::cout << GetCurrentThreadId() << ": func1" << '\n';
	//Sleep(1000); std::cout << GetCurrentThreadId() << ": func1 end" << '\n'; };
	//auto func2 = []() { std::cout << GetCurrentThreadId() << ": func2" << '\n';
	//Sleep(1000); std::cout << GetCurrentThreadId() << ": func2 end" << '\n'; };
	//auto func3 = []() { std::cout << GetCurrentThreadId() << ": func3" << '\n';
	//Sleep(1000); std::cout << GetCurrentThreadId() << ": func3 end" << '\n'; };
	//ThreadPool thread_pool(1, 2);
	//thread_pool.submitWork(func1);
	//thread_pool.submitWork(func2);
	//thread_pool.submitWork(func3);
}