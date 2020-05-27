#include "virtual_driverless_printer.h"
#include "ipp_client.h"
#include "my_util.h"
#include <dns_sd.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <WinSock2.h>
#include <Windows.h>
#include <iostream>
#include <sstream>
#include <fpdfview.h>


static unsigned WINAPI ProcessIPPThread(LPVOID ipp_client);

VirtualDriverlessPrinter::VirtualDriverlessPrinter(const std::string& name, const int port)
	: name_(name), port_(port) {
	// hostname
	WSADATA wsa_data;
	int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
	char hostname[256];
	std::cerr << "gethostname() return: " << gethostname(hostname, sizeof(hostname)) << '\n';
	hostname_ = hostname;

	// uuid
	char assemble_uuid_buf[46];
	uuid_ = httpAssembleUUID(hostname_.c_str(), port_, name_.c_str(), 0, assemble_uuid_buf, sizeof(assemble_uuid_buf));

	// url
	char uri[1024];
	httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL, hostname_.c_str(), port_, "/ipp/print");
	uri_ = uri;

	//adminurl
	char adminurl[1024];
	httpAssembleURI(HTTP_URI_CODING_ALL, adminurl, sizeof(adminurl), "http", NULL, hostname_.c_str(), port_, "/");
	adminurl_ = adminurl;

	HANDLE hprinter;
	DWORD cch_printer(ARRAYSIZE(windows_printer_name_));
	GetDefaultPrinter(windows_printer_name_, &cch_printer);
	if (!OpenPrinter(windows_printer_name_, &hprinter, NULL)) {
		std::cerr << "OpenPrinter failed <- " << GetLastError();
		assert(1);
		return;
	}

	// TODO: output-bin, media
	DWORD collate, copies, duplex, color;
	collate = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_COLLATE, NULL, NULL);
	copies = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_COPIES, NULL, NULL);
	duplex = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_DUPLEX, NULL, NULL);
	color = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_COLORDEVICE, NULL, NULL);

	if (collate > 0) {
		const char* multiple_document_handling[] = {
			/*
			당장은 multiple-document를 포함한 기능보다는 collate의 기능만 담당.
			이 가정에서, "single-document"와 "separate-documents-collated-copies"의 기능(=한 부씩 인쇄)은 같음.
			아래의 예시는 'a, b는 서로 다른 document', 'copies=2'를 가정함.
			*/
			"single-document", // {a(1), a(2), ..., b(1), b(2), ...}, {a(1), a(2), ..., b(1), b(2)}
			"separate-documents-uncollated-copies", // {a(1), a(1), a(2), a(2), ...}, {b(1), b(1), ..., b(2), b(2), ...}
			"separate-documents-collated-copies" }; // {a(1), a(2), ..., a(1), a(2), ...}, {b(1), b(2), ..., b(1), b(2), ...}
		ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-document-handling-default", NULL, multiple_document_handling[0]);
		ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-document-handling-supported", sizeof(multiple_document_handling) / sizeof(multiple_document_handling[0]), NULL, multiple_document_handling);
		printer_type_ &= CUPS_PRINTER_COLLATE;
	}
	if (copies > 0) {
		ippAddInteger(attrs_, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies-default", 1);
		ippAddRange(attrs_, IPP_TAG_PRINTER, "copies-supported", 1, 99); // FIXME possibility
		printer_type_ &= CUPS_PRINTER_COPIES;
	}
	if (duplex > 0) {
		const char* sides_supported[] = { "one-sided", "two-sided-long-edge", "two-sided-short-edge"};
		ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-default", NULL, sides_supported[0]);
		ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-supported", sizeof(sides_supported) / sizeof(sides_supported[0]), NULL, sides_supported);
		printer_type_ &= CUPS_PRINTER_DUPLEX;
	}
	if (color > 0) {
		const char* print_color_mode_supported[] = {"color", "monochrome" };
		ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-default", NULL, print_color_mode_supported[0]);
		ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-supported", sizeof(print_color_mode_supported) / sizeof(print_color_mode_supported[0]), NULL, print_color_mode_supported);
		ippAddBoolean(attrs_, IPP_TAG_PRINTER, "color-supported", true);
		printer_type_ &= CUPS_PRINTER_COLOR;
	}

	/*
	 * common attrs
	 */
	const char* ipp_features[] = { "ipp-everywhere" };
	const char* ipp_versions[] = { "1.1", "2.0" };
	const char* media_supported[] = { CUPS_MEDIA_A4 };
	const char* document_format_supported[] = { CUPS_FORMAT_PDF };
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-default", NULL, document_format_supported[0]);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", sizeof(document_format_supported) / sizeof(document_format_supported[0]), NULL, document_format_supported);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(ipp_features) / sizeof(ipp_features[0]), NULL, ipp_features);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", sizeof(ipp_versions) / sizeof(ipp_versions[0]), NULL, ipp_versions);
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-default", NULL, media_supported[0]);
	ippAddResolution(attrs_, IPP_TAG_PRINTER, "printer-resolution-default", IPP_RES_PER_INCH, 600, 600);
	ippAddResolution(attrs_, IPP_TAG_PRINTER, "printer-resolution-supported", IPP_RES_PER_INCH, 600, 600);

	// info
	ippAddBoolean(attrs_, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", NULL, "TEST_printer-info");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", NULL, "TEST_printer-location");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", NULL, "Tmax VirtualDriverlessPrinter");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", NULL, name_.c_str());
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uuid", NULL, uuid_.c_str());

	start_time_ = time(NULL);
	
	FPDF_InitLibrary();

	std::cerr << "VirtualDriverlessPrinter object is created." << '\n';
}

VirtualDriverlessPrinter::~VirtualDriverlessPrinter() {
	WSACleanup();
	FPDF_DestroyLibrary();
}

void VirtualDriverlessPrinter::run() {
	int timeout = 0;

	// socket
	http_addrlist_t* addrlist = httpAddrGetList(NULL, AF_INET, std::to_string(port_).c_str());
	ipv4_ = httpAddrListen(&(addrlist->addr), port_);
	httpAddrFreeList(addrlist);

	nfds_t num_fds = 1;
	struct pollfd polldata[1];
	polldata[0].fd = ipv4_;
	polldata[0].events = POLLIN;

	/*
	polldata[1].fd = ipv6_;
	polldata[1].events = POLLIN;
	polldata[2].fd = DNSServiceRefSockFD(bonjour_service_);
	polldata[2].events = POLLIN;
	*/


	unsigned thread_id = -1;
	//HANDLE threads[MAX_THREADS];
	std::cerr << "The printer \"" << name_ << "\" is start to run!" << '\n';
	for (;;) {
		if (poll(polldata, (nfds_t)num_fds, timeout) < 0 && errno != EINTR) {
			std::cerr << "WSAPoll failed <- " << WSAGetLastError() << '\n';
			break;
		}

		if (polldata[0].revents & POLLIN) {
			std::shared_ptr<IPPClient> ipp_client = std::make_shared<IPPClient>(this, ipv4_);
			ipp_client->process();
			/* TODO: multi threading
			if (ipp_client != nullptr) {
				HANDLE h_thread = (HANDLE)_beginthreadex(NULL, 0, ProcessIPPThread, (LPVOID)ipp_client.get(), 0, &thread_id);
				std::cerr << "Created thread_id: " << thread_id << '\n';
			}
			*/
		}

		/*
		if (polldata[1].revents & POLLIN) {

		}

		if (polldata[2].revents & POLLIN) {
		}
		*/
	}
	std::cerr << "The printer \"" << name_ << "\" is end to run!" << '\n';
}

bool VirtualDriverlessPrinter::printFile(const std::shared_ptr<PrintJob>& job) {
	std::cerr << "[" << __FUNCTION__ << "] Enter" << '\n';
	FPDF_DOCUMENT pdf_doc = FPDF_LoadDocument(job->getFilepath().c_str(), NULL);
	if (!pdf_doc) {
		std::cerr << "FPDF_LoadDocument failed! <- ";
		auto err = FPDF_GetLastError();
		switch (err) {
			case FPDF_ERR_SUCCESS:
				std::cerr << "Success";
				break;
			case FPDF_ERR_UNKNOWN:
				std::cerr << "Unknown error";
				break;
			case FPDF_ERR_FILE:
				std::cerr << "File not found or could not be opened";
				break;
			case FPDF_ERR_FORMAT:
				std::cerr << "File not in PDF format or corrupted";
				break;
			case FPDF_ERR_PASSWORD:
				std::cerr << "Password required or incorrect password";
				break;
			case FPDF_ERR_SECURITY:
				std::cerr << "Unsupported security scheme";
				break;
			case FPDF_ERR_PAGE:
				std::cerr << "Page not found or content error";
				break;
			default:
				std::cerr << "Unknown error: " << err << '\n';
				break;
		}
		return false;
	}

	HANDLE hprinter;
	if (!OpenPrinter(windows_printer_name_, &hprinter, NULL)) {
		std::cerr << "OpenPrinter failed! <- " << GetLastError() << '\n';
		return false;
	}

	DWORD dw_needed;
	GetPrinter(hprinter, 2, 0, 0, &dw_needed);
	PRINTER_INFO_2* pi2 = (PRINTER_INFO_2*)malloc(dw_needed);
	GetPrinter(hprinter, 2, (LPBYTE)pi2, dw_needed, &dw_needed);
	LPDEVMODE pdevmode = pi2->pDevMode;

	/*
	output-bin
	media
	*/
	ipp_t* job_attrs = job->getAttributes();
	ipp_attribute_t* attr = nullptr;
	if ((printer_type_ & CUPS_PRINTER_COLOR) &&
		(attr = ippFindAttribute(job_attrs, "print-color-mode", IPP_TAG_ZERO)) != NULL) {

	}
	if ((printer_type_ & CUPS_PRINTER_COPIES) &&
		(attr = ippFindAttribute(job_attrs, "copies", IPP_TAG_ZERO)) != NULL) {

	}
	if ((printer_type_ & CUPS_PRINTER_DUPLEX) &&
		(attr = ippFindAttribute(job_attrs, "sides", IPP_TAG_ZERO)) != NULL) {

	}
	if ((printer_type_ & CUPS_PRINTER_COLLATE) &&
		(attr = ippFindAttribute(job_attrs, "multiple-document-handling", IPP_TAG_ZERO)) != NULL) {

	}
	if ((attr = ippFindAttribute(job_attrs, "print-quality", IPP_TAG_ZERO)) != NULL
		) {

	}
	else if ((attr = ippFindAttribute(job_attrs, "printer-resolution", IPP_TAG_ZERO)) != NULL
		) {

	}

	DOCINFO doc_info = { 0 };
	doc_info.cbSize = sizeof(DOCINFO);
	if ((attr = ippFindAttribute(job_attrs, "document-name", IPP_TAG_NAME)) != NULL) {
		doc_info.lpszDocName = LPCWSTR(ippGetString(attr, 0, NULL));
	}
	else {
		doc_info.lpszDocName = L"Unknown";
	}


	FPDF_PAGE pdf_page;

	FPDF_CloseDocument(pdf_doc);
}

bool VirtualDriverlessPrinter::addJob(std::shared_ptr<PrintJob> job) {
	int job_id = job->getId();
	assert(job_id > -1);
	//TODO: rw lock
	jobs_.insert(std::make_pair(job_id , job));
	//TODO: kMaxJobs check
	//TODO: rw unlock
	return true;
}

std::shared_ptr<PrintJob> VirtualDriverlessPrinter::getJob(int job_id) const {
	auto it = jobs_.find(job_id);
	if (it != jobs_.end()) {
		return it->second;
	}
	return nullptr;
}

static unsigned WINAPI ProcessIPPThread(LPVOID ipp_client) {
	((IPPClient*)ipp_client)->process();
	std::cerr << "A Client Thread is end." << '\n';
	return 0;
}