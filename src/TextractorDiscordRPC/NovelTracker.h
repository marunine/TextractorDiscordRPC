#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace dt {
namespace tracker {

struct NovelInfo {
	std::string title;
	std::string alt;
	std::string img_url;
	std::string id;
};

class NovelTracker {
public:
	NovelTracker();

	NovelInfo lookup(std::filesystem::path const &path);
	void read_settings(nlohmann::json const &json);

private:
	static NovelInfo parse_response(nlohmann::json const &json);

	std::unordered_map<std::filesystem::path, NovelInfo> novel_info;
	std::filesystem::path last_lookup_path;
	std::string last_lookup_title;
	std::chrono::steady_clock::time_point last_query_time;
};

} // namespace tracker
} // namespace dt
