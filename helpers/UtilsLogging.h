/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#pragma once

#include <array>
#include <cctype>
#include <core/JSON.h>
#include <string>
#include <syscall.h>
#include <vector>

enum LogLevel {FATAL_LEVEL = 0, ERROR_LEVEL, WARNING_LEVEL, INFO_LEVEL, DEBUG_LEVEL};

static int gDefaultLogLevel = DEBUG_LEVEL;

#ifdef __DEBUG__    // XXX: maybe use BUILD_TYPE
#define LOGTRACE(fmt, ...) do { fprintf(stderr, "[%d] TRACE [%s:%d] %s: " fmt "\n", (int)syscall(SYS_gettid), WPEFramework::Core::FileNameOnly(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); fflush(stderr); } while (0)
#else
#define LOGTRACE(fmt, ...)
#endif
#define LOGDBG(fmt, ...) do { fprintf(stderr, "[%d] DEBUG [%s:%d] %s: " fmt "\n", (int)syscall(SYS_gettid), WPEFramework::Core::FileNameOnly(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); fflush(stderr); } while (0)
#define LOGINFO(fmt, ...) do { fprintf(stderr, "[%d] INFO [%s:%d] %s: " fmt "\n", (int)syscall(SYS_gettid), WPEFramework::Core::FileNameOnly(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); fflush(stderr); } while (0)
#define LOGWARN(fmt, ...) do { fprintf(stderr, "[%d] WARN [%s:%d] %s: " fmt "\n", (int)syscall(SYS_gettid), WPEFramework::Core::FileNameOnly(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); fflush(stderr); } while (0)
#define LOGERR(fmt, ...) do { fprintf(stderr, "[%d] ERROR [%s:%d] %s: " fmt "\n", (int)syscall(SYS_gettid), WPEFramework::Core::FileNameOnly(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); fflush(stderr); } while (0)

#define LOG_DEVICE_EXCEPTION0() LOGWARN("Exception caught: code=%d message=%s", err.getCode(), err.what());
#define LOG_DEVICE_EXCEPTION1(param1) LOGWARN("Exception caught" #param1 "=%s code=%d message=%s", param1.c_str(), err.getCode(), err.what());
#define LOG_DEVICE_EXCEPTION2(param1, param2) LOGWARN("Exception caught " #param1 "=%s " #param2 "=%s code=%d message=%s", param1.c_str(), param2.c_str(), err.getCode(), err.what());

namespace WPEFramework {
namespace LogSanitizer {

constexpr size_t kMaxLogPayloadLength = 200;
constexpr size_t kMaxJsonDepth = 32;

static const std::array<const char*, 10> kSensitiveJsonKeys = {
	"sat",
	"cdnaccesstoken",
	"accounttoken",
	"advertising.vcid2",
	"license",
	"accesstoken",
	"access_token",
	"authorization",
	"token",
	"authtoken"
};

static const std::array<const char*, 22> kSensitiveNeedles = {
	"\"sat\"",
	"\\\"sat\\\"",
	"\"cdnaccesstoken\"",
	"\\\"cdnaccesstoken\\\"",
	"\"accounttoken\"",
	"\\\"accounttoken\\\"",
	"\"advertising.vcid2\"",
	"\\\"advertising.vcid2\\\"",
	"\"license\"",
	"\\\"license\\\"",
	"\"accesstoken\"",
	"\\\"accesstoken\\\"",
	"\"access_token\"",
	"\\\"access_token\\\"",
	"\"authorization\"",
	"\\\"authorization\\\"",
	"\"token\"",
	"\\\"token\\\"",
	"token=",
	"\"authtoken\"",
	"\\\"authtoken\\\"",
	"bearer "
};

struct JsonScanState {
	bool foundSensitive = false;
	bool foundOversizedString = false;
};

inline std::string ToLowerCopy(const std::string& input)
{
	std::string out = input;
	for (auto& c : out) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

inline bool IsSensitiveJsonKey(const std::string& key)
{
	const std::string lowered = ToLowerCopy(key);
	for (const auto* k : kSensitiveJsonKeys) {
		if (lowered == k) {
			return true;
		}
	}

	return false;
}

inline bool ContainsSensitiveField(const std::string& input)
{
	const std::string lowered = ToLowerCopy(input);
	for (const auto* needle : kSensitiveNeedles) {
		if (lowered.find(needle) != std::string::npos) {
			return true;
		}
	}

	return false;
}

inline void ScanJsonVariantForSensitivity(const Core::JSON::Variant& value, const char* key, const size_t depth, JsonScanState& state)
{
	if (state.foundOversizedString || depth > kMaxJsonDepth) {
		return;
	}

	if (key != nullptr && !state.foundSensitive && IsSensitiveJsonKey(key)) {
		state.foundSensitive = true;
	}

	if (key != nullptr && !state.foundOversizedString) {
		if (std::char_traits<char>::length(key) > kMaxLogPayloadLength) {
			state.foundOversizedString = true;
			return;
		}
	}

	switch (value.Content()) {
	case Core::JSON::Variant::type::STRING: {
		const auto& jsonString = static_cast<const Core::JSON::String&>(value);
		if (!state.foundOversizedString && jsonString.Value().size() > kMaxLogPayloadLength) {
			state.foundOversizedString = true;
		}
		break;
	}
	case Core::JSON::Variant::type::OBJECT: {
		const Core::JSON::VariantContainer& nested = value.Object();
		Core::JSON::VariantContainer::Iterator nestedIt = nested.Variants();
		while (nestedIt.Next()) {
			ScanJsonVariantForSensitivity(nestedIt.Current(), nestedIt.Label(), depth + 1, state);
			if (state.foundOversizedString) {
				return;
			}
		}
		break;
	}
	case Core::JSON::Variant::type::ARRAY: {
		const Core::JSON::ArrayType<Core::JSON::Variant>& arr = value.Array();
		auto arrIt = arr.Elements();
		while (arrIt.Next()) {
			ScanJsonVariantForSensitivity(arrIt.Current(), nullptr, depth + 1, state);
			if (state.foundOversizedString) {
				return;
			}
		}
		break;
	}
	default:
		break;
	}
}

inline bool HasSensitiveDataInJson(const std::string& input)
{
	const bool keySignatureFound = ContainsSensitiveField(input);

	if (input.size() <= kMaxLogPayloadLength) {
		if (!keySignatureFound) {
			return false;
		}
	}

	Core::JSON::Variant root;
	if (!root.FromString(input)) {
		// Signature match in non-JSON payload should still be treated as sensitive.
		if (keySignatureFound) {
			return true;
		}

		// Scalar/raw payloads (for example quoted tokens) may fail object parsing.
		// Suppress oversized payloads to avoid leaking opaque token values.
		if (input.size() > kMaxLogPayloadLength) {
			return true;
		}
		return false;
	}

	JsonScanState state;
	ScanJsonVariantForSensitivity(root, nullptr, 0, state);

	const bool isSensitive = keySignatureFound || state.foundSensitive || state.foundOversizedString;
	return isSensitive;
}

inline std::string RedactSensitiveForLog(const std::string& input, bool& isSensitive)
{
	isSensitive = HasSensitiveDataInJson(input);
	if (isSensitive) {
		return std::string("payload.length=") + std::to_string(input.size());
	}

	return input;
}

} // namespace LogSanitizer
} // namespace WPEFramework
