#include <Windows.h>
#include <iostream>
#include <sstream>
#include <cups/ipp.h>
#include <cups/config.h>
#include <cstring>
#include <cassert>
#include "ipp_client.h"
#include "print_job.h"
#include "my_definitions.h"
#include "my_util.h"

static std::unique_ptr<FileLogger> IPP_ACCESS_LOGGER = nullptr;
void INIT_IPP_ACCESS_LOGGER(const std::string& filename) {
	std::string total_path = Util::get_userhome_dir() + "\\" + filename;
	IPP_ACCESS_LOGGER = std::make_unique<FileLogger>(total_path);
	CONSOLE_LOGGER->writeLog(std::string("ipp access log path: ") + total_path);
}

//#define LOG_IPP_ACCESS IPP_ACCESS_LOGGER->writeLog;
//#define LOG_IPP_ACCESS_F IPP_ACCESS_LOGGER->writefLog;

namespace TestPrint {
	void printCupsArray(cups_array_t* ca) {
		std::string buffer = "Requested Array(CUPS Array): ";
		for (auto element = cupsArrayFirst(ca); element; element = cupsArrayNext(ca)) {
			buffer += std::string((char*)element) + ", ";
		}
		buffer.pop_back();
		buffer.pop_back();
		CONSOLE_LOGGER->writeLog(buffer);
	}

	void printIPPAttrs(ipp_t* ipp_msg) {
		std::string buffer = "IPP Attributes: ";
		auto attr = ippFirstAttribute(ipp_msg);
		ipp_tag_t curr_group_tag = ippGetGroupTag(attr);
		buffer += std::string(ippTagString(curr_group_tag)) + "(" + Util::get_attr_stamp(attr)+ ", ";
		for (attr = ippNextAttribute(ipp_msg); attr != nullptr; attr = ippNextAttribute(ipp_msg)) {
			if (curr_group_tag != ippGetGroupTag(attr)) {
				curr_group_tag = ippGetGroupTag(attr);
				buffer.pop_back();
				buffer.pop_back();
				buffer += std::string("), ") + ippTagString(curr_group_tag) + "(";
			}
			buffer += Util::get_attr_stamp(attr) + ", ";
		}
		buffer.pop_back();
		buffer.pop_back();
		buffer += ")";
		CONSOLE_LOGGER->writeLog(buffer);
	}
}

HTTPClient::HTTPClient(int sock_fd) {
	std::stringstream log_ss;
	char hostname[1024];
	if ((http_ = httpAcceptConnection(sock_fd, 1)) != nullptr) {
		httpGetHostname(http_, hostname, sizeof(hostname));
		const_cast<std::string&>(hostname_) = hostname;
		const_cast<clock_t&>(start_) = clock();
		log_ss << Util::get_timestamp() << " [" << hostname_ << "] accept HTTP connection.";
	}
	else {
		log_ss << Util::get_timestamp() << " httpAcceptConnection failed.";
	}
	CONSOLE_LOGGER->writeLog(log_ss.str());
	//CONSOLE_LOGGER->writefLog("%s [%s] %s start.", Util::get_timestamp(), hostname_, httpStateString(httpGetState(http_)));
}

IPPClient::IPPClient(VirtualDriverlessPrinter* vdp, int sock_fd) : vdp_(vdp) {
	http_client_ = std::make_shared<HTTPClient>(sock_fd);
}

HTTPClient::~HTTPClient() {
	std::stringstream log_ss;
	//httpFlushWrite(http_);
	//httpClose(http_);
	log_ss << Util::get_timestamp() << " [" << hostname_ << "] close HTTP connection with status'" << httpStatus(final_status_) << "'. (" << std::to_string(clock() - start_) << "ms)\n";
	CONSOLE_LOGGER->writeLog(log_ss.str());
	//CONSOLE_LOGGER->writefLog("%s [%s] %s end. (%s)", Util::get_timestamp(), hostname_, httpStateString(httpGetState(http_)), std::to_string(clock() - start_));
}

IPPClient::~IPPClient() {
	//if (request_ != nullptr) ippDelete(request_);
	//if (response_ != nullptr) ippDelete(response_);
}

bool HTTPClient::process(ipp_t*& ipp_request) {
	// TMP TEST
	//if (hostname_ != "192.168.8.118") {
	//	std::cerr << "@@ not allowed client @@" << '\n';
	//	assert(respond(HTTP_STATUS_NOT_IMPLEMENTED, "", "", 0, nullptr));
	//	return false;
	//}
	//CONSOLE_LOGGER->writeLog("my pc is entranced!");

	char uri[1024];
	char scheme[32];
	char userpass[128];
	char hostname[HTTP_MAX_HOST];
	int port = -1;
	char resource[HTTP_MAX_HOST];
	http_state_t state;
	http_status_t status;
	bool ret = true;

	while ((state = httpReadRequest(http_, uri, sizeof(uri))) == HTTP_STATE_WAITING)
		Sleep(1);
	 
	if (state == HTTP_STATE_ERROR) {
		if (httpError(http_) == EPIPE) { // error case of being set to ignoring any signals when the socket or pipe is disconnected
			//respond(HTTP_STATUS_BAD_REQUEST, "", "", 0, nullptr);
			CONSOLE_LOGGER->writeLog(std::string("'") + hostname_ + "' client closed connection!");
		}
		else {
			//respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
			CONSOLE_LOGGER->writeLog(std::string("'") + hostname_ + "' bad request line!");
		}
		ret = false; goto END;
	}
	else if (state == HTTP_STATE_UNKNOWN_METHOD) {
		CONSOLE_LOGGER->writeLog(std::string("'") + hostname_ + "' bad/unknown operation!");
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto END;
	}
	else if (state == HTTP_STATE_UNKNOWN_VERSION) {
		CONSOLE_LOGGER->writeLog(std::string("'") + hostname_ + "' bad HTTP version!");
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto END;
	}

	if (httpSeparateURI(HTTP_URI_CODING_MOST, uri, scheme, sizeof(scheme),
		userpass, sizeof(userpass),
		hostname, sizeof(hostname), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK &&
		(state != HTTP_STATE_OPTIONS || strcmp(uri, "*"))) {
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto END;
	}

	/* process the request */
	// ippsample/ippeveprinter.c:5940
	const_cast<std::string&>(resource_) = resource;
	const_cast<http_state_t&>(operation_) = httpGetState(const_cast<http_t*>(http_));
	while ((status = httpUpdate(http_)) == HTTP_STATUS_CONTINUE);

	if (status != HTTP_STATUS_OK) {
		respond(HTTP_STATUS_BAD_REQUEST, "", "", 0, nullptr);
		ret = false; goto END;
	}

	/* validate the header of the HTTP request */
	if (!httpGetField(http_, HTTP_FIELD_HOST)[0] &&
		httpGetVersion(http_) >= HTTP_VERSION_1_1) {
		//std::cerr << "Missing 'Host:' field in the HTTP/1.1 and higher version request!" << '\n';
		respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr);
		ret = false; goto END;
	}

	if (std::string{ "Upgrade" } == httpGetField(http_, HTTP_FIELD_CONNECTION)) {
		if (!respond(HTTP_STATUS_NOT_IMPLEMENTED, NULL, NULL, 0, nullptr)) {
			ret = false; goto END;
		}
	}

	// ippsample/ippeveprinter.c:process_ipp()
	status = httpGetExpect(http_);
	if (status &&
		(operation_ == HTTP_STATE_POST || operation_ == HTTP_STATE_PUT)) {
		if (status == HTTP_STATUS_CONTINUE) {
			if (!respond(HTTP_STATUS_CONTINUE, "", "", 0, nullptr)) {
				ret = false; goto END;
			}
		}
		else {
			if (!respond(HTTP_STATUS_EXPECTATION_FAILED, NULL, NULL, 0, nullptr)) {
				ret = false; goto END;
			}
		}
	}
	
	//std::string encoding = httpGetContentEncoding(http_);
	switch (operation_) {
	case HTTP_STATE_POST:
		if (std::string{ "application/ipp" } != httpGetField(http_, HTTP_FIELD_CONTENT_TYPE)) {
			ret = respond(HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0, nullptr); goto END;
		}

		/* IPP request */
		ipp_request = ippNew();
		for (ipp_state_t ipp_state = ippRead(http_, ipp_request);
			ipp_state != IPP_STATE_DATA;
			ipp_state = ippRead(http_, ipp_request)) {
			if (ipp_state == IPP_STATE_ERROR) {
				respond(HTTP_STATUS_BAD_REQUEST, "", "", 0, nullptr);
			}
		}
		//TestPrint::printIPPAttrs(ipp_request);
		break;

	case HTTP_STATE_OPTIONS:
	case HTTP_STATE_HEAD:
	case HTTP_STATE_GET:
		ret = respond(HTTP_STATUS_NOT_FOUND, NULL, NULL, 0, nullptr);
		break;

	default:
		ret = false;
		break;
	}

END:
	return ret;
}

bool HTTPClient::respond(http_status_t status, std::string content_encoding,
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

	/* set HTTP response header */
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

	bool ret = true;
	if (httpWriteResponse(http_, status) < 0) {
		ret = false;
	}

	/* send the response */
	if (ret && message[0]) {
		if (httpPrintf(http_, "%s", message) < 0) {
			ret = false;
		}
		if (httpWrite2(http_, "", 0) < 0) {
			ret = false;
		}
	} else if (ret && ipp_response) {
		ippSetState(ipp_response, IPP_STATE_IDLE);
		ipp_state_t state = ippWrite(http_, ipp_response);
		if (state != IPP_STATE_DATA) {
			ret = false;
		}
	}

	const_cast<http_status_t&>(final_status_) = status;
	return ret;
}

bool IPPClient::process() {
	bool http_respond_ret = true;
	http_t* http = http_client_->getConnection();
	while (http_respond_ret && httpWait(http, 100)) {
		bool http_process_ret = http_client_->process(request_);
		if (http_process_ret && request_ == nullptr) {
			//std::cerr << "The HTTP request is processed. (IPP request 'request_' is NULL)" << '\n';
			return true;
		}
		else if (!http_process_ret) {
			//std::cerr << "The HTTP Process is failed!" << '\n';
			return false;
		}

		//std::cerr << "IPP Operation: " << ippOpString(ippGetOperation(request_)) << ", ";
		response_ = ippNewResponse(request_);
		int major, minor;
		major = ippGetVersion(request_, &minor);
		if (!((major == 2 && minor == 0) || (major == 1 && minor == 1))) {
			respond(IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED, "Bad request version numver %d.%d!", major, minor);
		}
		else if ((const_cast<int&>(ipp_request_id_) = ippGetRequestId(request_)) <= 0) {
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

			if ((attr = ippFindAttribute(request_, "requesting-user-name", IPP_TAG_NAME)) != NULL) {
				const_cast<std::string&>(username_) = std::string{ ippGetString(attr, 0, NULL) };
			}
			else {
				const_cast<std::string&>(username_) = "anonymous";
			}

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

					// TODO: HTTP에서 중복된거 없는지
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
						/* processing the operation */
						const_cast<ipp_op_t&>(ipp_operation_) = ippGetOperation(request_);
						CONSOLE_LOGGER->writeLog(Util::get_timestamp() + " " + stampHostAndUser() + "incoming IPP OP: " + ippOpString(ipp_operation_));
						switch (ipp_operation_) {
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
						case IPP_OP_CREATE_JOB:
							break;

						case IPP_OP_SEND_DOCUMENT:
							break;
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

		{/* log to the ipp access log file */
			http_version_t http_version = httpGetVersion(http_client_->getConnection());
			const std::string http_operation = http_client_->getOperationStr();
			ipp_attribute_t* requesting_username_attr = ippFindAttribute(request_, "requesting-user-name", IPP_TAG_NAME);
			char access_log_buffer[4096];
			//const std::string hostname = http_client_->getHostname();
			//const std::string timestamp = Util::get_timestamp();
			//const std::string operation = http_client_->getOperationStr();
			//const std::string resource = http_client_->getResourceStr();
			//const int status_code = http_client_->getFinalStatusCode();
			//const int content_length = http_client_->getContentLength();
			//const std::string ipp_operation = ippOpString(ipp_operation_);
			//const std::string ipp_status_str = ippErrorString(final_status_);
			int written_size = _snprintf_s(access_log_buffer, sizeof(access_log_buffer), "%s %s - [%s] \"%s %s HTTP/%s\" %d %d %s %s",
				http_client_->getHostname().c_str(),
				requesting_username_attr != NULL ? ippGetString(requesting_username_attr, 0, NULL) : "-",
				Util::get_timestamp().c_str(),
				http_operation.substr(http_operation.rfind('_') + 1).c_str(), http_client_->getResourceStr().c_str(),
				http_version == HTTP_VERSION_1_1 ? "1.1" : (http_version == HTTP_VERSION_1_0 ? "1.1" : "0.9"),
				http_client_->getFinalStatusCode(), http_client_->getContentLength(),
				ippOpString(ipp_operation_), ippErrorString(final_status_));
			assert(written_size > 0 && written_size < sizeof(access_log_buffer));
			IPP_ACCESS_LOGGER->writeLog(access_log_buffer);
			CONSOLE_LOGGER->writeLog(access_log_buffer);
		}
		CONSOLE_LOGGER->writeLog(std::string("IPP state: ") + ippStateString(ippGetState(request_)) + ", HTTP state: " + httpStateString(httpGetState(http_client_->getConnection())));
	} // 'while(httpWait(..))' loop end
	httpFlush(http);
	httpClose(http);
	if (request_ != nullptr) ippDelete(request_);
	if (response_ != nullptr) ippDelete(response_);
	return http_respond_ret;
}

void IPPClient::respondUnsupported(ipp_attribute_t* attr) {
	respond(IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Unsupported %s %s%s value!", ippGetName(attr),
		(ippGetCount(attr) > 1 ? "1setOf " : ""), ippTagString(ippGetValueTag(attr)));

	ipp_attribute_t* temp = ippCopyAttribute(response_, attr, 0);
	ippSetGroupTag(response_, &temp, IPP_TAG_UNSUPPORTED_GROUP);
}

std::string IPPClient::stampHostAndUser() const {
	return "[" + http_client_->getHostname() + ":" + username_ + "] ";
}

void IPPClient::ippGetPrinterAttributes_() {
	cups_array_t* ra = ippCreateRequestedArray(request_);
	//TestPrint::printCupsArray(ra);

	respond(IPP_STATUS_OK, "");

	// TODO: It seems to need a rw lock.
	// copy attrs
	
	//TestPrint::printIPPAttrs(response_);
	Util::copy_attributes(response_, vdp_->getAttributes(), NULL, IPP_TAG_ZERO, 1);
	//TestPrint::printIPPAttrs(response_);

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

	cupsArrayDelete(ra);
	//TestPrint::printIPPAttrs(response_);
}

void IPPClient::ippPrintJob_() {
	if (!validJobAttributes_() || !validDocAttributes_()) {
		// httpflush, flush_document_data
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_BAD_REQUEST, std::string(__FUNCTION__) + ": Invalid 'Job' or 'Doc' attributes in request.");
		return;
	}

	if (!haveDocumentData_()) {
		respond(IPP_STATUS_ERROR_BAD_REQUEST, "No file in request!");
		return;
	}

	auto job = std::make_shared<PrintJob>(http_client_->getHostname(), username_, request_, vdp_);
	if (job == nullptr) {
		respond(IPP_STATUS_ERROR_BUSY, "Currently printing another job!");
		return;
	}

	job->setProcessingTime(time(NULL));
	job->setState(IPP_JSTATE_PROCESSING);
	if (finishDocumentData_(job)) {
		vdp_->addJob(job->getId(), job);
		job->setState(IPP_JSTATE_COMPLETED);
		vdp_->printFile(job);
		respond(IPP_STATUS_OK, "");
	}
	else {
		// respond(...) is already called in the finishDocumentData_()
		job->setState(IPP_JSTATE_ABORTED);
		respond(IPP_STATUS_ERROR_INTERNAL, std::string(__FUNCTION__) + ": Printing failed.");
	}
	job->setCompletedTime(time(NULL));
	
	std::vector<std::string> rv = { "job-id", "job-state", "job-state-reasons", "job-uri" };
	Util::copy_job_attributes(response_, job.get(), rv);
	vdp_->removeJob(job->getId());
	return;
};

void IPPClient::ippCreateJob_() {
	if (haveDocumentData_()) {
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_BAD_REQUEST, "Document data must not be supplied from this IPP message!");
		return;
	}

	if (!validJobAttributes_() || !validDocAttributes_()) {
		return;
	}

	auto job = std::make_shared<PrintJob>(http_client_->getHostname(), username_, request_, vdp_);
	if (!vdp_->addJob(job->getId(), job)) {
		respond(IPP_STATUS_ERROR_BUSY, "Currently the printer is busy..");
	}

	respond(IPP_STATUS_OK, "");
};

void IPPClient::ippSendDocument_() {
	/* find job */
	ipp_attribute_t* attr = ippFindAttribute(request_, "job-id", IPP_TAG_INTEGER); assert(attr != NULL);
	int job_id = ippGetInteger(attr, 0);
	auto job = vdp_->getJob(job_id);
	if (job == nullptr) {
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_NOT_FOUND, "The job does not exist!");
		return;
	}

	bool have_data = haveDocumentData_();
	if ((!job->getFilepath().empty() || job->getFd() >= 0) && have_data) {
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_MULTIPLE_JOBS_NOT_SUPPORTED, "Multiple document jobs are not supported!");
		return;
	}
	else if (job->getState() >= IPP_JSTATE_HELD && have_data) {
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_NOT_POSSIBLE, "The job is not in a pending state!");
		return;
	}
	
	if ((attr = ippFindAttribute(request_, "last-document", IPP_TAG_OPERATION)) == NULL) {
		httpFlush(http_client_->getConnection());
		respond(IPP_STATUS_ERROR_BAD_REQUEST, "Missing requeired 'last-document' attribute!");
		return;
	}
	else if (ippGetValueTag(attr) != IPP_TAG_BOOLEAN || ippGetCount(attr) != 1) {
		httpFlush(http_client_->getConnection());
		respondUnsupported(attr);
		return;
	}

	if (have_data && !validDocAttributes_()) {
		httpFlush(http_client_->getConnection());
		return;
	}
	 
	// TODO
	// vdp_->addJob, printFile, removeJob, ...
};

void IPPClient::ippGetJobs_() {
	std::stringstream errlog_ss;
	errlog_ss << "[" + http_client_->getHostname() + ":" + username_ + "] " << __FUNCTION__ << '\n';
	ipp_attribute_t* attr = nullptr;
	cups_array_t* ra = nullptr;
	int job_comparison;
	ipp_jstate_t job_state_criteria;
	std::string which_jobs;
	if ((attr = ippFindAttribute(request_, "which-jobs", IPP_TAG_KEYWORD)) != NULL) {
		which_jobs = ippGetString(attr, 0, NULL);
		//std::cerr << http_client_->getHostname() << " which-jobs=" << which_jobs << '\n';
		errlog_ss << "which-jobs=" << which_jobs << '\n';
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

	if ((attr = ippFindAttribute(request_, "limit", IPP_TAG_INTEGER)) != NULL) {
		errlog_ss << "limit=" << ippGetInteger(attr, 0) << '\n';
	}

	int first_job_id = 0;
	if ((attr = ippFindAttribute(request_, "first-job-id", IPP_TAG_INTEGER)) != NULL) {
		first_job_id = ippGetInteger(attr, 0);
		errlog_ss << "first-job-id=" <<  first_job_id << '\n';
	}

	std::string username;
	if ((attr = ippFindAttribute(request_, "my-jobs", IPP_TAG_BOOLEAN)) != NULL) {
		bool my_jobs = ippGetBoolean(attr, 0);
		errlog_ss << "my-jobs=" << std::boolalpha << my_jobs << '\n';
		if (my_jobs) {
			if ((attr = ippFindAttribute(request_, "requesting-user-name", IPP_TAG_NAME)) == NULL) {
				respond(IPP_STATUS_ERROR_BAD_REQUEST, "NeedRequesting-user-name with my-jobs!");
				return;
			}
			username = ippGetString(attr, 0, NULL);
			errlog_ss << "requesting-user-name=\"" << username << "\"" << '\n';
		}
	}

	/* build a list of jobs for this printer */
	ra = ippCreateRequestedArray(request_);
	respond(IPP_STATUS_OK, "");

	//TODO: rw lock
	int count = 0;
	for (auto element : vdp_->getJobs()) {
		int job_id = element.first;
		auto job = element.second;
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
		Util::copy_job_attributes(response_, job.get(), ra);
	}
	cupsArrayDelete(ra);
	//TODO: rw unlock

	ERROR_LOGGER->writeLog(errlog_ss.str());
}

void IPPClient::ippGetJobAttributes_() {
	std::stringstream errlog_ss;
	errlog_ss << "[" + http_client_->getHostname() + ":" + username_ + "] " << __FUNCTION__ << '\n';
	ipp_attribute_t* attr = nullptr;
	int job_id = -1;
	
	/* find job */
	if ((attr = ippFindAttribute(request_, "job-id", IPP_TAG_INTEGER)) != NULL) {
		job_id = ippGetInteger(attr, 0);
		errlog_ss << "job-id: " << job_id;
	}
	else if ((attr = ippFindAttribute(request_, "job-uri", IPP_TAG_URI)) != NULL) {
		// TODO
	}

	//TODO: rw lock
	const auto& jobs = vdp_->getJobs(); // unordered_map
	auto it = jobs.find(job_id);
	if (it == jobs.end()) {
		respond(IPP_STATUS_ERROR_NOT_FOUND, "job-id=%d Job not found!", job_id);
		return;
	}
	auto job = it->second;
	//TODO: rw unlock

	respond(IPP_STATUS_OK, "");
	cups_array_t* ra = ippCreateRequestedArray(request_);
	Util::copy_job_attributes(response_, job.get(), ra);
	cupsArrayDelete(ra);
	ERROR_LOGGER->writeLog(errlog_ss.str());
}

bool IPPClient::finishDocumentData_(std::shared_ptr<PrintJob> job) {
	std::stringstream errlog_ss;
	errlog_ss << "[" + http_client_->getHostname() + ":" + username_ + "] " << __FUNCTION__ << '\n';

	bool ret = true;
	size_t bytes = 0;
	char buf[4096];
	char err_buf[1024];
	int job_file_fd = -1;

	if ((job_file_fd = job->createJobFile()) < 0) {
		strerror_s(err_buf, sizeof(err_buf), errno);
		respond(IPP_STATUS_ERROR_INTERNAL, "Unable to create print file: %s!", err_buf);
		ret = false;
		goto END;
	}

	errlog_ss << "created job file: '" << job->getFilepath() << "'" << '\n';

	while ((bytes = httpRead2(http_client_->getConnection(), buf, sizeof(buf))) > 0) {
		if (write(job_file_fd, buf, (size_t)bytes) < bytes) {
			/* write error */
			int err = errno;
			job->closeJobFile();
			job->unlinkJobFile();
			strerror_s(err_buf, sizeof(err_buf), err);
			respond(IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s!", err_buf);
			ret = false;
			goto END;
		}
	}

	if (bytes < 0) {
		/* error while reading the print data */
		job->closeJobFile();
		job->unlinkJobFile();
		respond(IPP_STATUS_ERROR_INTERNAL, "Unable to read print file!");
		ret = false;
		goto END;
	}

	if (job->closeJobFile()) {
		int err = errno;
		job->unlinkJobFile();
		respond(IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(err));
		ret = false;
		goto END;
	}

END:
	ERROR_LOGGER->writeLog(errlog_ss.str());
	if (!ret) {
		job->abort();
	}
	return ret;
}

bool IPPClient::validJobAttributes_() {
	//std::stringstream errlog_ss;
	//errlog_ss << "[" + http_client_->getHostname() + ":" + username_ + "] " << __FUNCTION__ << '\n';
	TestPrint::printIPPAttrs(request_);
	/*
	기본적으로
	1) Attribute가 포함한 값의 개수
	2) Value Tag
	3) 값의 범위
	등을 검사

	validDocAttributes_()??

	TODO list:
	"ipp-attribute-fidelity"
	"job-hold-until" -> no-hold, indefinite (until a client performs a Release-Job), day-time, ...
	"job-impressions"
	"job-sheets" -> determines which Job start/end sheet(s).
	
	*/
	bool ret = true; // is valid?
	ipp_t* printer_attrs = vdp_->getAttributes();
	ipp_attribute_t* attr = nullptr;
	ipp_attribute_t* supported_attrs = nullptr;

	// TODO, FIXME: 윈도우에 어떻게 매핑시킬 것인지 조사 필요할 듯
	if ((attr = ippFindAttribute(request_, "multiple-document-handling", IPP_TAG_ZERO)) != NULL) {
		const std::string& str_value = std::string(ippGetString(attr, 0, NULL));
		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD ||
			(str_value != "single-document" && str_value != "separate-documents-uncollated-copies" && str_value != "separate-documents-collated-copies")) {
			respondUnsupported(attr);
			ret = false;
		}
	}

	if ((attr = ippFindAttribute(request_, "copies", IPP_TAG_ZERO)) != NULL) {
		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER ||
			ippGetInteger(attr, 0) < 1 || ippGetInteger(attr, 0) > 500) {
			respondUnsupported(attr);
			ret = false;
		}
	}

	if ((attr = ippFindAttribute(request_, "sides", IPP_TAG_ZERO)) != NULL) {
		const std::string& sides = std::string(ippGetString(attr, 0, NULL));
		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD) {
			respondUnsupported(attr);
			ret = false;
		}
		else if ((supported_attrs = ippFindAttribute(vdp_->getAttributes(), "sides-supported", IPP_TAG_KEYWORD)) == NULL ||
			(supported_attrs != NULL && !ippContainsString(supported_attrs, sides.c_str()))) {
			respondUnsupported(attr);
			ret = false;
		}
	}

	// already processed(filtered) by the 'pdftopdf' filter on CUPS(TOS)
	if ((attr = ippFindAttribute(request_, "orientation-requested", IPP_TAG_ZERO)) != NULL ||
		(attr = ippFindAttribute(request_, "page-ranges", IPP_TAG_ZERO)) != NULL) {
		respondUnsupported(attr);
		//ret = false;
	}

	if ((attr = ippFindAttribute(request_, "job-name", IPP_TAG_ZERO)) != NULL) {
		if (ippGetCount(attr) != 1 ||
			(ippGetValueTag(attr) != IPP_TAG_NAME && ippGetValueTag(attr) != IPP_TAG_NAMELANG)) {
			respondUnsupported(attr);
			ret = false;
		}
	}

	if ((attr = ippFindAttribute(request_, "media", IPP_TAG_ZERO)) != NULL) {
		if (ippGetCount(attr) != 1 ||
			(ippGetValueTag(attr) & (IPP_TAG_NAME | IPP_TAG_NAMELANG | IPP_TAG_KEYWORD)) == 0) {
			respondUnsupported(attr);
			ret = false;
		}
		else {
			supported_attrs = ippFindAttribute(printer_attrs, "media-supported", IPP_TAG_KEYWORD);
			if (!ippContainsString(supported_attrs, ippGetString(attr, 0, NULL))) {
				respondUnsupported(attr);
				ret = false;
			}
		}
	}

	if ((attr = ippFindAttribute(request_, "print-quality", IPP_TAG_ZERO)) != NULL) {
		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_ENUM ||
			ippGetInteger(attr, 0) < IPP_QUALITY_DRAFT ||
			ippGetInteger(attr, 0) > IPP_QUALITY_HIGH) {
			respondUnsupported(attr);
			ret = false;
		}
	}

	if ((attr = ippFindAttribute(request_, "printer-resolution", IPP_TAG_ZERO)) != NULL) {
		supported_attrs = ippFindAttribute(printer_attrs, "printer-resolution-supported", IPP_TAG_RESOLUTION);
		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_RESOLUTION ||
			supported_attrs == NULL) {
			respondUnsupported(attr);
			ret = false;
		}
		else {
			int xdpi;
			int ydpi;
			int supported_ydpi;
			ipp_res_t units;
			ipp_res_t supported_units;

			xdpi = ippGetResolution(attr, 0, &ydpi, &units);
			int supported_attrs_count = ippGetCount(supported_attrs);

			bool is_supported = false;
			for (int i = 0; i < supported_attrs_count; i++) {
				if (xdpi == ippGetResolution(supported_attrs, i, &supported_ydpi, &supported_units) &&
					ydpi == supported_ydpi && units == supported_units) {
					is_supported = true;
					break;
				}
			}

			if (!is_supported) {
				respondUnsupported(attr);
				ret = false;
			}
		}
	}
	return ret;
}

bool IPPClient::validDocAttributes_() {
	std::stringstream errlog_ss;
	errlog_ss << "[" + http_client_->getHostname() + ":" + username_ + "] " << __FUNCTION__ << '\n';
	bool ret = true;
	ipp_op_t op = ippGetOperation(request_);
	const std::string op_name = ippOpString(op);
	ipp_t* printer_attrs = vdp_->getAttributes();
	ipp_attribute_t* attr = nullptr;
	ipp_attribute_t* supported_attrs = nullptr;
	
	/* compression */
	if ((attr = ippFindAttribute(request_, "compression", IPP_TAG_ZERO)) != NULL) {
		std::string compression = std::string(ippGetString(attr, 0, NULL)); assert(!compression.empty());
		supported_attrs = ippFindAttribute(printer_attrs, "compression-supported", IPP_TAG_KEYWORD);

		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD ||
			ippGetGroupTag(attr) != IPP_TAG_OPERATION ||
			(op != IPP_OP_PRINT_JOB && op != IPP_OP_SEND_DOCUMENT && op != IPP_OP_VALIDATE_JOB) ||
			!ippContainsString(supported_attrs, compression.c_str())) {
			respondUnsupported(attr);
			ret = false;
		}
		else {
			errlog_ss << "compression='" << compression << "'\n";
			ippAddString(request_, IPP_TAG_JOB, IPP_TAG_KEYWORD, "compression-supplied", NULL, compression.c_str()); // 무슨 역할? -> TODO: RFC8011
			
			if (compression != "none") {
				httpSetField(http_client_->getConnection(), HTTP_FIELD_CONTENT_ENCODING, compression.c_str());
			}
		}
	}

	/* document-format */
	std::string format;
	if ((attr = ippFindAttribute(request_, "document-format", IPP_TAG_ZERO)) != NULL) {
		format = std::string(ippGetString(attr, 0, NULL)); assert(!format.empty());
		supported_attrs = ippFindAttribute(printer_attrs, "document-format-supported", IPP_TAG_MIMETYPE);
		if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_MIMETYPE ||
			ippGetGroupTag(attr) != IPP_TAG_OPERATION ||
			!ippContainsString(supported_attrs, format.c_str())) {
			respondUnsupported(attr);
			ret = false;
		}
		else {
			errlog_ss << "document-format='" << format << "'\n";
			ippAddString(request_, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-supplied", NULL, format.c_str());
		}
	}
	else {
		// TODO: PDF 인지 체크하는 과정 필요한가?
		format = std::string( ippGetString(ippFindAttribute(printer_attrs, "document-format-default", IPP_TAG_MIMETYPE), 0, NULL) ); assert(!format.empty());
		attr = ippAddString(request_, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format.c_str());
	}

	/* documnet-name */
	// 2020-06-16: Windows에 제출할 문서 이름은 job-name으로 대체
	//if ((attr = ippFindAttribute(request_, "document-name", IPP_TAG_NAME)) != NULL) {
	//	ippAddString(request_, IPP_TAG_JOB, IPP_TAG_NAME, "document-name-supplied", NULL, ippGetString(attr, 0, NULL));
	//}

	ERROR_LOGGER->writeLog(errlog_ss.str());
	return ret;
};

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