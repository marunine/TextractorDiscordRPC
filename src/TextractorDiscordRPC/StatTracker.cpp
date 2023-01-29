#include "StatTracker.h"

using namespace std::literals;

namespace dt {
namespace tracker {

inline system_time_point_t get_system_time() {
	return std::chrono::system_clock::now();
}

inline std::chrono::year_month_day get_ymd(system_time_point_t time_point) {
	return std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(time_point));
}

inline duration_t seconds_to_duration(int seconds) {
	return std::chrono::duration_cast<duration_t>(std::chrono::seconds(seconds));
}

StatTracker::StatTracker()
	: session_timeout_duration(120s), idle_timeout_duration(600s), share_chars_read(true),
	  clean_re(L"[a-zA-Z\\d\\s,\\[\\]\\-\x3000\x3001\x30FB\x3002\x002E\x3001\x301C\xFF08\xFF09\x3003\x300C\x300D\xFF1A]") {
}

void StatTracker::add_line(const std::filesystem::path &path, const std::wstring &line) {
	auto cleaned = std::regex_replace(line, clean_re, L"");
	auto read_chars = (uint32_t)cleaned.length();
	if (read_chars == 0) {
		return;
	}

	last_path = path;

	auto const title = get_title(path);
	auto const system_time = get_system_time();
	auto ref_time = now();
	auto &novel = novels[title];

	if (novel.empty() || is_expired(novel.back(), ref_time)) {
		novel.push_back(Session{
			.read_chars = 0,
			.time_start = ref_time,
			.time_updated = ref_time,
			.system_time_start = system_time,
			.system_time_updated = system_time,
		});
	}

	auto &session = novel.back();
	session.read_chars += read_chars;
	session.time_updated = ref_time;
	session.system_time_updated = system_time;
}

Stats StatTracker::get_stats(const std::filesystem::path &path) const {
	Stats stats = {};

	auto it = novels.find(get_title(path));
	if (it == novels.end()) {
		return stats;
	}

	auto const &sessions = it->second;
	if (sessions.empty()) {
		return stats;
	}

	auto const ref_system_time = get_system_time();
	auto const ref_ymd = get_ymd(ref_system_time);
	auto const ref_time = now();
	auto total_time = duration_t();

	for (auto const &session : sessions) {
		if (get_ymd(session.system_time_updated) != ref_ymd && get_ymd(session.system_time_start) != ref_ymd) {
			continue;
		}

		if (is_expired(session, ref_time)) {
			total_time += (session.time_updated - session.time_start) + 10s;
		}

		if (share_chars_read) {
			stats.chars += session.read_chars;
		}
	}

	auto const &current_session = sessions.back();
	if (ref_time - current_session.time_updated < idle_timeout_duration) {
		auto start_time = is_expired(current_session, ref_time) ? current_session.system_time_updated : current_session.system_time_start;
		auto session_start = start_time - total_time;
		stats.timestamp = std::chrono::time_point_cast<std::chrono::seconds>(session_start).time_since_epoch().count();
	}

	return stats;
}

void StatTracker::read_settings(const nlohmann::json &json) {
	if (auto share_chars = json.value<bool>("share_read_chars", true)) {
		share_chars_read = share_chars;
	}
	if (auto session_timeout = json.value<int>("session_timeout", 0)) {
		session_timeout_duration = seconds_to_duration(session_timeout);
	}
	if (auto idle_timeout = json.value<int>("idle_timeout", 0)) {
		idle_timeout_duration = seconds_to_duration(idle_timeout);
	}
}

std::filesystem::path StatTracker::get_last_path() const {
	return last_path;
}

bool StatTracker::is_expired(const Session &session, time_point_t ref_time) const {
	return ref_time - session.time_updated > session_timeout_duration;
}

std::wstring StatTracker::get_title(std::filesystem::path const &path) {
	if (path.has_parent_path()) {
		return path.parent_path().filename().wstring() + L'/' + path.filename().wstring();
	}

	return path.filename().wstring();
}

} // namespace tracker
} // namespace dt
