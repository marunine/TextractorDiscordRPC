#include "NovelTracker.h"
#include <cpr/cpr.h>
#include <fstream>
#include <Windows.h>

using namespace std::literals;

namespace dt {
namespace tracker {

static std::string utf8_encode(const std::wstring &wide_string) {
	if (wide_string.empty()) {
		return std::string();
	}

	const int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), (int)wide_string.size(), nullptr, 0, nullptr, nullptr);
	if (size_needed <= 0) {
		return std::string();
	}

	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), (int)wide_string.size(), result.data(), size_needed, nullptr, nullptr);

	return result;
}

static std::wstring utf8_decode(const std::string &string) {
	if (string.empty()) {
		return std::wstring();
	}

	const int size_needed = MultiByteToWideChar(CP_UTF8, 0, string.data(), (int)string.size(), nullptr, 0);
	if (size_needed <= 0) {
		return std::wstring();
	}

	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, string.data(), (int)string.size(), result.data(), size_needed);

	return result;
}

NovelTracker::NovelTracker() {
}

NovelInfo NovelTracker::lookup(std::filesystem::path const &path) {
	NovelInfo info = {};

	if (path == last_lookup_path) {
		auto it = novel_info.find(path);
		if (it != novel_info.end()) {
			return it->second;
		}
	}

	auto const &title = utf8_encode(path.has_parent_path() ? path.parent_path().filename() : path);
	if (title.empty()) {
		return info;
	}

	auto const now = std::chrono::steady_clock::now();
	if (now - last_query_time < 90s) {
		return info;
	}

	last_query_time = now;

	nlohmann::json request = {
		{"filters", std::vector{"search", "=", title.c_str()}},
		{"fields", "id, title, titles.title, titles.official, image.url, image.sexual, image.violence"},
		{"sort", "popularity"},
		{"reverse", false},
		{"results", 1},
		{"page", 1},
		{"count", false},
		{"compact_filters", false},
		{"normalized_filters", false},
	};

	auto response = cpr::Post(cpr::Url{"https://api.vndb.org/kana/vn"}, cpr::Header{{"Content-Type", "application/json"}}, cpr::Body{request.dump()});
	if (response.status_code && response.status_code < 400) {
		NovelInfo new_info = {};
		last_lookup_path = path;

		try {
			auto const json = nlohmann::json::parse(response.text);
			new_info = parse_response(json);
		} catch (...) {
			// do nothing, try again later
		}

		// error parsing, or no info found
		novel_info[path] = new_info;
	}

	return info;
}

void NovelTracker::read_settings(nlohmann::json const &json) {
	auto novels = json.find("novels");
	if (novels == json.end()) {
		return;
	}

	for (auto const &novel : *novels) {
		if (!novel.is_object()) {
			continue;
		}

		auto path = novel.value("path", "");
		if (path.empty()) {
			continue;
		}

		auto wpath = std::filesystem::path(utf8_decode(path));
		if (wpath.empty()) {
			continue;
		}

		auto new_info = NovelInfo();
		new_info.title = novel.value("title", "");
		new_info.alt = novel.value("alt", "");
		new_info.img_url = novel.value("img_url", "");
		new_info.id = novel.value("id", "");

		novel_info[wpath] = new_info;
	}
}

NovelInfo NovelTracker::parse_response(nlohmann::json const &json) {
	NovelInfo info = {};

	auto const results = json.find("results");
	if (results == json.end()) {
		return info;
	}

	if (!results->is_array() || results->empty()) {
		return info;
	}

	auto const entry = results->begin();
	if (!entry->is_object()) {
		return info;
	}

	info.alt = entry->value("title", "");
	info.id = entry->value("id", "");

	// find the official title
	auto const titles = entry->find("titles");
	if (titles != entry->end()) {
		for (auto const &item : *titles) {
			if (!item.is_object()) {
				continue;
			}

			auto is_official = item.value("official", false);
			if (is_official == false) {
				continue;
			}

			info.title = item.value("title", "");
			break;
		}
	}

	// set the image url to something that isn't totally nsfw
	auto const images = entry->find("image");
	if (images != entry->end()) {
		auto sexual = images->value("sexual", 2);
		auto violence = images->value("violence", 2);
		if (sexual < 2 && violence < 2) {
			info.img_url = images->value("url", "");
		}
	}

	return info;
}

} // namespace tracker
} // namespace dt
