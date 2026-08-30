#include "web_utils.h"

#include <string.h>

#include "app_config.h"

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

// Validate Unicode scalar values rather than treating UTF-8 bytes as ASCII.
// The byte boundaries below come directly from the UTF-8 encoding rules.
bool validUserText(const String& value, bool allowNewlines) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value.c_str());
  size_t index = 0;
  while (index < value.length()) {
    const uint8_t first = bytes[index];
    if (first < 0x80) {
      if ((first < 0x20 || first == 0x7f) &&
          !(allowNewlines && (first == '\n' || first == '\r' || first == '\t'))) {
        return false;
      }
      ++index;
      continue;
    }

    size_t continuationCount = 0;
    uint32_t codePoint = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      continuationCount = 1;
      codePoint = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuationCount = 2;
      codePoint = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuationCount = 3;
      codePoint = first & 0x07;
    } else {
      return false;
    }
    if (index + continuationCount >= value.length()) {
      return false;
    }
    for (size_t offset = 1; offset <= continuationCount; ++offset) {
      const uint8_t continuation = bytes[index + offset];
      if ((continuation & 0xc0) != 0x80) {
        return false;
      }
      codePoint = (codePoint << 6) | (continuation & 0x3f);
    }
    if ((continuationCount == 2 && codePoint < 0x800) ||
        (continuationCount == 3 && codePoint < 0x10000) ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff) {
      return false;
    }
    index += continuationCount + 1;
  }
  return true;
}

bool validStoredText(const char* value, size_t capacity, bool allowNewlines) {
  if (value == nullptr || capacity == 0 || memchr(value, '\0', capacity) == nullptr) {
    return false;
  }
  return validUserText(String(value), allowNewlines);
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
  constexpr uint32_t kFnv1aOffsetBasis = 2166136261UL;
  constexpr uint32_t kFnv1aPrime = 16777619UL;
  constexpr uint8_t kFieldSeparator = 0xff;
  if (hash == 0) {
    hash = kFnv1aOffsetBasis;
  }
  for (size_t index = 0; index < value.length(); ++index) {
    hash ^= static_cast<uint8_t>(value[index]);
    hash *= kFnv1aPrime;
  }
  hash ^= kFieldSeparator;
  hash *= kFnv1aPrime;
  return hash;
}

bool recentlySubmitted(uint32_t hash, uint32_t previousHash,
                       uint32_t previousTimeMs) {
  return previousHash != 0 && hash == previousHash &&
         static_cast<uint32_t>(millis() - previousTimeMs) <
             AppConfig::kDuplicateWindowMs;
}
