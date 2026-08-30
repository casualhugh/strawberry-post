#pragma once

#include <WebServer.h>

void recordHttpRequest(WebServer& server);
void handlePeriodicDiagnostics();
void sendDiagnosticsJson(WebServer& server);
