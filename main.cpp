#include <dns_sd.h>
#include <cups/cups.h>
#include <cups/http.h>
#include <cups/ipp.h>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include "virtual_driverless_printer.h"
#include "my_definitions.h"

//static const int orientation_supported[] = {
//	IPP_ORIENT_PORTRAIT
//};
//
//static const std::string media_supported[] = {
//	"iso_a4_210x297mm"
//};
//
//static const std::string ipp_version = "2.0";
//
//static const std::string device_id = "MFG:TmaxOS;MDL:WindowVirtualPrinter;CMD:PDF";


CRITICAL_SECTION JOB_ID_CS;
int NEXT_JOB_ID = 0;

int main() {
	InitializeCriticalSection(&JOB_ID_CS);

	VirtualDriverlessPrinter vdprinter("Virtual IPP Everywhere Printer", 8631);
	vdprinter.run();

	DeleteCriticalSection(&JOB_ID_CS);
}

/*
static bool register_printer_to_bonjour(const VirtualDriverlessPrinter& vdprinter) {

}
*/