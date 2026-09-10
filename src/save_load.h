#pragma once
#include "app_state.h"
#include <string>

bool saveProject(const std::string& filepath);
bool loadProject(const std::string& filepath);
bool saveProjectInteractive();
bool loadProjectInteractive();
bool exportStl(const std::string& filepath);
