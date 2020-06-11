#pragma once
#include "virtual_driverless_printer.h"
#include "my_definitions.h"
#include "logger.h"
#include <cups/http.h>
#include <cups/ipp.h>
#include <memory>

class HTTPClient;

class IPPClient {
public:
	IPPClient(VirtualDriverlessPrinter* vdp, int sock_fd);
	virtual ~IPPClient();// = default;
	bool process();
	template <typename... Args>
	void respond(ipp_status_t status, const std::string& message_format, Args... args);
	void respondUnsupported(ipp_attribute_t* attr); // FIXME: naming?
	//ipp_t* getResponse() const { return response_; };

private:
	std::shared_ptr<HTTPClient> http_client_ = nullptr;
	ipp_t* request_ = nullptr;
	ipp_t* response_ = nullptr;
	int ipp_request_id_;
	ipp_op_t ipp_operation_;
	//std::string uri_;
	//std::string hostname_;
	VirtualDriverlessPrinter* vdp_;
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

class HTTPClient {
public:
	HTTPClient(int sock_fd);
	virtual ~HTTPClient();
	bool process(ipp_t*& ipp_request);
	bool respond(http_status_t status, std::string content_encoding
		, std::string mime_type, size_t length, ipp_t* ipp_response);
	http_t* getConnection() const { return http_; };
	std::string getHostname() const { return hostname_; };
private:
	time_t start_;
	http_t* http_ = nullptr;
	http_state_t operation_ = HTTP_STATE_WAITING;
	http_addr_t addr_;
	std::string resource_; // the resource requested from a client
	std::string hostname_; // the hostname of a client requesting
	//std::string hostport_;
};