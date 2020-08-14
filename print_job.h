#pragma once
#include <cups/ipp.h>
#include <string>
//#include <process.h>
//#include "virtual_driverless_printer.h"

// is the '상호참조' resolved from the forward declaration?? -> OK
class VirtualDriverlessPrinter;

class PrintJob {
public:
	//static void initPrivateStatics();
	PrintJob(const std::string& hostname, const std::string& username, ipp_t* request, VirtualDriverlessPrinter* vdp);
	virtual ~PrintJob();
	//void* process();
	void abort();
	int createJobFile(); // create(open) a job file and return the fd
	int closeJobFile();
	int unlinkJobFile();
	ipp_jstate_t getState() const { return state_; };
	ipp_t* getAttributes() const { return attrs_; };
	int getId() const { return id_; };
	int getFd() const { return fd_; };
	std::string getFilepath() const { return filepath_; };
	//std::wstring getFilepathW() const { return filepath_wstr_; };
	std::string getHostname() const { return hostname_; };
	std::string getUsername() const { return username_; };

	void setState(ipp_jstate_t state);
	//void setFd(int fd);
	//void setFilepath(const std::string& filepath);
	//void setUsername(const std::string& username);
	void setProcessingTime(time_t time);
	void setCompletedTime(time_t time);

private:
	const int id_ = -1;
	const int fd_ = -1;
	VirtualDriverlessPrinter* vdp_ = nullptr; // owner printer
	ipp_t* attrs_ = ippNew(); // job attributes
	ipp_jstate_t state_ = IPP_JSTATE_HELD; // job state
	const std::string uri_; // job uri
	const std::string name_ = ""; // job name
	const std::string filepath_ = "";
	//const std::wstring filepath_wstr_ = L"";
	const std::string hostname_ = ""; // requesting hostname
	const std::string username_ = ""; // requesting username
	const std::string format_ = "application/pdf";
	const int impressions_ = -1;
	time_t created_time_;
	time_t processing_time_;
	time_t completed_time_;
};