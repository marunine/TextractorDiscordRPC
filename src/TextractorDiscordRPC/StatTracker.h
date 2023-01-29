#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <regex>
#include <vector>
#include <nlohmann/json.hpp>

namespace dt {
namespace tracker {

typedef std::chrono::time_point<std::chrono::system_clock> system_time_point_t;
typedef std::chrono::time_point<std::chrono::steady_clock> time_point_t;
typedef std::chrono::nanoseconds duration_t;

inline time_point_t now() {
	return time_point_t::clock::now();
}

struct Stats {
	int64_t timestamp;
	uint32_t chars;
};

class StatTracker {
public:
	StatTracker();

	void add_line(const std::filesystem::path &path, const std::wstring &line);
	Stats get_stats(const std::filesystem::path &path) const;
	void read_settings(const nlohmann::json &json);
	std::filesystem::path get_last_path() const;

private:
	struct Session {
		uint32_t read_chars;
		time_point_t time_start;
		time_point_t time_updated;
		system_time_point_t system_time_start;
		system_time_point_t system_time_updated;
	};

	bool is_expired(const Session &session, time_point_t ref_time) const;
	static std::wstring get_title(std::filesystem::path const &path);

	std::filesystem::path last_path;
	std::wregex clean_re;
	std::unordered_map<std::wstring, std::vector<Session>> novels;
	duration_t session_timeout_duration;
	duration_t idle_timeout_duration;
	bool share_chars_read;
};

} // namespace tracker
} // namespace dt
