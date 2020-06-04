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

//PrintJob
HANDLE MUTEX_JOB_ID;
int NEXT_JOB_ID;

int main() {
	MUTEX_JOB_ID = CreateMutex(NULL, FALSE, NULL);
	NEXT_JOB_ID = 0;
	VirtualDriverlessPrinter vdprinter("Virtual IPP Everywhere Printer", 8631);
	vdprinter.run();
}

/*
static bool register_printer_to_bonjour(const VirtualDriverlessPrinter& vdprinter) {

}
*/