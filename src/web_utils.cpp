#include "web_utils.h"

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

String escapeHtml(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 16);
  for (size_t index = 0; index < value.length(); ++index) {
    switch (value[index]) {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      case '\'':
        escaped += F("&#39;");
        break;
      default:
        escaped += value[index];
    }
  }
  return escaped;
}
