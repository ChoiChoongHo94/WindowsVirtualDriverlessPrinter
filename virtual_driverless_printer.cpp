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

	/*
	 * common attrs
	 */
	/* TODO:
		color-supported
		copies-supported
		copies-default
		*/
	const char* ipp_features[] = { "ipp-everywhere" };
	const char* ipp_versions[] = { "1.1", "2.0" };
	const char* media_supported[] = { "iso_a4_210x297mm" };
	const char* document_format_supported[] = { "application/pdf" };
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-default", NULL, "application/pdf");
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", sizeof(document_format_supported) / sizeof(document_format_supported[0]), NULL, document_format_supported);
	ippAddInteger(attrs_, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies-default", 1);
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
}

VirtualDriverlessPrinter::~VirtualDriverlessPrinter() {
	WSACleanup();
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
	std::cerr << "the printer \"" << name_ << "\" is start to run!" << '\n';
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
	std::cerr << "the printer \"" << name_ << "\" is end to run!" << '\n';
}

void VirtualDriverlessPrinter::addJob(int job_id, PrintJob* job) {
	jobs_map_.insert(std::make_pair(job_id, job));
}

static unsigned WINAPI ProcessIPPThread(LPVOID ipp_client) {
	((IPPClient*)ipp_client)->process();
	std::cerr << "A Client Thread is end." << '\n';
	return 0;
}