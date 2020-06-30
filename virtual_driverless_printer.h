#pragma once

#include <cups/ipp.h>
#include <cups/cups.h>
#include <dns_sd.h>
#include <string>
#include <queue>
#include <unordered_map>
#include <memory>
//#include <thread>
//#include <mutex>
#include "print_job.h"
#include "thread_pool.h"

#define poll WSAPoll
typedef ULONG nfds_t;

extern const std::string SPOOL_DIR;

class VirtualDriverlessPrinter {
public:
	/* TODO
	static const int kMaxJobs = 500;
	static const int kMaxThreads = 5;
	*/
	
	VirtualDriverlessPrinter(/*const std::string& name,*/ const int port);
	virtual ~VirtualDriverlessPrinter();
	void run();
	bool printFile(const std::shared_ptr<PrintJob>& job);

	std::string getName() const { return name_; };
	std::string getHostname() const { return hostname_; };
	std::string getURI() const { return uri_; };
	std::string getSpoolDir() const { return spool_dir_; };
	int getPort() const { return port_; };
	time_t getStartTime() const { return start_time_; };
	ipp_pstate_t getState() const { return state_; };
	ipp_t* getAttributes() const { return attrs_; };

	//void queueJob(const std::shared_ptr<PrintJob>& job);
	//std::shared_ptr<PrintJob> dequeueJob();
	bool addJob(int job_id, std::shared_ptr<PrintJob> job);
	bool removeJob(int job_id);
	std::shared_ptr<PrintJob> getJob(int job_id) const;
	const std::unordered_map<int, std::shared_ptr<PrintJob> >& getJobs() const { return std::ref(jobs_); };

	void setState(ipp_pstate_t state) { state_ = state; };

private:
	const time_t start_time_;
	const std::string name_;
	wchar_t windows_printer_name_[1024];
	const std::string hostname_;
	const std::string uuid_;
	const std::string uri_;
	//const std::string device_uri_;
	const std::string adminurl_;
	const std::string spool_dir_ = SPOOL_DIR;
	const cups_ptype_t printer_type_ = 0;
	const int port_ = -1;
	const int ipv4_ = -1; // ipv4 socket
	const int ipv6_ = -1; // ipv6 socket
	const DNSServiceRef bonjour_service_ = nullptr;
	ipp_pstate_t state_ = IPP_PSTATE_IDLE;
	ipp_t* attrs_ = ippNew();
	std::unordered_map<std::string, WORD> media_size_db_;
	
	//PrintJob* active_job_;
	ThreadPool ipp_client_thread_pool_;

	HANDLE mutex_jobs_ = nullptr;
	std::unordered_map<int, std::shared_ptr<PrintJob> > jobs_;
	//std::queue<std::shared_ptr<PrintJob>& > job_queue_;
	
	void initBonjourService_();
	//static void CALLBACK ipp_client_routine_(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK);
};