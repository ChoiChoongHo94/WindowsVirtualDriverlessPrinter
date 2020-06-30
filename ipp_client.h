#pragma once
#include "virtual_driverless_printer.h"
#include "my_definitions.h"
#include "logger.h"
#include <memory>

class IPPClient;

class HTTPClient {
public:
	HTTPClient(int sock_fd);
	virtual ~HTTPClient();
	bool process(ipp_t*& ipp_request); // O : IPP message (from HTTP message)
	bool respond(http_status_t status, std::string content_encoding
		, std::string mime_type, size_t length, ipp_t* ipp_response);
	http_t* getConnection() const { return http_; };
	const std::string getHostname() const { return hostname_; };
	const std::string getOperationStr() const { return httpStateString(operation_); };
	const std::string getResourceStr() const { return resource_; };
	const int getFinalStatusCode() const { return static_cast<int>(final_status_); };
	const int getContentLength() const { return httpGetLength2(http_); };
private:
	http_t* http_ = nullptr;
	const clock_t start_ = -1;
	const http_state_t operation_ = HTTP_STATE_WAITING;
	//const http_addr_t addr_ = -1;
	const std::string resource_; // the resource requested from a client
	const std::string hostname_; // the hostname of a client requesting
	const http_status_t final_status_ = HTTP_STATUS_NONE;
	//std::string hostport_;
};

class IPPClient {
public:
	IPPClient(VirtualDriverlessPrinter* vdp, int sock_fd);
	virtual ~IPPClient();// = default;
	bool process();
	template <typename... Args>
	void respond(ipp_status_t status, const std::string& message_format, Args... args) {
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
			formatted = ippGetString(attr, 0, NULL); //TODO: error log용으로 고려
		}
		const_cast<ipp_status_t&>(final_status_) = status;
		if (!formatted.empty()) {
			CONSOLE_LOGGER->writeLog(stampHostAndUser() + formatted);
			ERROR_LOGGER->writeLog(stampHostAndUser() + formatted);
		}
	};
	void respondUnsupported(ipp_attribute_t* attr); // FIXME: naming?
	//ipp_t* getResponse() const { return response_; };
	std::string stampHostAndUser() const;

private:
	std::shared_ptr<HTTPClient> http_client_ = nullptr;
	ipp_t* request_ = nullptr;
	ipp_t* response_ = nullptr;
	const int ipp_request_id_ = -1;
	const ipp_op_t ipp_operation_ = IPP_OP_CUPS_NONE;
	const ipp_status_t final_status_ = IPP_STATUS_CUPS_INVALID;
	const std::string username_ = ""; // requesting user
	VirtualDriverlessPrinter* const vdp_;
	//std::shared_ptr<PrintJob> job_ = nullptr;

	void ippGetPrinterAttributes_();
	void ippPrintJob_();
	void ippCreateJob_();
	void ippSendDocument_();
	void ippGetJobs_();
	void ippGetJobAttributes_();
	
	bool finishDocumentData_(std::shared_ptr<PrintJob> job);
	bool validJobAttributes_();
	bool validDocAttributes_();
	bool haveDocumentData_();
};