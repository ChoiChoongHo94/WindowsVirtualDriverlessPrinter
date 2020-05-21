#include <Windows.h>
#include <iostream>
#include <cups/ipp.h>
#include <cups/config.h>
#include <cstring>
#include <cassert>
#include "ipp_client.h"
#include "print_job.h"
#include "my_definitions.h"
#include "my_util.h"

static std::shared_ptr<PrintJob> create_job(ipp_t* reqeust, VirtualDriverlessPrinter* vdp);

namespace TestPrint {
	void printCupsArray(cups_array_t* ca) {
		std::cerr << "============================" << __FUNCTION__ << "============================" << '\n';
		std::cerr << "Array count: " << cupsArrayCount(ca) << '\n';
		for (auto element = cupsArrayFirst(ca); element; element = cupsArrayNext(ca)) {
			std::cout << (char*)element << '\n';
		}
		std::cerr << "=================================================================================" << '\n';
	}

	void printIPPAttrs(ipp_t* ipp_msg) {
		std::cerr << "============================" << __FUNCTION__ << "=============================" << '\n';
		for (auto attr = ippFirstAttribute(ipp_msg); attr; attr = ippNextAttribute(ipp_msg)) {
			auto value_tag = ippGetValueTag(attr);
			std::cerr << ippGetName(attr) << ", " <<
				ippTagString(ippGetGroupTag(attr)) << "/" << ippTagString(value_tag);
			if (value_tag >= IPP_TAG_TEXT) {
				std::cerr << ", " << ippGetString(attr, 0, NULL) << '\n';
			}
			else {
				std::cerr << '\n';
			}
		}
		std::cerr << "=================================================================================" << '\n';
	}
}

IPPClient::HTTPClient::HTTPClient(int sock_fd) {
	std::cerr << "[HTTPClient(" << this << ") ctor]" << '\n';
	char hostname[1024];
	if ((http_ = httpAcceptConnection(sock_fd, 1)) != nullptr) {
		//char uri[1024];
		httpGetHostname(http_, hostname, sizeof(hostname));
		hostname_ = hostname;
		
	}
	//operation_ = httpGetState(http_);
	//else {
		std::cerr << "httpAcceptConnection(), error code: " << httpError(http_) <<
			", initial state: " << httpStateString(httpGetState(http_)) << '\n';
		std::cerr << "Client hostname: " << hostname_ << '\n';
	//}
		//std::cerr << "HTTPClient created, hostname_: " << hostname << '\n';
}

IPPClient::IPPClient(VirtualDriverlessPrinter* vdp, int sock_fd) : vdp_(vdp) {
	std::cerr << "[IPPClient(" << this << ") ctor]" << '\n';
	http_client_ = std::make_shared<HTTPClient>(sock_fd);
}

IPPClient::HTTPClient::~HTTPClient() {
	std::cerr << "[HTTPClient(" << this << ") dtor]" << '\n';
	httpClose(http_);
}

IPPClient::~IPPClient() {
	std::cerr << "[IPPClient(" << this << ") dtor]" << '\n';
}

bool IPPClient::HTTPClient::process(ipp_t*& ipp_request) {
	// Test
	if (hostname_ != "192.168.8.118") {
		std::cerr << "@@ not allowed client @@" << '\n';
		if (!respond(HTTP_STATUS_NOT_IMPLEMENTED, "", "", 0, nullptr)) {
			std::cerr << "@@ respond false @@" << '\n';
		}
		return false;
	}

	std::cerr << "[" << __FUNCTION__ << "] Enter" << '\n';
	char uri[1024];
	char scheme[32];
	char userpass[128];
	char hostname[HTTP_MAX_HOST];
	int port = -1;
	char resource[HTTP_MAX_HOST];
	http_state_t state;
	http_status_t status;
	bool ret = true;

	// 1)
	while ((state = httpReadRequest(http_, uri, sizeof(uri))) == HTTP_STATE_WAITING)
		sleep(1);
	 
	// 2)
	if (state == HTTP_STATE_ERROR) {
		if (httpError(http_) == EPIPE) { // error case of being set to ignoring any signals when the socket or pipe is disconnected
			std::cerr << "'" << hostname_ << "' client closed connection!" << '\n';
		}
		else {
			std::cerr << "'" << hostname_ << "' bad request line! <- " << httpError(http_) << '\n';
		}
		ret = false; goto EXIT;
	}
	else if (state == HTTP_STATE_UNKNOWN_METHOD) {
		std::cerr << "'" << hostname_ << "' bad/unknown operation!" << '\n';
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto EXIT;
	}
	else if (state == HTTP_STATE_UNKNOWN_VERSION) {
		std::cerr << "'" << hostname_ << "' bad HTTP version!" << '\n';
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto EXIT;
	}

	/*
	std::cerr << "'" << hostname_ << "' " << httpStateString(state) << '\n';
	//operation_ = httpGetState(http_);
	while ((status = httpUpdate(http_)) == HTTP_STATUS_CONTINUE);

	if (status != HTTP_STATUS_OK) {
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, NULL);
		return false;
	}
	*/

	// 3)
	if (httpSeparateURI(HTTP_URI_CODING_MOST, uri, scheme, sizeof(scheme),
		userpass, sizeof(userpass),
		hostname, sizeof(hostname), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK &&
		(state != HTTP_STATE_OPTIONS || strcmp(uri, "*"))) {
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto EXIT;
	}

	/* process the request */
	// ippsample/ippeveprinter.c:5940
	// 4)
	start_ = time(NULL);
	operation_ = httpGetState(http_);
	std::cerr << "HTTP Operation: " << httpStateString(operation_) << '\n';
	while ((status = httpUpdate(http_)) == HTTP_STATUS_CONTINUE);

	if (status != HTTP_STATUS_OK) {
		respond(HTTP_STATUS_BAD_REQUEST, "", "", 0, nullptr);
		ret = false; goto EXIT;
	}

	/* validate the header of the HTTP request */
	// 5)
	if (!httpGetField(http_, HTTP_FIELD_HOST)[0] &&
		httpGetVersion(http_) >= HTTP_VERSION_1_1) {
		std::cerr << "Missing 'Host:' field in the HTTP/1.1 and higher version request!" << '\n';
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto EXIT;
	}

	// 6)
	if (std::string{ "Upgrade" } == httpGetField(http_, HTTP_FIELD_CONNECTION)) {
		if (!respond(HTTP_STATUS_NOT_IMPLEMENTED, NULL, NULL, 0, nullptr)) {
			ret = false; goto EXIT;
		}
	}

	// ippsample/ippeveprinter.c:process_ipp()
	status = httpGetExpect(http_);
	if (status &&
		(operation_ == HTTP_STATE_POST || operation_ == HTTP_STATE_PUT)) {
		if (status == HTTP_STATUS_CONTINUE) {
			if (!respond(HTTP_STATUS_CONTINUE, "", "", 0, nullptr)) {
				ret = false; goto EXIT;
			}
		}
		else {
			if (!respond(HTTP_STATUS_EXPECTATION_FAILED, NULL, NULL, 0, nullptr)) {
				ret = false; goto EXIT;
			}
		}
	}
	
	//std::string encoding = httpGetContentEncoding(http_);
	// 7)
	switch (operation_) {
	case HTTP_STATE_POST:
		if (std::string{ "application/ipp" } != httpGetField(http_, HTTP_FIELD_CONTENT_TYPE)) {
			ret = respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr); goto EXIT;
		}

		/* IPP request */
		ipp_request = ippNew();
		for (ipp_state_t ipp_state = ippRead(http_, ipp_request);
			ipp_state != IPP_STATE_DATA;
			ipp_state = ippRead(http_, ipp_request)) {
			if (ipp_state == IPP_STATE_ERROR) {
				//std::cerr << "IPP read error (%s)!" << ippStateString(ipp_state) << '\n';
				respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
			}
		}
		TestPrint::printIPPAttrs(ipp_request);
		break;

	case HTTP_STATE_OPTIONS:
	case HTTP_STATE_HEAD:
	case HTTP_STATE_GET:
		ret =  respond(HTTP_STATUS_NOT_FOUND, NULL, NULL, 0, nullptr);
		break;

	default:
		ret = false;
		break;
	}

EXIT:
	std::cerr << "[" << __FUNCTION__ << "] Exit state/status: " << httpStateString(httpGetState(http_)) << "/" <<
		httpStatus(httpGetStatus(http_)) << '\n';
	return ret;
}

bool IPPClient::HTTPClient::respond(http_status_t status, std::string content_encoding,
	std::string mime_type, size_t length, ipp_t* ipp_response) {
	if (status == HTTP_STATUS_CONTINUE) {
		return (httpWriteResponse(http_, HTTP_STATUS_CONTINUE) == 0);
	}

	/*
	* Format an error message...
	*/
	char message[1024];
	if (mime_type.empty() && !length && status != HTTP_STATUS_OK && status != HTTP_STATUS_SWITCHING_PROTOCOLS)
	{
		snprintf(message, sizeof(message), "%d - %s\n", status, httpStatus(status));
		mime_type = "text/plain";
		length = strlen(message);
	}
	else
		message[0] = '\0';

	/* send HTTP response header */
	httpClearFields(http_);
	if (status == HTTP_STATUS_METHOD_NOT_ALLOWED || httpGetState(http_) == HTTP_STATE_OPTIONS) {
		httpSetField(http_, HTTP_FIELD_ALLOW, "GET, HEAD, OPTIONS, POST");
	}

	if (!mime_type.empty()) {
		if (mime_type == "text/html") {
			httpSetField(http_, HTTP_FIELD_CONTENT_TYPE, "text/html; charset=utf-8");
		}
		else {
			httpSetField(http_, HTTP_FIELD_CONTENT_TYPE, mime_type.c_str());
		}

		if (!content_encoding.empty()) {
			httpSetField(http_, HTTP_FIELD_CONTENT_ENCODING, content_encoding.c_str());
		}
	}

	httpSetLength(http_, length);

	std::cerr << "[" << __FUNCTION__ << "] httpWriteResponse: ";
	if (httpWriteResponse(http_, status) < 0) {
		std::cerr << "failed! <- " << httpStateString(httpGetState(http_)) << "/" << httpStatus(status);
		return false;
	}
	std::cerr << httpStateString(httpGetState(http_)) << "/" << httpStatus(status);

	/* send the response */
	if (message[0]) {
		if (httpPrintf(http_, "%s", message) < 0) {
			return false;
		}
		if (httpWrite2(http_, "", 0) < 0) {
			return false;
		}
	} else if (ipp_response) {
		ippSetState(ipp_response, IPP_STATE_IDLE);
		ipp_state_t state = ippWrite(http_, ipp_response);
		std::cerr << ", " << ippStateString(state);
		if (state != IPP_STATE_DATA) {
			return false;
		}
	}
	std::cerr << '\n';
	return true;
}

bool IPPClient::process() {
	std::cerr << "[" << __FUNCTION__ << "] Enter" << '\n';
	bool http_respond_ret = true;
	http_t* http = http_client_->getConnection();
	while (http_respond_ret && httpWait(http, 30000)) {
		bool http_process_ret = http_client_->process(request_);
		if (http_process_ret && request_ == nullptr) {
			std::cerr << "The HTTP request is processed. (IPP request 'request_' is NULL)" << '\n';
			return true;
		}
		else if (!http_process_ret) {
			std::cerr << "The HTTP Process is failed!" << '\n';
			return false;
		}

		std::cerr << "IPP Operation: " << ippOpString(ippGetOperation(request_)) << ", ";
		response_ = ippNewResponse(request_);
		int major, minor;
		major = ippGetVersion(request_, &minor);
		if (!((major == 2 && minor == 0) || (major == 1 && minor == 1))) {
			respond(IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED, "Bad request version numver %d.%d!", major, minor);
		}
		else if ((ipp_request_id_ = ippGetRequestId(request_)) <= 0) {
			respond(IPP_STATUS_ERROR_BAD_REQUEST, "Bad request-id %d!", ipp_request_id_);
		}
		else if (!ippFirstAttribute(request_)) {
			respond(IPP_STATUS_ERROR_BAD_REQUEST, "No attributes in request!");
		}
		else {
			ipp_tag_t group;
			ipp_attribute_t* uri = nullptr;
			ipp_attribute_t* charset = nullptr;
			ipp_attribute_t* language = nullptr;
			ipp_attribute_t* attr = nullptr;
			std::string name;

			/* get group tag */
			for (attr = ippFirstAttribute(request_), group = ippGetGroupTag(attr);
				attr; attr = ippNextAttribute(request_), group = ippGetGroupTag(attr)) {
				if (ippGetGroupTag(attr) < group && ippGetGroupTag(attr) != IPP_TAG_ZERO) {
					respond(IPP_STATUS_ERROR_BAD_REQUEST, "Attribute groups are ut of order (%x < %x)!"
						, ippGetGroupTag(attr), group);
				}
			}
			
			if (!attr) {
				attr = ippFirstAttribute(request_);
				name = ippGetName(attr);
				if (attr && !name.empty() && name == "attributes-charset" &&
					ippGetValueTag(attr) == IPP_TAG_CHARSET) {
					charset = attr;
				}

				attr = ippNextAttribute(request_);
				name = ippGetName(attr);
				if (attr && !name.empty() && name == "attributes-natural-language" &&
					ippGetValueTag(attr) == IPP_TAG_LANGUAGE) {
					language = attr;
				}

				if ((attr = ippFindAttribute(request_, "printer-uri", IPP_TAG_URI)) != nullptr) {
					uri = attr;
				}
				else if ((attr = ippFindAttribute(request_, "job-uri", IPP_TAG_URI)) != nullptr) {
					uri = attr;
				}
				
				if (charset &&
					!(std::string{ "us-ascii" } == ippGetString(charset, 0, NULL) || 
					std::string{ "utf-8" } == ippGetString(charset, 0, NULL))) {
					respond(IPP_STATUS_ERROR_BAD_REQUEST, "Unsupported character set '%s'!"
						, ippGetString(charset, 0, NULL));
				}
				else if (!charset || !language || !uri) {
					respond(IPP_STATUS_ERROR_BAD_REQUEST, "Missing required attributes!");
				}
				else {
					char scheme[32];
					char userpass[32];
					char host[256];
					char resource[256];
					int port;

					name = ippGetName(uri);
					if (httpSeparateURI(HTTP_URI_CODING_ALL, ippGetString(uri, 0, NULL),
						scheme, sizeof(scheme),
						userpass, sizeof(userpass),
						host, sizeof(host), &port,
						resource, sizeof(resource)) < HTTP_URI_STATUS_OK) {
						respond(IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Bad %s value '%s'!"
							, name, ippGetString(uri, 0, NULL));
					}
					else if ((name == "job-uri" && strncmp(resource, "/ipp/print/", 11)) ||
						(name == "printer-uri" && strcmp(resource, "/ipp/print"))) {
						respond(IPP_STATUS_ERROR_NOT_FOUND, "%s %s not found!", name, ippGetString(uri, 0, NULL));
					}
					else {
						std::cerr << "uri: " << ippGetString(uri, 0, NULL) << '\n';
						/* processing the operation */
						ipp_operation_ = ippGetOperation(request_);
						switch (ipp_operation_) {

						case IPP_OP_CREATE_JOB:
							break;

						case IPP_OP_SEND_DOCUMENT:
							break;

						case IPP_OP_PRINT_JOB:
							ippPrintJob_();
							break;

						case IPP_OP_GET_PRINTER_ATTRIBUTES:
							ippGetPrinterAttributes_();
							break;

						case IPP_OP_GET_JOBS:
							ippGetJobs_();
							break;

						case IPP_OP_GET_JOB_ATTRIBUTES:
							ippGetJobAttributes_();
							break;

						/* TODO
						case IPP_OP_PRINT_URI:
							break;
						case IPP_OP_VALIDATE_JOB:
							break;
							break;
						case IPP_OP_SEND_URI:
							break;
						case IPP_OP_CANCEL_JOB:
							break;
						case IPP_OP_CLOSE_JOB:
							break;
						case IPP_OP_IDENTIFY_PRINTER:
							break;
						*/

						default:
							respond(IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED, "Operation not supported!");
							break;
						} // switch(operation_) end
					}
				} // ipp main 'if' statement end
			} // 'if (!attr)' statement end
		} // main 'if' statement end

		if (httpGetState(http) != HTTP_STATE_POST_SEND) {
			httpFlush(http);
		}
		http_respond_ret = http_client_->respond(HTTP_STATUS_OK, "", "application/ipp", ippLength(response_), response_);
		if (ippGetStatusCode(response_) >= IPP_STATUS_ERROR_BAD_REQUEST) {
			break;
		}
	} // 'while(httpWait(..))' loop end

	std::cerr << "[" << __FUNCTION__ << "] Exit state/status: " << ippStateString(ippGetState(response_)) << "/" <<
		ippErrorString(ippGetStatusCode(response_)) << ", Return: " << http_respond_ret << '\n';
	return http_respond_ret;
}

template <typename... Args>
void IPPClient::respond(ipp_status_t status, const std::string& message_format, Args... args) {
	ippSetStatusCode(response_, status);
	std::string formatted = "";
	if (!message_format.empty()) {
		ipp_attribute_t* attr = ippFindAttribute(response_, "status-message", IPP_TAG_TEXT);
		if (attr) {
			ippSetStringf(response_, &attr, 0, message_format.c_str(), args...);
		}
		else {
			attr = ippAddStringf(response_, IPP_TAG_OPERATION, IPP_TAG_TEXT, "status-message", NULL, message_format.c_str(), args...);
		}

		formatted = ippGetString(attr, 0, NULL);
	}

	std::cerr << "[" << __FUNCTION__ << "] " << ippOpString(ipp_operation_) << ", " << ippErrorString(status);
	if (!formatted.empty()) {
		std::cerr << " (" << formatted << ")";
	}
	std::cerr << '\n';
}

void IPPClient::respondUnsupported(ipp_attribute_t* attr) {
	respond(IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Unsupported %s %s%s value!", ippGetName(attr),
		(ippGetCount(attr) > 1 ? "1setOf " : ""), ippTagString(ippGetValueTag(attr)));

	ipp_attribute_t* temp = ippCopyAttribute(response_, attr, 0);
	ippSetGroupTag(response_, &temp, IPP_TAG_UNSUPPORTED_GROUP);
}

void IPPClient::ippGetPrinterAttributes_() {
	std::cout << "[" << __FUNCTION__ << "] Enter" << '\n';
	TestPrint::printIPPAttrs(request_);
	cups_array_t* ra = ippCreateRequestedArray(request_);
	TestPrint::printCupsArray(ra);

	respond(IPP_STATUS_OK, "");

	// TODO: It seems to need a rw lock.
	// copy attrs
	
	TestPrint::printIPPAttrs(response_);
	Util::copy_attributes(response_, vdp_->getAttributes(), NULL, IPP_TAG_ZERO, IPP_TAG_CUPS_CONST);
	TestPrint::printIPPAttrs(response_);

	/*
	In RFC 8011, Section 4.2.5.1. "Get-Printer-Attributes Request",
	If the 'requested-attributes' is NULL(i.e. here the 'ra' is NULL),
	this means that the client is interested to 'all' attributes.
	*/
	if (!ra || cupsArrayFind(ra, (void*)"printer-uri-supported")) {
		ippAddString(response_, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", NULL, vdp_->getURI().c_str());
	}
	if (!ra || cupsArrayFind(ra, (void*)"printer-state")) {
		ippAddInteger(response_, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", (int)vdp_->getState());
	}
	// TODO: unlock

	//cupsArrayDelete(ra);
	TestPrint::printIPPAttrs(response_);
	std::cout << "[" << __FUNCTION__ << "] Exit" << '\n';
}

void IPPClient::ippPrintJob_() {
	std::cout << "[" << __FUNCTION__ << "] Enter" << '\n';
	if (!validJobAttributes_()) {
		// httpflush, flush_document_data
		httpFlush(http_client_->getConnection());
		return;
	}

	if (!haveDocumentData_()) {
		respond(IPP_STATUS_ERROR_BAD_REQUEST, "No file in request!");
		return;
	}

	// 필요 없을듯
	if ((job_ = std::make_shared<PrintJob>(request_, vdp_)) == nullptr) {
		respond(IPP_STATUS_ERROR_BUSY, "Currently printing another job!");
		return;
	}

	size_t bytes = 0;
	char buf[4096];
	char err_buf[1024];
	int job_file_fd = -1;
	cups_array_t* ra = nullptr;
	if ((job_file_fd = job_->createJobFile()) < 0) {
		strerror_s(err_buf, sizeof(err_buf), errno);
		respond(IPP_STATUS_ERROR_INTERNAL, "Unable to create print file: %s!", err_buf);
		goto ABORT_JOB;
	}
	std::cerr << "Created job file '" << job_->getFilename() << "'" << '\n';
	
	while ((bytes = httpRead2(http_client_->getConnection(), buf, sizeof(buf))) > 0) {
		if (write(job_file_fd, buf, (size_t)bytes) < bytes) {
			/* write error */
			int err = errno;
			job_->closeJobFile();
			job_->unlinkJobFile();
			strerror_s(err_buf, sizeof(err_buf), err);
			respond(IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s!", err_buf);
			goto ABORT_JOB;
		}
	}
	
	if (bytes < 0) {
		/* error while reading the print data */
		job_->closeJobFile();
		job_->unlinkJobFile();
		respond(IPP_STATUS_ERROR_INTERNAL, "Unable to read print file!");
		goto ABORT_JOB;
	}
	
	if (job_->closeJobFile()) {
		int err = errno;
		job_->unlinkJobFile();
		respond(IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(err));
		goto ABORT_JOB;
	}
	
	job_->setState(IPP_JSTATE_PENDING);
	
	//TODO: create 'process_job' thread
	job_->process();
	
	respond(IPP_STATUS_OK, "");
	
	ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
	cupsArrayAdd(ra, (void*)"job-id");
	cupsArrayAdd(ra, (void*)"job-state");
	//cupsArrayAdd(ra, "job-state-message");
	cupsArrayAdd(ra, (void*)"job-state-reasons");
	cupsArrayAdd(ra, (void*)"job-uri");
	
	Util::copy_job_attributes(response_, job_.get(), ra);
	TestPrint::printIPPAttrs(response_);
	cupsArrayDelete(ra);
	std::cout << "[" << __FUNCTION__ << "] Exit, Success" << '\n';
	return;
	
	ABORT_JOB:
	job_->setState(IPP_JSTATE_ABORTED);
	job_->setCompletedTime(time(NULL));
	
	ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
	cupsArrayAdd(ra, (void*)"job-id");
	cupsArrayAdd(ra, (void*)"job-state");
	cupsArrayAdd(ra, (void*)"job-state-reasons");
	cupsArrayAdd(ra, (void*)"job-uri");
	Util::copy_job_attributes(response_, job_.get(), ra);
	TestPrint::printIPPAttrs(response_);
	cupsArrayDelete(ra);
	std::cout << "[" << __FUNCTION__ << "] Exit, Aborted" << '\n';
	return;
};

// TODO: 리턴타입 검토
PrintJob* IPPClient::ippCreateJob_() {
	std::cout << "[" << __FUNCTION__ << "] Enter" << '\n';

	if (haveDocumentData_()) {
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_BAD_REQUEST, "Document data must not be supplied from this IPP message!");
		return nullptr;
	}

	if (!validJobAttributes_()) {
		return nullptr;
	}

	
}

void IPPClient::ippSendDocument_() {

}

void IPPClient::ippGetJobs_() {
	std::cout << "[" << __FUNCTION__ << "] Enter" << '\n';
	ipp_attribute_t* attr = nullptr;
	cups_array_t* ra = nullptr;
	int job_comparison;
	ipp_jstate_t job_state_criteria;
	std::string which_jobs;
	if ((attr = ippFindAttribute(request_, "which-jobs", IPP_TAG_KEYWORD)) != NULL) {
		which_jobs = ippGetString(attr, 0, NULL);
		std::cerr << http_client_->getHostname() << " which-jobs=" << which_jobs << '\n';
	}

	if (which_jobs.empty() || which_jobs == "not-completed") {
		job_comparison = -1;
		job_state_criteria = IPP_JSTATE_STOPPED;
	}
	else if (which_jobs == "completed") {
		job_comparison = 1;
		job_state_criteria = IPP_JSTATE_CANCELED;
	} // "aborted", "canceled" .. in 'ippeveprinter.c'?
	else {
		respond(IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "The which-jobs value \"%s\" is not supported!", which_jobs);
		ippAddString(response_, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD, "which-bjos", NULL, which_jobs.c_str());
		return;
	}

	int limit = 0;
	if ((attr = ippFindAttribute(request_, "limit", IPP_TAG_INTEGER)) != NULL) {
		limit = ippGetInteger(attr, 0);
	}
	std::cerr << http_client_->getHostname() << " Get-Jobs limit=" << limit << '\n';

	int first_job_id = 1;
	if ((attr = ippFindAttribute(request_, "first-job-id", IPP_TAG_INTEGER)) != NULL) {
		first_job_id = ippGetInteger(attr, 0);
	}
	std::cerr << http_client_->getHostname() << " Get-Jobs first-job-id=" << first_job_id << '\n';

	std::string username;
	if ((attr = ippFindAttribute(request_, "my-jobs", IPP_TAG_BOOLEAN)) != NULL) {
		bool my_jobs = ippGetBoolean(attr, 0);
		std::cerr << http_client_->getHostname() << "Get-Jobs my-jobs=" << (my_jobs ? "true" : "false") << '\n';

		if (my_jobs) {
			if ((attr = ippFindAttribute(request_, "requesting-user-name", IPP_TAG_NAME)) == NULL) {
				respond(IPP_STATUS_ERROR_BAD_REQUEST, "NeedRequesting-user-name with my-jobs!");
				return;
			}

			username = ippGetString(attr, 0, NULL);
			std::cerr << http_client_->getHostname() << " Get-Jobs requesting-user-name=\"" << username << "\"" << '\n';
		}
	}

	/* build a list of jobs for this printer */
	ra = ippCreateRequestedArray(request_);
	respond(IPP_STATUS_OK, "");

	//TODO: rw lock
	int count = 0;
	for (auto element : vdp_->getJobs()) {
		int job_id = element.first;
		PrintJob* job = element.second;
		const std::string& job_username = job->getUsername();
		if ((job_comparison < 0 && job->getState() > job_state_criteria) ||
			(job_comparison == 0 && job->getState() != job_state_criteria) ||
			(job_comparison > 0 && job->getState() < job_state_criteria) ||
			job_id < first_job_id ||
			(!username.empty() && !job_username.empty() && (username != job_username))) {
			continue;
		}

		if (count > 0) {
			ippAddSeparator(response_);
		}

		count++;
		Util::copy_job_attributes(response_, job, ra);
	}
	cupsArrayDelete(ra);
	//TODO: rw unlock

	std::cout << "[" << __FUNCTION__ << "] Exit, Success" << '\n';
}

void IPPClient::ippGetJobAttributes_() {
	ipp_attribute_t* attr = nullptr;
	int job_id = -1;
	
	/* find job */
	if ((attr = ippFindAttribute(request_, "job-id", IPP_TAG_INTEGER)) != NULL) {
		job_id = ippGetInteger(attr, 0);
	}
	else if ((attr = ippFindAttribute(request_, "job-uri", IPP_TAG_URI)) != NULL) {
		// FIXME
		assert(1);
	}

	//TODO: rw lock
	const auto& jobs = vdp_->getJobs(); // unordered_map
	auto it = jobs.find(job_id);
	if (it == jobs.end()) {
		respond(IPP_STATUS_ERROR_NOT_FOUND, "job-id=%d Job not found!", job_id);
		return;
	}
	PrintJob* job = it->second;
	//TODO: rw unlock

	respond(IPP_STATUS_OK, "");
	cups_array_t* ra = ippCreateRequestedArray(request_);
	Util::copy_job_attributes(response_, job, ra);
	cupsArrayDelete(ra);
}

bool IPPClient::validJobAttributes_() {
	TestPrint::printIPPAttrs(request_);
	/*
	validDocAttributes_()
	"copies"
	"ipp-attribute-fidelity"
	"job-hold-until"
	"job-impressions"
	"job-name"
	"job-sheets"
	"media"
	"page-ranges"
	"print-quality"
	"orientation-requested"
	"multiple-document-handling"
	"media-col"
	"printer-resolution
	"sides"
	*/
	return true;
}

// TODO: static function?
bool IPPClient::validDocAttributes_() {
	/*
	"compression"
	"document-format"
	"document-name"
	*/
	return true;
};

// TODO: static function?
bool IPPClient::haveDocumentData_() {
	char temp;
	http_t* http = http_client_->getConnection();
	if (httpGetState(http) != HTTP_STATE_POST_RECV) {
		return false;
	}
	else {
		return (httpPeek(http, &temp, 1) > 0);
	}
}

static std::shared_ptr<PrintJob> create_job(ipp_t* request, VirtualDriverlessPrinter* vdp) {
	auto job = std::make_shared<PrintJob>(request, vdp);

}

