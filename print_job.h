#pragma once
#include <cups/ipp.h>
#include <string>
//#include "virtual_driverless_printer.h"

// is the '상호참조' resolved from the forward declaration?? -> OK
class VirtualDriverlessPrinter;

class PrintJob {
public:
	PrintJob(ipp_t* request, VirtualDriverlessPrinter* vdp);
	virtual ~PrintJob();
	void* process();
	int createJobFile(); // create the job file and return the fd
	int closeJobFile();
	int unlinkJobFile();
	ipp_jstate_t getState() { return state_; };
	ipp_t* getAttributes() { return attrs_; };
	int getFd() { return fd_; };
	std::string getFilename() { return filename_; };
	std::string getUsername() { return username_; };

	void setState(ipp_jstate_t state);
	//void setFd(int fd);
	void setProcessingTime(time_t time);
	void setCompletedTime(time_t time);

private:
	int id_ = -1;
	int fd_ = -1;
	VirtualDriverlessPrinter* vdp_; // owner printer
	ipp_t* attrs_ = ippNew(); // job attributes
	ipp_jstate_t state_ = IPP_JSTATE_HELD; // job state
	std::string uri_; // job uri
	std::string name_; // job name
	std::string filename_;
	std::string username_;
	std::string format_ = "application/pdf";
	std::string impressions_;
	time_t created_time_;
	time_t processing_time_;
	time_t completed_time_;
};