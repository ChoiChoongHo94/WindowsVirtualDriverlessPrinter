#pragma once
#include <cups/array.h>
#include <cups/ipp.h>

struct group_filter_s {
	cups_array_t* ra = nullptr;
	ipp_tag_t group_tag;
};

using GroupFilter = group_filter_s;