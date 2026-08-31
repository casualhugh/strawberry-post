#include "web_utils.h"

#include <string.h>

#include "app_config.h"
#include "strawberry_core.h"

String escapeJson(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    switch (character) {
      case '"':
        escaped += F("\\\"");
        break;
      case '\\':
        escaped += F("\\\\");
        break;
      case '\b':
        escaped += F("\\b");
        break;
      case '\f':
        escaped += F("\\f");
        break;
      case '\n':
        escaped += F("\\n");
        break;
      case '\r':
        escaped += F("\\r");
        break;
      case '\t':
        escaped += F("\\t");
        break;
      default:
        if (static_cast<uint8_t>(character) < 0x20) {
          char encoded[7];
          snprintf(encoded, sizeof(encoded), "\\u%04x", character & 0xff);
          escaped += encoded;
        } else {
          escaped += character;
        }
    }
  }
  return escaped;
}

bool validUserText(const String& value, bool allowNewlines) {
  return StrawberryCore::validUserText(value.c_str(), value.length(),
                                       allowNewlines);
}

bool validStoredText(const char* value, size_t capacity, bool allowNewlines) {
  if (value == nullptr || capacity == 0 || memchr(value, '\0', capacity) == nullptr) {
    return false;
  }
  return StrawberryCore::validUserText(value, strnlen(value, capacity),
                                       allowNewlines);
}

bool formRequestWithinLimits(WebServer& server) {
  if (server.args() > AppConfig::kMaxFormArguments) {
    return false;
  }
  const String contentLength = server.header("Content-Length");
  if (!contentLength.isEmpty()) {
    size_t parsedLength = 0;
    for (size_t index = 0; index < contentLength.length(); ++index) {
      const char character = contentLength[index];
      if (character < '0' || character > '9') {
        return false;
      }
      parsedLength = parsedLength * 10 + static_cast<size_t>(character - '0');
      if (parsedLength > AppConfig::kMaxFormBodyBytes) {
        return false;
      }
    }
  }
  return true;
}

uint32_t appendSubmissionHash(uint32_t hash, const String& value) {
  return StrawberryCore::appendSubmissionHash(hash, value.c_str(),
                                              value.length());
}

bool recentlySubmitted(uint32_t hash, uint32_t previousHash,
                       uint32_t previousTimeMs) {
  return StrawberryCore::recentlySubmitted(
      hash, previousHash, millis(), previousTimeMs,
      AppConfig::kDuplicateWindowMs);
}
