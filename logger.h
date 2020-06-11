#pragma once
#include <string>
#include <fstream>
#include <queue>
#include <array>

class LoggerBase {
public:
	LoggerBase();
	virtual ~LoggerBase();
	LoggerBase(const LoggerBase&) = delete;
	LoggerBase(LoggerBase&&) = delete;
	LoggerBase& operator=(const LoggerBase&) = delete;
	LoggerBase& operator=(LoggerBase&&) = delete;
	template<typename... Args>
	void writef(const std::string& format, Args... args);
protected:
	virtual void openOstream(const std::string& name) = 0;
	virtual void closeOstream() = 0;
	virtual void write(const std::string& message) = 0;
private:
	constexpr static int kFormatBufferSize = 4 * 1024; // 4KB
	char* format_buffer_ = nullptr;
	//std::queue<std::string> message_queue_; //TODO: multi-threading
};

class ConsoleLogger : public LoggerBase {
public:
	ConsoleLogger() = default;
	virtual ~ConsoleLogger() = default;
	//template<typename... Args>
	//void writef(const std::string& format, Args... args);

private:
	virtual void openOstream(const std::string&) override {};
	virtual void closeOstream() override {};
	virtual void write(const std::string& message) override;
};

class FileLogger : public LoggerBase {
public:
	FileLogger();
	FileLogger(const std::string& filepath);
	//FileLogger(const FileLogger& src);
	//FileLogger& operator=(const FileLogger& rhs);
	virtual ~FileLogger();
	//template<typename... Args>
	//void writef(const std::string& format, Args... args);

	std::string getFilepath() const { return filepath_; };

private:
	const std::string filepath_;
	std::ofstream file_output_stream_;

	void init();
	virtual void openOstream(const std::string& filepath) override;
	virtual void closeOstream() override;
	virtual void write(const std::string& message) override;
};