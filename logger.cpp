#include "logger.h"
#include <cstdio>
#include <cassert>
#include <iostream>

	LoggerBase::LoggerBase() {
		format_buffer_ = new char[kFormatBufferSize];
		assert(format_buffer_ != nullptr);
	}

	LoggerBase::~LoggerBase() {
		delete[] format_buffer_;
	}

	template<typename... Args>
	void LoggerBase::writef(const std::string& format, Args... args) {
		size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
		snprintf(format_buffer_.get(), size, format.c_str(), args...);
		write(std::string(format_buffer_.get()));
	}

	FileLogger::FileLogger(const std::string& filepath)
		: filepath_(filepath) {
		init();
	}

	FileLogger::~FileLogger() {
		closeOstream();
	}

	//template<typename... Args>
	//void FileLogger::writef(const std::string& format, Args... args) {
	//	std::string formatted_string = formatString(format, args...);
	//	write(formatted_string);
	//}

	void FileLogger::init() {
		assert(!filepath_.empty());
		openOstream(filepath_);
	}

	void FileLogger::openOstream(const std::string& filepath) {
		file_output_stream_.open(filepath, std::ofstream::out | std::ofstream::app);
	}

	void FileLogger::closeOstream() {
		file_output_stream_.close();
	}

	void FileLogger::write(const std::string& message) {
		file_output_stream_ << message << '\n';
	}

	//template<typename... Args>
	//void ConsoleLogger::writef(const std::string& format, Args... args) {
	//	return;
	//}

	void ConsoleLogger::write(const std::string& message) {
		std::cout << message << '\n';
	}