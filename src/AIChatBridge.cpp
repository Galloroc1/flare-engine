#include "AIChatBridge.h"

#include <cerrno>
#include <ctime>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

AIChatBridge::AIChatBridge()
	: input_fd(-1), output_fd(-1), child_pid(-1), response_chunks(0), response_bytes(0) {}
AIChatBridge::~AIChatBridge() { stop(); }

void AIChatBridge::logDiagnostic(const std::string& message) {
	const char* configured_path = getenv("FLARE_AI_LOG");
	std::string path = (configured_path && *configured_path)
		? configured_path : "../flare-engine/python/village_chief.log";
	std::ofstream log(path.c_str(), std::ios::app);
	if (!log) return;
	time_t now = time(NULL);
	struct tm local_time;
	localtime_r(&now, &local_time);
	char timestamp[32];
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &local_time);
	log << timestamp << " cpp " << message << std::endl;
}

bool AIChatBridge::start() {
	stop();
	const char* script = getenv("FLARE_AI_BRIDGE");
	const char* python = getenv("FLARE_AI_PYTHON");
	// The launcher sets absolute paths; these defaults also support running the
	// game binary directly from flare-game, its usual working directory.
	if (!script || !*script) script = "../flare-engine/python/ai_village_chief.py";
	if (!python || !*python) python = "../flare-engine/.venv/bin/python";
	if (access(script, R_OK) != 0 || access(python, X_OK) != 0)
		return false;
	// A Python startup failure must be reported in the dialog, not terminate Flare.
	signal(SIGPIPE, SIG_IGN);
	int to_child[2], from_child[2];
	if (pipe(to_child) != 0) return false;
	if (pipe(from_child) != 0) {
		close(to_child[0]); close(to_child[1]);
		return false;
	}
	child_pid = static_cast<int>(fork());
	if (child_pid == 0) {
		setenv("FLARE_AI_SOURCE", "game", 1);
		dup2(to_child[0], STDIN_FILENO);
		dup2(from_child[1], STDOUT_FILENO);
		close(to_child[0]); close(to_child[1]);
		close(from_child[0]); close(from_child[1]);
		execl(python, python, "-u", script, static_cast<char*>(NULL));
		_exit(127);
	}
	close(to_child[0]); close(from_child[1]);
	if (child_pid < 0) {
		close(to_child[1]); close(from_child[0]);
		return false;
	}
	input_fd = to_child[1];
	output_fd = from_child[0];
	response_chunks = response_bytes = 0;
	logDiagnostic("bridge_started child_pid=" + std::to_string(child_pid));
	int flags = fcntl(output_fd, F_GETFL, 0);
	if (flags != -1) fcntl(output_fd, F_SETFL, flags | O_NONBLOCK);
	return true;
}

void AIChatBridge::stop() {
	if (input_fd >= 0) close(input_fd);
	if (output_fd >= 0) close(output_fd);
	input_fd = output_fd = -1;
	buffer.clear();
	if (child_pid > 0) {
		// Closing stdin ends the Python loop; if it is inside a model call,
		// stop it so changing maps or closing the menu never blocks the game.
		kill(child_pid, SIGTERM);
		int status;
		waitpid(child_pid, &status, 0);
	}
	child_pid = -1;
}

std::string AIChatBridge::encode(const std::string& value) {
	const char* digits = "0123456789abcdef";
	std::string result;
	result.reserve(value.size() * 2);
	for (size_t i = 0; i < value.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(value[i]);
		result += digits[c >> 4];
		result += digits[c & 15];
	}
	return result;
}

std::string AIChatBridge::decode(const std::string& hex) {
	std::string result;
	if (hex.size() % 2) return result;
	for (size_t i = 0; i < hex.size(); i += 2) {
		char hi = hex[i], lo = hex[i+1];
		int a = (hi >= '0' && hi <= '9') ? hi-'0' : (hi >= 'a' && hi <= 'f') ? hi-'a'+10 : -1;
		int b = (lo >= '0' && lo <= '9') ? lo-'0' : (lo >= 'a' && lo <= 'f') ? lo-'a'+10 : -1;
		if (a < 0 || b < 0) return "";
		result += static_cast<char>((a << 4) | b);
	}
	return result;
}

bool AIChatBridge::send(const std::string& message) {
	if (input_fd < 0 || child_pid < 0) return false;
	int status;
	if (waitpid(child_pid, &status, WNOHANG) == child_pid) {
		child_pid = -1;
		logDiagnostic("send_failed child_exited_before_request");
		return false;
	}
	std::string line = "Q\t" + encode(message) + "\n";
	// A large input is rejected before reaching this code by the UI.
	bool sent = write(input_fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
	response_chunks = response_bytes = 0;
	logDiagnostic("request_sent bytes=" + std::to_string(message.size()) + " accepted=" + (sent ? "true" : "false"));
	return sent;
}

void AIChatBridge::poll(std::vector<std::string>& events) {
	if (output_fd < 0) return;
	char chunk[4096];
	ssize_t count;
	while ((count = read(output_fd, chunk, sizeof(chunk))) > 0) {
		buffer.append(chunk, static_cast<size_t>(count));
		if (buffer.size() > 1024 * 1024) {
			events.push_back("E\t" + encode("村长的回复过长，请重新提问。"));
			stop();
			return;
		}
	}
	if (count == 0) {
		close(output_fd);
		output_fd = -1;
	}
	size_t end;
	while ((end = buffer.find('\n')) != std::string::npos) {
		std::string line = buffer.substr(0, end);
		buffer.erase(0, end + 1);
		if (line.empty()) continue;
		if (line.size() > 2 && line[0] == 'D' && line[1] == '\t') {
			response_chunks++;
			response_bytes += (line.size() - 2) / 2;
		}
		else if (line == "F") {
			logDiagnostic("python_stream_finished chunks=" + std::to_string(response_chunks) +
				" answer_bytes=" + std::to_string(response_bytes));
		}
		else if (line == "X") {
			logDiagnostic("python_stream_closed_unexpectedly");
		}
		events.push_back(line);
	}
	if (count == 0) events.push_back("X");
}

// Tool results use a separate frame so they never enter conversation as user text.
bool AIChatBridge::replyTool(const std::string& response) {
	if (input_fd < 0) return false;
	std::string line = "R\t" + encode(response) + "\n";
	return write(input_fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
}
