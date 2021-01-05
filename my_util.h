#pragma once
#include <cups/ipp.h>
#include <cassert>
#include <vector>
#include <string>
#include "my_definitions.h"

class PrintJob;

namespace Util {
	void copy_attributes(ipp_t* dst, ipp_t* src, cups_array_t* ra, ipp_tag_t group_tag, int quickcopy);
	void copy_job_attributes(ipp_t* dst, PrintJob* pjob, cups_array_t* ra);
	void copy_job_attributes(ipp_t* dst, PrintJob* pjob, const std::vector<std::string>& rv);
	int group_filter_cb(const GroupFilter& filter, ipp_t* dst, ipp_attribute_t* attr);
	std::string get_attr_stamp(ipp_attribute_t* attr); // return 'name:value' of an ipp attribute
	const std::string get_userhome_dir();
	const std::string get_timestamp();
	std::string wstr_to_str(wchar_t* wstr, size_t len); // FIXME: delete
	std::string wstr_to_utf8(const std::wstring& wstr);
	std::wstring utf8_to_wstr(const std::string& utf8);
	bool hangle_check(const std::string& utf8);
	std::string hash_str(const std::string& str);
}