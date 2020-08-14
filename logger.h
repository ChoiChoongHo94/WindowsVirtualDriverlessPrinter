#pragma once
#include <string>
#include <fstream>
#include <queue>
#include <array>
#include <cstdio>

class LoggerBase {
public:
	LoggerBase();
	virtual ~LoggerBase();
	LoggerBase(const LoggerBase&) = delete;
	LoggerBase(LoggerBase&&) = delete;
	LoggerBase& operator=(const LoggerBase&) = delete;
	LoggerBase& operator=(LoggerBase&&) = delete;
	virtual void writeLog(const std::string& message) = 0;
	//template<typename... Args>
	//void writefLog(const std::string& format, Args... args) {
	//	//size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
	//	size_t size = snprintf(format_buffer_, kFormatBufferSize, format.c_str(), args...);
	//	this->writeLog(std::string(format_buffer_));
	//};
	//template<typename T>
	//LoggerBase& operator<<(T arg) {
	//
	//}
protected:
	virtual void openOstream(const std::string& name) = 0; // convert to ostream -> outstream
	virtual void closeOstream() = 0;
private:
	const int kFormatBufferSize = 4 * 1024; // 4KB
	char* format_buffer_ = nullptr;
	//std::queue<std::string> message_queue_; //TODO: multi-threading
};

class ConsoleLogger : public LoggerBase {
public:
	ConsoleLogger();
	virtual ~ConsoleLogger() = default;
	virtual void writeLog(const std::string& message) override;
protected:
	virtual void openOstream(const std::string&) override {};
	virtual void closeOstream() override {};
};

class FileLogger : public LoggerBase {
public:
	FileLogger();
	FileLogger(const std::string& filepath);
	virtual ~FileLogger();
	std::string getFilepath() const { return filepath_; };
	virtual void writeLog(const std::string& message) override;
protected:
	virtual void openOstream(const std::string& filepath) override; // TODO: 에러체크
	virtual void closeOstream() override;
private:
	const std::string filepath_;
	std::ofstream file_output_stream_;
};