#include "my_util.h"
#include "print_job.h"
#include <cups/ipp.h>
#include <cups/cups.h>
#include <string>
#include <iostream>

namespace Util {
	//TODO: wrap cups_array_t to std::vector
	void copy_attributes(ipp_t* dst, ipp_t* src, cups_array_t* ra, ipp_tag_t group_tag, int quickcopy) {
		GroupFilter filter = { ra, IPP_TAG_ZERO };
		ippCopyAttributes(dst, src, quickcopy, (ipp_copycb_t)group_filter_cb, &filter);
	}

	void copy_job_attributes(ipp_t* dst, PrintJob* job, cups_array_t* ra) {
		auto job_attrs = job->getAttributes();
		copy_attributes(dst, job_attrs, ra, IPP_TAG_JOB, 0);

		if (!ra || cupsArrayFind(ra, (void*)"job-state")) {
			ippAddInteger(dst, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", (int)job->getState());
		}

		if (!ra || cupsArrayFind(ra, (void*)"job-state-reasons")) {
			switch (job->getState()) {
			case IPP_JSTATE_PENDING:
				ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
					NULL, "none");
				break;
			case IPP_JSTATE_HELD:
				// TODO: job-hold-until
				if (job->getFd()) {
					ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
						NULL, "job-incoming");
				}
				else {
					ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
						NULL, "job-data-insufficient");
				}
				break;
			case IPP_JSTATE_PROCESSING:
				// TODO: job->cancel
				ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
					NULL, "job-printing");
				break;
			case IPP_JSTATE_STOPPED:
				ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
					NULL, "job-stopped");
				break;
			case IPP_JSTATE_CANCELED:
				ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
					NULL, "job-canceled-by-user");
				break;
			case IPP_JSTATE_ABORTED:
				ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
					NULL, "aborted-by-system");
				break;
			case IPP_JSTATE_COMPLETED:
				ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
					NULL, "job-completed-successfully");
				break;
			default:
				std::cerr << "Aborted!!" << '\n';
				abort();
			}

			// 이렇게 한번에 하면, attribute의 value로 쓰레기값이 들어감. 여유있을 때 원인분석 ㄱㄱ
			//ippAddString(dst, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
			//	NULL, reason_str.c_str());
		}

		/* TODO
			job-impressions
			job-impressions-completed
			job-printer-up-time
			job-state-message
			date-time-at-completed
			date-time-at-prociessing
			time-at-completed
			time-at-processing
			job-impressions
			job-impressions-completed
			job-printer-up-time
			job-state-message
		*/
		return;
	}

	void copy_job_attributes(ipp_t* dst, PrintJob* job, const std::vector<std::string>& rv) {
		cups_array_t* ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
		for (auto element : rv) {
			cupsArrayAdd(ra, (void*)element.c_str());
		}
		copy_job_attributes(dst, job, ra);
		cupsArrayDelete(ra);
		return;
	}

	int group_filter_cb(const GroupFilter& filter, ipp_t* dst, ipp_attribute_t* attr) {
		ipp_tag_t attr_group = ippGetGroupTag(attr);
		std::string attr_name = std::string(ippGetName(attr));

		if ((filter.group_tag != IPP_TAG_ZERO && attr_group != filter.group_tag && attr_group != IPP_TAG_ZERO) ||
			attr_name.empty() ||
			(attr_name == "media-col-database" && !cupsArrayFind(filter.ra, (void*)attr_name.c_str()))) {
			return 0;
		}

		/*
		In RFC 8011, Section 4.2.5.1. "Get-Printer-Attributes Request",
		If the 'requested-attributes' is NULL(i.e. here the 'ra' is NULL),
		this means that the client is interested to 'all' attributes.
		*/
		return (!filter.ra || (cupsArrayFind(filter.ra, (void*)attr_name.c_str()) != nullptr));
	}
}