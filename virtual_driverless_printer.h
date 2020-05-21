#pragma once
#pragma comment(lib, "ws2_32.lib")
#include <string>
#include <cups/ipp.h>
#include <dns_sd.h>
#include <unordered_map>
#include "print_job.h"

#define poll WSAPoll
typedef ULONG nfds_t;

class VirtualDriverlessPrinter {
public:
	VirtualDriverlessPrinter(const std::string& name, const int port);
	virtual ~VirtualDriverlessPrinter();
	void run();

	// TODO: synchronized? 
	void addJob(int job_id, PrintJob* job);

	std::string getName() { return name_; };
	std::string getHostname() { return hostname_; };
	std::string getURI() { return uri_; };
	std::string getSpoolDir() { return spool_dir_; };
	int getPort() { return port_; };
	time_t getStartTime() { return start_time_; };
	ipp_pstate_t getState() { return state_; };
	ipp_t* getAttributes() { return attrs_; };
	std::unordered_map<int, PrintJob*>& getJobs() { return jobs_map_; };

	void setState(ipp_pstate_t state) { state_ = state; };

private:
	std::string name_;
	std::string hostname_;
	std::string uuid_;
	std::string uri_;
	std::string device_uri_;
	std::string adminurl_;
	std::string spool_dir_ = "C:/Temp";
	int port_ = -1;
	int ipv4_ = -1; // ipv4 socket
	int ipv6_ = -1; // ipv6 socket
	time_t start_time_;
	//DNSServiceRef bonjour_service_; // bonjour service socket
	ipp_pstate_t state_ = IPP_PSTATE_IDLE;
	ipp_t* attrs_ = ippNew();
	PrintJob* active_job_;

	// TODO: queueing
	std::unordered_map<int, PrintJob* > jobs_map_;
	// TODO: read-write lock

};