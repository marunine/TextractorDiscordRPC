#include "Extension.h"
#include "StatTracker.h"
#include "NovelTracker.h"
#include <Windows.h>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <format>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <discord_rpc.h>

namespace dt {
namespace ext {

static const char APPLICATION_ID[] = "1061362183335854222";

using namespace std::literals;
using namespace dt::tracker;

static std::mutex ext_mutex;
static std::thread updater_thread;
static bool updater_should_quit;
static DWORD updater_process_id;

static dt::tracker::StatTracker stat_tracker;

static std::filesystem::path get_process_path(DWORD process_id) {
	HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
	std::wstring ret;

	if (process) {
		std::vector<WCHAR> buffer(4096);
		auto size = (DWORD)buffer.size();
		if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
			ret = buffer.data();
		}
		CloseHandle(process);
	}

	return ret;
}

static uint64_t get_last_write_time(const std::filesystem::path &path) {
	uint64_t result = 0;

	auto const wstr = path.wstring();

	HANDLE file = CreateFileW(wstr.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file != INVALID_HANDLE_VALUE) {
		FILETIME ftCreate, ftAccess, ftWrite;
		if (GetFileTime(file, &ftCreate, &ftAccess, &ftWrite)) {
			result = ((uint64_t)ftWrite.dwHighDateTime << 32) | (uint64_t)ftWrite.dwLowDateTime;
		}
	}

	return result;
}

static void update_stats(const std::wstring &sentence, DWORD process_id) {
	auto const path = get_process_path(process_id);

	ext_mutex.lock();
	stat_tracker.add_line(path, sentence);
	ext_mutex.unlock();
}

static void status_update(NovelTracker &novel_tracker) {
	static bool had_activity;

	ext_mutex.lock();
	auto path = stat_tracker.get_last_path();
	auto stats = stat_tracker.get_stats(path);
	ext_mutex.unlock();

	auto info = novel_tracker.lookup(path);
	if (!info.title.empty() && stats.timestamp) {
		DiscordRichPresence presence = {};
		std::string state_text;

		presence.details = info.title.c_str();
		presence.startTimestamp = stats.timestamp;

		if (stats.chars > 0) {
			state_text = std::format("Session Chars: {}", stats.chars);
			presence.state = state_text.c_str();
		}

		if (!info.img_url.empty()) {
			presence.largeImageKey = info.img_url.c_str();
			if (!info.alt.empty()) {
				presence.largeImageText = info.alt.c_str();
			}
		}

		Discord_UpdatePresence(&presence);
		Discord_RunCallbacks();

		had_activity = true;
	} else if (had_activity) {
		had_activity = false;

		Discord_ClearPresence();
		Discord_RunCallbacks();
	}
}

static void discord_callback_ready(const DiscordUser *request) {
	// todo: what should we do?
}

static void discord_callback_disconnected(int errorCode, const char *message) {
	// todo: what should we do?
}

static void discord_callback_error(int errorCode, const char *message) {
	// todo: what should we do?
}

// status_update_thread() - Reports current VN status to Discord Rich Presence
static void status_update_thread() {
	auto const process_path = get_process_path(GetCurrentProcessId());
	auto const config_path = process_path.parent_path() / "discord_config.json";
	auto const update_frequency = 15s;
	NovelTracker novel_tracker;
	uint64_t last_config_write_time = 0;

	DiscordEventHandlers handlers = {};
	handlers.ready = discord_callback_ready;
	handlers.disconnected = discord_callback_disconnected;
	handlers.errored = discord_callback_error;

	Discord_Initialize(APPLICATION_ID, &handlers, 1, nullptr);
	Discord_RunCallbacks();

	for (;;) {
		auto iter_start = std::chrono::high_resolution_clock::now();

		ext_mutex.lock();
		bool should_quit = updater_should_quit;
		ext_mutex.unlock();

		if (should_quit) {
			break;
		}

		// check if json has changed
		auto config_write_time = get_last_write_time(config_path);
		if (config_write_time != last_config_write_time) {
			last_config_write_time = config_write_time;
			try {
				std::ifstream stream(config_path, std::ifstream::binary);
				auto json = nlohmann::json::parse(stream);
				novel_tracker.read_settings(json);

				ext_mutex.lock();
				stat_tracker.read_settings(json);
				ext_mutex.unlock();
			} catch (...) {
				// ignore errors parsing
			}
		}

		// update discord status
		status_update(novel_tracker);

		// sleep until the next iteration
		auto iter_end = std::chrono::high_resolution_clock::now();
		auto time_spent = iter_end - iter_start;
		std::this_thread::sleep_for(time_spent < update_frequency ? update_frequency - time_spent : 1s);
	}
}

} // namespace ext
} // namespace dt

// process_sentence() - Callback for a textractor sentence hook
// @sentence: sentence received by Textractor (UTF-16). Can be modified, Textractor will receive this modification only if true is returned.
// @sentenceInfo: contains miscellaneous info about the sentence (see README).
// Returns: true if the sentence was modified, false otherwise.
//
// Textractor will display the sentence after all extensions have had a chance to process and/or modify it.
// The sentence will be destroyed if it is empty or if you call Skip().
// This function may be run concurrently with itself: please make sure it's thread safe.
// It will not be run concurrently with DllMain.
bool process_sentence(std::wstring &sentence, SentenceInfo sentence_info) {
	if (sentence_info["current select"]) {
		auto const process_id = (DWORD)sentence_info["process id"];
		if (process_id != 0) {
			dt::ext::update_stats(sentence, process_id);
		}
	}

	// no modifications for sentence, purely tracking for statisical purposes
	return false;
}

// DLLMain() - Entry point for the extension.
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
	using namespace dt::ext;

	switch (reason) {
	case DLL_PROCESS_ATTACH:
		updater_thread = std::thread(status_update_thread);
		break;
	case DLL_PROCESS_DETACH:
		ext_mutex.lock();
		updater_should_quit = true;
		ext_mutex.unlock();
		updater_thread.join();
		break;
	}
	return TRUE;
}
