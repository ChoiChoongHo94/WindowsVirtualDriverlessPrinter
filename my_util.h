#pragma once
#include <cups/ipp.h>
#include "my_definitions.h"

class PrintJob;

namespace Util {
	void copy_attributes(ipp_t* dst, ipp_t* src, cups_array_t* ra, ipp_tag_t group_tag, int quickcopy);
	void copy_job_attributes(ipp_t* dst, PrintJob* pjob, cups_array_t* ra);
	int group_filter_cb(const GroupFilter& filter, ipp_t* dst, ipp_attribute_t* attr);
};