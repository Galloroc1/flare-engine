#ifndef AI_CHAT_BRIDGE_H
#define AI_CHAT_BRIDGE_H

#include <string>
#include <vector>

/** A nonblocking line-protocol bridge to the local Python Ran Agent. */
class AIChatBridge {
public:
	AIChatBridge();
	~AIChatBridge();
	bool start();
	void stop();
	bool send(const std::string& message);
	bool replyTool(const std::string& response);
	void poll(std::vector<std::string>& events);
	static std::string decode(const std::string& hex);
	static void logDiagnostic(const std::string& message);

private:
	static std::string encode(const std::string& text);
	int input_fd;
	int output_fd;
	int child_pid;
	std::string buffer;
	size_t response_chunks;
	size_t response_bytes;
};

#endif
