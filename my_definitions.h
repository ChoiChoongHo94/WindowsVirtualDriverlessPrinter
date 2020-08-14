#pragma once
#include <cups/array.h>
#include <cups/ipp.h>
#include <memory>
#include "logger.h"

/* externs for initialization */
// 'main.cpp'
extern std::shared_ptr<ConsoleLogger> CONSOLE_LOGGER;
extern std::shared_ptr<FileLogger> ERROR_LOGGER;

/* Global Definitions */

struct group_filter_s {
	cups_array_t* ra = nullptr;
	ipp_tag_t group_tag;
};

using GroupFilter = group_filter_s;