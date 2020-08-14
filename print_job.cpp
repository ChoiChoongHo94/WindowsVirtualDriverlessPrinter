#include "print_job.h"
#include "virtual_driverless_printer.h"
#include "my_util.h"
#include "my_definitions.h"
#include <cups/config.h>
#include <cups/cups.h>
#include <cups/ipp.h>
#include <cups/http.h>
#include <iostream>
#include <fcntl.h>
#include <process.h>
#include <sstream>

// in 'main.cpp'
HANDLE MUTEX_NEXT_JOB_ID;
int NEXT_JOB_ID;

PrintJob::PrintJob(const std::string& hostname, const std::string& username, ipp_t* request, VirtualDriverlessPrinter* vdp)
	: hostname_(hostname), username_(username), vdp_(vdp) {
	std::stringstream errlog_ss;
	errlog_ss << "[" + hostname_ + ":" + username_ + "] " << __FUNCTION__ << '\n';

	setState(IPP_JSTATE_PENDING);
	ipp_attribute_t* attr = nullptr;

	// TODO: rw lock ... (좀더 아래에 해도 될듯, 검토하기)

	Util::copy_attributes(attrs_, request, NULL, IPP_TAG_ZERO, 0);

	/* 2020-06-17
	ippAddString 을 썼을때 소멸자 ippDelete(attrs_)에서 계속 죽음(memory leak).\
	일단 ippAddString안쓰도록 VirtualDriverlessPrinter::printFile에 이 로직 추가함.
	*/
	//{/* convert all of legacy attrs to ipp attrs */
	//	if ((attr = ippFindAttribute(attrs_, "InputSlot", IPP_TAG_ZERO)) != NULL) {
	//		ippFindAttribute(attrs_, "printer-input-tray", IPP_TAG_ZERO);
	//		ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_STRING, "printer-input-tray", NULL, ippGetString(attr, 0, NULL));
	//		//ippDeleteAttribute(attrs_, attr);
	//	}
	//	if ((attr = ippFindAttribute(attrs_, "ColorModel", IPP_TAG_ZERO)) != NULL
	//		// || (attr = ippFindAttribute(attrs_, "SelectColor", IPP_TAG_ZERO)) != NULL
	//		) {
	//		std::string color_value = ippGetString(attr, 0, NULL);
	//		std::transform( std::begin(color_value), std::end(color_value), std::begin(color_value),
	//			[](char ch) {return std::tolower(ch); });
	//		if (color_value == "rgb") {
	//			color_value = "color";
	//		}
	//		else if (color_value == "gray" || color_value == "grayscale") {
	//			color_value = "monochrome";
	//		}
	//		ippFindAttribute(attrs_, "print-color-mode", IPP_TAG_ZERO);
	//		ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, color_value.c_str());
	//		//ippDeleteAttribute(attrs_, attr);
	//	}
	//}

	ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_NAME, "job-originating-user-name", NULL, username_.c_str());

	if (ippGetOperation(request) != IPP_OP_CREATE_JOB) {
		/* just test print */
		if ((attr = ippFindAttribute(attrs_, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL) {
			errlog_ss << "document-format-detected: " << ippGetString(attr, 0, NULL) << '\n';
		}
		else if ((attr = ippFindAttribute(attrs_, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL) {
			errlog_ss << "document-format-supplied: " << ippGetString(attr, 0, NULL) << '\n';
		}
	}

	if ((attr = ippFindAttribute(request, "job-impressions", IPP_TAG_INTEGER)) != NULL) {
		const_cast<int&>(impressions_) = ippGetInteger(attr, 0);
	}

	if ((attr = ippFindAttribute(request, "job-name", IPP_TAG_NAME)) != NULL) {
		const_cast<std::string&>(name_) = ippGetString(attr, 0, NULL);
	}

	//WaitForSingleObject(MUTEX_NEXT_JOB_ID, INFINITE);
	const_cast<int&>(id_) = ++NEXT_JOB_ID;
	//ReleaseMutex(MUTEX_NEXT_JOB_ID);
	//std::cerr << "job-id: " << id_ << '\n';

	char job_uri_buf[1024];
	if ((attr = ippFindAttribute(request, "printer-uri", IPP_TAG_URI)) != NULL) {
		snprintf(job_uri_buf, sizeof(job_uri_buf), "%s/%d", ippGetString(attr, 0, NULL), id_);
	}
	else {
		httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri_buf, sizeof(job_uri_buf), "ipp", NULL,
			vdp_->getHostname().c_str(), vdp_->getPort(), "/ipp/print/%d", id_);
	}
	const_cast<std::string&>(uri_) = job_uri_buf;
	errlog_ss << "job-uri:" << uri_ << '\n';

	char uuid[64];
	httpAssembleUUID(vdp_->getHostname().c_str(), vdp_->getPort(), vdp_->getName().c_str(), id_, uuid, sizeof(uuid));

	created_time_ = time(NULL);
	ippAddDate(attrs_, IPP_TAG_JOB, "date-time-at-creation", ippTimeToDate(created_time_));
	ippAddInteger(attrs_, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", id_);
	ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL, uri_.c_str());
	ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_URI, "job-uuid", NULL, uuid);
	if ((attr = ippFindAttribute(request, "printer-uri", IPP_TAG_URI)) != NULL) {
		ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, ippGetString(attr, 0, NULL));
	}
	else {
		char printer_uri[1024];
		httpAssembleURI(HTTP_URI_CODING_ALL, printer_uri, sizeof(printer_uri), "ipp", NULL,
			vdp_->getHostname().c_str(), vdp_->getPort(), "/ipp/print");
		ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, printer_uri);
	}
	if ((attr = ippFindAttribute(request, "document-name", IPP_TAG_NAME)) != NULL) {
		ippAddString(attrs_, IPP_TAG_DOCUMENT, IPP_TAG_NAME, "document-name", NULL, ippGetString(attr, 0, NULL));
	}

	ippAddInteger(attrs_, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-creation", (int)(created_time_ - vdp_->getStartTime()));
	//vdp->addJob(id_, this);

	/*
	cupsArrayAdd(client->printer->jobs, job);
	client->printer->active_job = job;
	*/

	//TODO: rw unlock
	ERROR_LOGGER->writeLog(errlog_ss.str());
}

PrintJob::~PrintJob() {
	ippDelete(attrs_);
}

// TODO: delete
//void* PrintJob::process() {
//	setProcessingTime(time(NULL));
//	setState(IPP_JSTATE_PROCESSING);
//	vdp_->setState(IPP_PSTATE_PROCESSING);
//
//	//TODO: run Print program using '_spawnvpe' function
//
//	/*
//	* Sleep for a random amount of time to simulate job processing.
//	*/
//	//sleep((unsigned)(5 + (rand() % 11)));
//
//	//TODO: handle Cancel-job
//	setState(IPP_JSTATE_COMPLETED);
//	vdp_->setState(IPP_PSTATE_IDLE);
//	//TODO: current active job
//
//	return NULL;
//}

void PrintJob::abort() {
	setState(IPP_JSTATE_ABORTED);
	setCompletedTime(time(NULL));
}

int PrintJob::createJobFile() {
	std::string job_name = (std::string)ippGetString(ippFindAttribute(attrs_, "job-name", IPP_TAG_NAME), 0, NULL);
	
	if (job_name.empty()) {
		job_name = "untitled";
	}

	const_cast<std::string&>(filepath_) = vdp_->getSpoolDir() + "/" + std::to_string(id_) + "-" + Util::hash_str(job_name);
	//std::wstring filepath_wstr = Util::utf8_to_wstr(filepath_);
	
	//return (const_cast<int&>(fd_) = _wopen(filepath_wstr.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | _O_U8TEXT, 0666));
	return (const_cast<int&>(fd_) = open(filepath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | _O_U8TEXT, 0666));
}

int PrintJob::closeJobFile() {
	int ret = close(fd_); // Success: 0, Fail: -1
	//fd_ = -1;
	return ret;
}

int PrintJob::unlinkJobFile() {
	return unlink(filepath_.c_str()); // Success: 0, Fail: -1
}

void PrintJob::setState(ipp_jstate_t state) {
	state_ = state;
	//ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", NULL, state);
	ERROR_LOGGER->writeLog("Job(job-id: " + std::to_string(id_) + ")'s jstate is set to '" + ippEnumString("job-state", (int)state) + "'\n");
}

/*
void PrintJob::setFd(int fd) {
	fd_ = fd;
}
*/

void PrintJob::setProcessingTime(time_t time) {
	processing_time_ = time;
}

void PrintJob::setCompletedTime(time_t time) {
	completed_time_ = time;
}