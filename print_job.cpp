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

// in 'main.cpp'
extern CRITICAL_SECTION JOB_ID_CS;
extern int NEXT_JOB_ID;

PrintJob::PrintJob(ipp_t* request, VirtualDriverlessPrinter* vdp) {
	vdp_ = vdp;

	ipp_attribute_t* attr = nullptr;

	// TODO: rw lock ...
	Util::copy_attributes(attrs_, request, NULL, IPP_TAG_JOB, 0);
	if ((attr = ippFindAttribute(request, "requesting-user-name", IPP_TAG_NAME)) != NULL) {
		username_ = std::string{ ippGetString(attr, 0, NULL) };
	}
	else {
		username_ = "anonymous";
	}

	ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_NAME, "job-originating-user-name", NULL, username_.c_str());

	if (ippGetOperation(request) != IPP_OP_CREATE_JOB) {
		/* just test print */
		if ((attr = ippFindAttribute(attrs_, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL) {
			std::cerr << "document-format-detected: " << ippGetString(attr, 0, NULL) << '\n';
		}
		else if ((attr = ippFindAttribute(attrs_, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL) {
			std::cerr << "document-format-supplied: " << ippGetString(attr, 0, NULL) << '\n';
		}
	}

	if ((attr = ippFindAttribute(request, "job-impressions", IPP_TAG_INTEGER)) != NULL) {
		impressions_ = ippGetInteger(attr, 0);
	}

	if ((attr = ippFindAttribute(request, "job-name", IPP_TAG_NAME)) != NULL) {
		name_ = ippGetString(attr, 0, NULL);
	}

	EnterCriticalSection(&JOB_ID_CS);
	id_ = ++NEXT_JOB_ID;
	LeaveCriticalSection(&JOB_ID_CS);
	std::cerr << "job-id: " << id_ << '\n';

	char job_uri_buf[1024];
	if ((attr = ippFindAttribute(request, "printer-uri", IPP_TAG_URI)) != NULL) {
		snprintf(job_uri_buf, sizeof(job_uri_buf), "%s/%d", ippGetString(attr, 0, NULL), id_);
	}
	else {
		httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri_buf, sizeof(job_uri_buf), "ipp", NULL,
			vdp_->getHostname().c_str(), vdp_->getPort(), "/ipp/print/%d", id_);
	}
	uri_ = job_uri_buf;
	std::cerr << "job-uri:" << uri_ << '\n';

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

	ippAddInteger(attrs_, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-creation", (int)(created_time_ - vdp_->getStartTime()));

	//vdp->addJob(id_, this);

	/*
	cupsArrayAdd(client->printer->jobs, job);
	client->printer->active_job = job;
	*/

	//TODO: rw unlock
}

PrintJob::~PrintJob() {
	ippDelete(attrs_);
}

void* PrintJob::process() {
	setProcessingTime(time(NULL));
	setState(IPP_JSTATE_PROCESSING);
	vdp_->setState(IPP_PSTATE_PROCESSING);

	//TODO: run Print program using '_spawnvpe' function

	/*
	* Sleep for a random amount of time to simulate job processing.
	*/
	sleep((unsigned)(5 + (rand() % 11)));

	//TODO: handle Cancel-job
	setState(IPP_JSTATE_COMPLETED);
	vdp_->setState(IPP_PSTATE_IDLE);
	//TODO: current active job

	return NULL;
}

void PrintJob::abort() {
	setState(IPP_JSTATE_ABORTED);
	setCompletedTime(time(NULL));
}

int PrintJob::createJobFile() {
	const char* tmp_job_name = ippGetString(ippFindAttribute(attrs_, "job-name", IPP_TAG_NAME), 0, NULL);
	std::string job_name;
	if (!tmp_job_name) {
		job_name = "untitled";
	}
	else {
		job_name = tmp_job_name;
	}
	filepath_ = vdp_->getSpoolDir() + "/" + std::to_string(id_) + "-" + job_name;

	// ippsample issue: https://github.com/istopwg/ippsample/issues/181
	return (fd_ = open(filepath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
}

int PrintJob::closeJobFile() {
	int ret = close(fd_); // Success: 0, Fail: -1
	fd_ = -1;
	return ret;
}

int PrintJob::unlinkJobFile() {
	return unlink(filepath_.c_str()); // Success: 0, Fail: -1
}

void PrintJob::setState(ipp_jstate_t state) {
	state_ = state;
	//ippAddString(attrs_, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", NULL, state);
	std::cerr << "Job " << id_ << ", " << "jstate is set to '" << state << "'" << '\n';
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