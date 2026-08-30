#pragma once

#include <Arduino.h>
#include <WebServer.h>

String escapeJson(const String& value);
String escapeHtml(const String& value);
bool validUserText(const String& value, bool allowNewlines = true);
bool validStoredText(const char* value, size_t capacity,
                     bool allowNewlines = true);
bool formRequestWithinLimits(WebServer& server);
uint32_t appendSubmissionHash(uint32_t hash, const String& value);
bool recentlySubmitted(uint32_t hash, uint32_t previousHash,
                       uint32_t previousTimeMs);
