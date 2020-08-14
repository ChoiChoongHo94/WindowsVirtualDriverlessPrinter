#include "logger.h"
#include "my_util.h"
#include <cassert>
#include <iostream>

LoggerBase::LoggerBase() {
	format_buffer_ = new char[kFormatBufferSize];
	assert(format_buffer_ != nullptr);
}

LoggerBase::~LoggerBase() {
	delete[] format_buffer_;
}

ConsoleLogger::ConsoleLogger() {
	SetConsoleOutputCP(CP_UTF8);
}

void ConsoleLogger::writeLog(const std::string& message) {
	std::cerr << message << '\n' << std::flush;
}

FileLogger::FileLogger(const std::string& filepath)
	: filepath_(filepath) {
	assert(!filepath_.empty());
	openOstream(filepath_);
}

FileLogger::~FileLogger() {
	closeOstream();
}

void FileLogger::openOstream(const std::string& filepath) {
	file_output_stream_.open(filepath, std::ofstream::out | std::ofstream::app);
}

void FileLogger::closeOstream() {
	file_output_stream_.close();
}

void FileLogger::writeLog(const std::string& message) {
	file_output_stream_ << Util::get_timestamp() << " " << message <<
		(*(--cend(message)) == '\n' ? "" : "\n") << std::flush;
}