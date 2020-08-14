#include "virtual_driverless_printer.h"
#include "ipp_client.h"
#include "my_util.h"
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <WinSock2.h>
#include <Windows.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <fpdfview.h>
#include <unordered_set>
#include <cctype>

static unsigned WINAPI ProcessIPPThread(LPVOID ipp_client);

VirtualDriverlessPrinter::VirtualDriverlessPrinter(/*const std::string& name,*/ const int port)
	: /*name_(name),*/ port_(port), start_time_(time(NULL)) {
	// hostname
	WSADATA wsa_data;
	int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
	char hostname[256] = "\0";
	gethostname(hostname, ARRAYSIZE(hostname));
	CONSOLE_LOGGER->writeLog(std::string("Windows's hostname: ") + hostname);
	const_cast<std::string&>(hostname_) = hostname;

	// uuid
	char assemble_uuid_buf[46];
	const_cast<std::string&>(uuid_) = httpAssembleUUID(hostname_.c_str(), port_, name_.c_str(), 0, assemble_uuid_buf, sizeof(assemble_uuid_buf));

	// url
	char uri[1024];
	httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL, hostname_.c_str(), port_, "/ipp/print");
	const_cast<std::string&>(uri_) = uri;

	//adminurl
	char adminurl[1024];
	httpAssembleURI(HTTP_URI_CODING_ALL, adminurl, sizeof(adminurl), "http", NULL, hostname_.c_str(), port_, "/");
	const_cast<std::string&>(adminurl_) = adminurl;

	HANDLE hprinter;
	DWORD cch_printer = ARRAYSIZE(windows_printer_name_);
	GetDefaultPrinter(windows_printer_name_, &cch_printer);
	const_cast<std::string&>(name_) = Util::wstr_to_str(windows_printer_name_, lstrlen(windows_printer_name_));
	const_cast<std::string&>(name_utf8_) = Util::wstr_to_utf8(std::wstring(windows_printer_name_));
	CONSOLE_LOGGER->writeLog(std::string("Windows's default printer: ") + Util::wstr_to_str(windows_printer_name_, cch_printer));
	if (!OpenPrinter(windows_printer_name_, &hprinter, NULL)) {
		CONSOLE_LOGGER->writeLog(std::string("OpenPrinter failed! <- ") + std::to_string(GetLastError()));
		abort();
		return;
	}

	DWORD collate, copies, duplex, color, media;
	DWORD num_papers;
	DWORD num_papersizes;
	std::unique_ptr<WORD[]> papers = nullptr;
	std::unique_ptr<POINT[]> papersizes = nullptr;
	{ /* Get Windows printer's capabilities */
		// TODO: output-bin
		collate = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_COLLATE, NULL, NULL);
		copies = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_COPIES, NULL, NULL);
		duplex = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_DUPLEX, NULL, NULL);
		color = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_COLORDEVICE, NULL, NULL);
		num_papers = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_PAPERS, NULL, NULL);
		num_papersizes = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_PAPERSIZE, NULL, NULL);
		papers = std::make_unique<WORD[]>(num_papers);
		papersizes = std::make_unique<POINT[]>(num_papersizes);
		DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_PAPERS, (LPWSTR)papers.get(), NULL);
		DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_PAPERSIZE, (LPWSTR)papersizes.get(), NULL);
	}

	//num_resolutions = DeviceCapabilities(windows_printer_name_, windows_printer_name_, DC_ENUMRESOLUTIONS, (LPWSTR)poutput, NULL);

	auto& printer_type_ref = const_cast<cups_ptype_t&>(printer_type_);
	char* media_supported[128];

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
		printer_type_ref |= CUPS_PRINTER_COLLATE;
	}

	if (copies > 0) {
		ippAddInteger(attrs_, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies-default", 1);
		ippAddRange(attrs_, IPP_TAG_PRINTER, "copies-supported", 1, 99); // FIXME possibility
		printer_type_ref |= CUPS_PRINTER_COPIES;
	}

	if (duplex > 0) {
		const char* sides_supported[] = { "one-sided", "two-sided-long-edge", "two-sided-short-edge" };
		ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-default", NULL, sides_supported[0]);
		ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-supported", sizeof(sides_supported) / sizeof(sides_supported[0]), NULL, sides_supported);
		printer_type_ref |= CUPS_PRINTER_DUPLEX;
	}

	if (color > 0) {
		const char* print_color_mode_supported[] = { "color", "monochrome" };
		ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-default", NULL, print_color_mode_supported[0]);
		ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-supported", sizeof(print_color_mode_supported) / sizeof(print_color_mode_supported[0]), NULL, print_color_mode_supported);
		ippAddBoolean(attrs_, IPP_TAG_PRINTER, "color-supported", true);
		printer_type_ref |= CUPS_PRINTER_COLOR;
	}

	if (num_papers > 0) {
		int media_supported_idx = -1;

		for (int i = 0; i < (int)num_papersizes; ++i) {
			pwg_media_t* pwg_media = pwgMediaForSize(papersizes[i].x * 10, papersizes[i].y * 10);

			if (pwg_media->ppd == nullptr || media_size_db_.find(pwg_media->pwg) != media_size_db_.cend()) {
				continue;
			}

			media_size_db_[pwg_media->ppd] = papers[i];
			media_supported[++media_supported_idx] = (char*)malloc(strlen(pwg_media->ppd) + 1);
			strcpy(media_supported[media_supported_idx], pwg_media->ppd);

			media_size_db_[pwg_media->pwg] = papers[i];
			media_supported[++media_supported_idx] = (char*)malloc(strlen(pwg_media->pwg) + 1);
			strcpy(media_supported[media_supported_idx], pwg_media->pwg);

			media_supported[media_supported_idx][63] = '\0'; // case for string overflow
			CONSOLE_LOGGER->writeLog("Supported Media(" + std::to_string(i + 1) + "): " + std::string(pwg_media->pwg));
		}

		assert(media_supported_idx != -1);
		num_papers = media_supported_idx + 1;
	}
	//if (num_resolutions > 0) {}

	/*
	 * common attrs
	 */
	const char* ipp_features[] = { "ipp-everywhere" };
	const char* ipp_versions[] = { "1.1", "2.0"};
	//const char* media_supported[] = { CUPS_MEDIA_A4 };	
	const char* document_format_supported[] = { CUPS_FORMAT_PDF };
	//const int print_quality_supported[] = { IPP_QUALITY_DRAFT, IPP_QUALITY_NORMAL, IPP_QUALITY_HIGH };
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-default", NULL, document_format_supported[0]);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", sizeof(document_format_supported) / sizeof(document_format_supported[0]), NULL, document_format_supported);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(ipp_features) / sizeof(ipp_features[0]), NULL, ipp_features);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", sizeof(ipp_versions) / sizeof(ipp_versions[0]), NULL, ipp_versions);
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-default", NULL, media_supported[0]);
	ippAddStrings(attrs_, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-supported", num_papers, NULL, media_supported);
	//ippAddInteger(attrs_, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-default", IPP_QUALITY_NORMAL);
	//ippAddIntegers(attrs_, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-supported", sizeof(print_quality_supported) / sizeof(print_quality_supported[0]), print_quality_supported);
	ippAddResolution(attrs_, IPP_TAG_PRINTER, "printer-resolution-default", IPP_RES_PER_INCH, 300, 300);
	ippAddResolution(attrs_, IPP_TAG_PRINTER, "printer-resolution-supported", IPP_RES_PER_INCH, 300, 300);

	// info
	ippAddBoolean(attrs_, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", NULL, "TEST_printer-info");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", NULL, "TEST_printer-location");
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", NULL, Util::wstr_to_str(windows_printer_name_, ARRAYSIZE(windows_printer_name_)).c_str());
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", NULL, name_.c_str());
	ippAddString(attrs_, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uuid", NULL, uuid_.c_str());

	// mutex
	if ((mutex_jobs_ = CreateMutex(NULL, FALSE, NULL)) == nullptr) {
		CONSOLE_LOGGER->writeLog(std::string("CreateMutex is failed! <- ") + std::to_string(GetLastError()));
		abort();
	};

	initBonjourService_();
	FPDF_InitLibrary();

	CONSOLE_LOGGER->writeLog("VirtualDriverlessPrinter object is created.");
}

VirtualDriverlessPrinter::~VirtualDriverlessPrinter() {
	WSACleanup();
	DNSServiceRefDeallocate(bonjour_service_);
	FPDF_DestroyLibrary();
}


void VirtualDriverlessPrinter::initBonjourService_() {
	assert(port_ != -1);
	//std::string service_name = Util::wstr_to_str(windows_printer_name_, ARRAYSIZE(windows_printer_name_));
	//const std::wstring& service_name_wstr = windows_printer_name_;
	//const std::string& service_name_utf8 = Util::wstr_to_utf8str(service_name_wstr);

	auto& bonjour_service_ref = const_cast<DNSServiceRef&>(bonjour_service_);
	DNSServiceErrorType err;
	if (err = (DNSServiceCreateConnection(&bonjour_service_ref)) != kDNSServiceErr_NoError) {
		CONSOLE_LOGGER->writeLog(std::string("Bonjour Connection is failed! <- Error code: ") + std::to_string(err));
		abort();
	}

	TXTRecordRef txt_record;
	std::string str_value;
	ipp_attribute_t* attr;
	TXTRecordCreate(&txt_record, 1024, NULL);
	TXTRecordSetValue(&txt_record, "rp", 9, "ipp/print");
	TXTRecordSetValue(&txt_record, "txtvers", 1, "1");
	TXTRecordSetValue(&txt_record, "qtotal", 1, "1");
	TXTRecordSetValue(&txt_record, "adminurl", 1, adminurl_.c_str());
	if ((attr = ippFindAttribute(attrs_, "color-supported", IPP_TAG_BOOLEAN)) != NULL) {
		TXTRecordSetValue(&txt_record, "Color", 1, ippGetBoolean(attr, 0) ? "T" : "F");
	}
	if ((attr = ippFindAttribute(attrs_, "sides-supported", IPP_TAG_KEYWORD)) != NULL) {
		TXTRecordSetValue(&txt_record, "Duplex", 1, ippGetCount(attr) > 1 ? "T" : "F");
	}
	if ((attr = ippFindAttribute(attrs_, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL) {
		str_value = ippGetString(attr, 0, NULL);
		for (int i = 1; i < ippGetCount(attr); i++) {
			str_value += "," + std::string(ippGetString(attr, 0, NULL));
		}
		TXTRecordSetValue(&txt_record, "pdl", str_value.size(), str_value.c_str());
	}
	if ((attr = ippFindAttribute(attrs_, "printer-location", IPP_TAG_TEXT)) != NULL && !(str_value = ippGetString(attr, 0, NULL)).empty()) {
		TXTRecordSetValue(&txt_record, "note", str_value.size(), str_value.c_str());
	}
	if ((attr = ippFindAttribute(attrs_, "printer-make-and-model", IPP_TAG_TEXT)) != NULL && !(str_value = ippGetString(attr, 0, NULL)).empty()) {
		TXTRecordSetValue(&txt_record, "ty", str_value.size(), str_value.c_str());
	}
	if ((attr = ippFindAttribute(attrs_, "printer-uuid", IPP_TAG_URI)) != NULL && !(str_value = ippGetString(attr, 0, NULL)).empty()) {
		TXTRecordSetValue(&txt_record, "UUID", str_value.size() - 9, str_value.c_str() + 9);
	}

	if ((err = DNSServiceRegister(&bonjour_service_ref, kDNSServiceFlagsShareConnection, kDNSServiceInterfaceIndexAny, name_utf8_.c_str(), //service_name.c_str(),
		"_ipp._tcp", NULL /* domain */, NULL /* host */, htons(port_), TXTRecordGetLength(&txt_record), TXTRecordGetBytesPtr(&txt_record), NULL, NULL)) != kDNSServiceErr_NoError) {
		CONSOLE_LOGGER->writeLog(std::string("Bonjour Service registration is failed! <- Error code: %s") + std::to_string(err));
		abort();
	}

	CONSOLE_LOGGER->writeLog(std::string("Bonjour Service('") + name_utf8_ + "') registration on '" + std::to_string(port_) + "' port is success.");
}

void VirtualDriverlessPrinter::run() {
	int timeout = -1;

	// socket
	http_addrlist_t* addrlist = httpAddrGetList(NULL, AF_INET, std::to_string(port_).c_str());
	const_cast<int&>(ipv4_) = httpAddrListen(&(addrlist->addr), port_);
	httpAddrFreeList(addrlist);

	nfds_t num_fds = 1;
	struct pollfd polldata[1];

	polldata[0].fd = ipv4_;
	polldata[0].events = POLLIN;
	/*
	polldata[1].fd = ipv6_;
	polldata[1].events = POLLIN;
	polldata[2].fd = DNSServiceRefSockFD(bonjour_service_); -> 필수적인것은 아님. Bonjour데몬과 통신하고 그에 따른 콜백을 이용할 때 필요한듯 (dns-sd.h 주석 참고)
	polldata[2].events = POLLIN;
	*/

	CONSOLE_LOGGER->writeLog(std::string("The printer '") + name_utf8_ + "' is start to run.");
	for (;;) {
		if (poll(polldata, (nfds_t)num_fds, timeout) < 0 && errno != EINTR) {
			CONSOLE_LOGGER->writeLog(std::string("WSAPoll failed! <- ") + std::to_string(WSAGetLastError()));
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
	CONSOLE_LOGGER->writeLog(std::string("The printer '") + name_ + "' exit normally.");
}

//void VirtualDriverlessPrinter::queueJob(const std::shared_ptr<PrintJob>& job) {
//	//TODO: rw lock
//	job_queue_mutex_.lock();
//	job_queue_.push(job);
//	job_queue_mutex_.unlock();
//	//TODO: rw unlock
//}
//
//std::shared_ptr<PrintJob> VirtualDriverlessPrinter::dequeueJob() {
//	job_queue_mutex_.lock();
//	auto& ret = job_queue_.front();
//	job_queue_.pop();
//	job_queue_mutex_.unlock();
//	return ret;
//}

bool VirtualDriverlessPrinter::printFile(const std::shared_ptr<PrintJob>& job) {
	assert(job->getState() == IPP_JSTATE_COMPLETED);

	std::stringstream errlog_ss;
	errlog_ss << "[" + job->getHostname() + ":" + job->getUsername() + "] " << __FUNCTION__ << '\n';

	FPDF_DOCUMENT pdf_doc = FPDF_LoadDocument(job->getFilepath().c_str(), NULL);

	if (!pdf_doc) {
		auto err = FPDF_GetLastError();
		switch (err) {
			case FPDF_ERR_SUCCESS:
				errlog_ss << "FPDF_ERR: Success.\n";
				break;
			case FPDF_ERR_UNKNOWN:
				errlog_ss << "FPDF_ERR: Unknown error.\n";
				break;
			case FPDF_ERR_FILE:
				errlog_ss << "FPDF_ERR: File not found or could not be opened.\n";
				break;
			case FPDF_ERR_FORMAT:
				errlog_ss << "FPDF_ERR: File not in PDF format or corrupted.\n";
				break;
			case FPDF_ERR_PASSWORD:
				errlog_ss << "FPDF_ERR: Password required or incorrect password.\n";
				break;
			case FPDF_ERR_SECURITY:
				errlog_ss << "FPDF_ERR: Unsupported security scheme.\n";
				break;
			case FPDF_ERR_PAGE:
				errlog_ss << "FPDF_ERR: Page not found or content error.\n";
				break;
			default:
				errlog_ss << "FPDF_ERR: Unknown error: " << err << ".\n";
				break;
		}
		ERROR_LOGGER->writeLog(errlog_ss.str());
		return false;
	}

	HANDLE hprinter;
	if (!OpenPrinter(windows_printer_name_, &hprinter, NULL)) {
		errlog_ss << "WINAPI_ERR: OpenPrinter failed! <- " << GetLastError() << ".\n";
		ERROR_LOGGER->writeLog(errlog_ss.str());
		return false;
	}

	DWORD dw_needed;
	GetPrinter(hprinter, 2, 0, 0, &dw_needed);
	PRINTER_INFO_2* pi2 = (PRINTER_INFO_2*)malloc(dw_needed);
	GetPrinter(hprinter, 2, (LPBYTE)pi2, dw_needed, &dw_needed);
	LPDEVMODE pdevmode = pi2->pDevMode;

	/*
	TODO: output-bin
	*/
	ipp_t* job_attrs = job->getAttributes();
	ipp_attribute_t* attr = nullptr;
	std::string str_value;
	{ /* applying print options and loging */
		if (printer_type_ & CUPS_PRINTER_COLOR &&
			((attr = ippFindAttribute(job_attrs, "print-color-mode", IPP_TAG_ZERO)) != NULL ||
				(attr = ippFindAttribute(job_attrs, "ColorModel", IPP_TAG_ZERO)) != NULL ||
				(attr = ippFindAttribute(job_attrs, "SelectColor", IPP_TAG_ZERO)) != NULL)) {
			str_value = ippGetString(attr, 0, NULL);
			std::transform(std::begin(str_value), std::end(str_value), std::begin(str_value),
				[](char ch) {return std::tolower(ch); });
			if (str_value == "color" || str_value == "rgb") {
				pdevmode->dmColor = DMCOLOR_COLOR;
			}
			else if (str_value == "monochrome" || str_value == "gray" || str_value == "grayscale") {
				pdevmode->dmColor = DMCOLOR_MONOCHROME;
			}
			pdevmode->dmFields |= DM_COLOR;
			errlog_ss << "PRINT_OPT: " << Util::get_attr_stamp(attr) << '\n';
		}
		if ((printer_type_ & CUPS_PRINTER_COPIES) &&
			(attr = ippFindAttribute(job_attrs, "copies", IPP_TAG_ZERO)) != NULL) {
			pdevmode->dmCopies = ippGetInteger(attr, 0);
			pdevmode->dmFields |= DM_COPIES;
			errlog_ss << "PRINT_OPT: " << Util::get_attr_stamp(attr) << '\n';
		}
		if ((printer_type_ & CUPS_PRINTER_DUPLEX) &&
			(attr = ippFindAttribute(job_attrs, "sides", IPP_TAG_ZERO)) != NULL) {
			str_value = ippGetString(attr, 0, NULL);
			if (str_value == "one-sided") {
				pdevmode->dmDuplex = DMDUP_SIMPLEX;
			}
			else if (str_value == "two-sided-long-edge") {
				pdevmode->dmDuplex = DMDUP_VERTICAL;
			}
			else if (str_value == "two-sided-short-edge") {
				pdevmode->dmDuplex = DMDUP_HORIZONTAL;
			}
			pdevmode->dmFields |= DM_DUPLEX;
			errlog_ss << "PRINT_OPT: " << Util::get_attr_stamp(attr) << '\n';
		}
		if ((printer_type_ & CUPS_PRINTER_COLLATE) &&
			(attr = ippFindAttribute(job_attrs, "multiple-document-handling", IPP_TAG_ZERO)) != NULL) {
			str_value = ippGetString(attr, 0, NULL);
			if (str_value == "single-document" || str_value == "separate-documents-collated-copies") {
				pdevmode->dmCollate = DMCOLLATE_TRUE;
			}
			else if (str_value == "separate-documents-uncollated-copies") {
				pdevmode->dmCollate = DMCOLLATE_FALSE;
			}
			pdevmode->dmFields |= DM_COLLATE;
			errlog_ss << "PRINT_OPT: " << Util::get_attr_stamp(attr) << '\n';
		}

		if ((attr = ippFindAttribute(job_attrs, "media", IPP_TAG_KEYWORD)) != NULL) {
			str_value = ippGetString(attr, 0, NULL);
			pdevmode->dmPaperSize = media_size_db_[str_value];
			pdevmode->dmFields |= DM_PAPERSIZE;
			errlog_ss << "PRINT_OPT: " << Util::get_attr_stamp(attr) << '\n';
		}
	}

	// FIXME: only for test
	//pdevmode->dmPrintQuality = DMRES_DRAFT;
	//pdevmode->dmFields |= DM_PRINTQUALITY;

	{/* only for logging, TODO: 추후에 로깅 필요한 옵션 추가 */
	}

	/* TODO
	if ((attr = ippFindAttribute(job_attrs, "print-quality", IPP_TAG_ZERO)) != NULL) {

	}
	else if ((attr = ippFindAttribute(job_attrs, "printer-resolution", IPP_TAG_ZERO)) != NULL) {

	}
	*/

	/* Windows Spooling */
	DOCINFO doc_info = { 0 };
	doc_info.cbSize = sizeof(DOCINFO);
	constexpr size_t k_docname_buf_size = 256;
	wchar_t docname_buf[k_docname_buf_size];
	if ((attr = ippFindAttribute(job_attrs, "job-name", IPP_TAG_NAME)) != NULL) {
		std::string job_name = ippGetString(attr, 0, NULL);
		job_name = job_name.substr(job_name.find(" - ") + 3); // assume 'job-name' set from CUPS
		wcscpy(docname_buf, Util::utf8_to_wstr(job_name).c_str());
		doc_info.lpszDocName = docname_buf;
		errlog_ss << "JOB_ATTR: " << Util::get_attr_stamp(attr) << '\n';
	}
	else {
		doc_info.lpszDocName = L"untitled";
		errlog_ss << "JOB_ATTR: " << "'job-name' is not supplied (set 'untitled').\n";
	}

	HDC hdc = CreateDC(L"WINSPOOL", windows_printer_name_, NULL, pdevmode);
	if (hdc == nullptr) {
		errlog_ss << "WINAPI_ERR: " << "CreateDC failed! <- " << GetLastError() << ".\n";
		ERROR_LOGGER->writeLog(errlog_ss.str());
		return false;
	}

	if (StartDoc(hdc, &doc_info) <= 0) {
		errlog_ss << "WINAPI_ERR: " << "StartDoc failed! <- " << GetLastError() << ".\n";
		ERROR_LOGGER->writeLog(errlog_ss.str());
		return false;
	}

	int num_pdf_pages = FPDF_GetPageCount(pdf_doc);
	double pdf_page_width, pdf_page_height, logpixelsx, logpixelsy, size_x, size_y;
	FPDF_PAGE pdf_page = nullptr;
	int render_flags = FPDF_ANNOT | FPDF_PRINTING | FPDF_NO_CATCH;

	if (pdevmode->dmColor == DMCOLOR_MONOCHROME) {
		render_flags |= FPDF_GRAYSCALE;
	}

	for (int i = 0; i < num_pdf_pages; i++) {
		pdf_page = FPDF_LoadPage(pdf_doc, i);

		if (pdf_page == nullptr) {
			errlog_ss << "FPDF_ERR: " << "FPDF_LoadPage failed! <- " << FPDF_GetLastError() << ".\n";
			ERROR_LOGGER->writeLog(errlog_ss.str());
			return false;
		}

		pdf_page_width = FPDF_GetPageWidth(pdf_page);
		pdf_page_height = FPDF_GetPageHeight(pdf_page);
		logpixelsx = GetDeviceCaps(hdc, LOGPIXELSX);
		logpixelsy = GetDeviceCaps(hdc, LOGPIXELSY);
		size_x = pdf_page_width * logpixelsx / 72;
		size_y = pdf_page_height * logpixelsy / 72;
		StartPage(hdc);
		FPDF_RenderPage(hdc, pdf_page, 0, 0, size_x, size_y, 0, render_flags);
		EndPage(hdc);
		errlog_ss << "Page #" << i + 1 << "(total:" << num_pdf_pages << ") with " << size_x << ", " << size_y << '\n';
	}

	FPDF_CloseDocument(pdf_doc);
	EndDoc(hdc);
	ClosePrinter(hprinter);
	ERROR_LOGGER->writeLog(errlog_ss.str());
	job->unlinkJobFile();
	return true;
}

bool VirtualDriverlessPrinter::addJob(int job_id, std::shared_ptr<PrintJob> job) {
	assert(job_id > -1);
	if (WaitForSingleObject(mutex_jobs_, INFINITE) != WAIT_OBJECT_0) {
		CONSOLE_LOGGER->writeLog(std::string("WaitForSingleObject is failed! <- ") + std::to_string(GetLastError()));
		return false;
	}
	//TODO: duplicate check -> return false;
	jobs_.insert(std::make_pair(job_id , job));
	//TODO: kMaxJobs check
	ReleaseMutex(mutex_jobs_);
	return true;
}

bool VirtualDriverlessPrinter::removeJob(int job_id) {
	if (WaitForSingleObject(mutex_jobs_, INFINITE) != WAIT_OBJECT_0) {
		CONSOLE_LOGGER->writeLog(std::string("WaitForSingleObject is failed! <- ") + std::to_string(GetLastError()));
		return false;
	}
	jobs_.erase(job_id);
	ReleaseMutex(mutex_jobs_);
	return true;
}

std::shared_ptr<PrintJob> VirtualDriverlessPrinter::getJob(int job_id) const {
	if (WaitForSingleObject(mutex_jobs_, INFINITE) != WAIT_OBJECT_0) {
		CONSOLE_LOGGER->writeLog(std::string("WaitForSingleObject is failed! <- ") + std::to_string(GetLastError()));
		return nullptr;
	}
	auto it = jobs_.find(job_id);
	if (it == jobs_.end()) {
		return nullptr;
	}
	ReleaseMutex(mutex_jobs_);
	return it->second;
}

// TODO: examine deletion
//static unsigned WINAPI ProcessIPPThread(LPVOID ipp_client) {
//	((IPPClient*)ipp_client)->process();
//	std::cerr << "A Client Thread is end." << '\n';
//	return 0;
//}
//
//void CALLBACK VirtualDriverlessPrinter::ipp_client_routine_(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
//
//}