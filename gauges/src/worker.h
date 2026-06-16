#pragma once

#include <string>

struct SharedState;

void WorkerMain(const std::string& videoPath, SharedState& shared);
