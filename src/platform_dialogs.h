#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool showNativeStlSaveDialog(const char* suggestedPath, char* outPath, size_t outPathSize);
bool showNativeProjectSaveDialog(const char* suggestedPath, char* outPath, size_t outPathSize);
bool showNativeProjectOpenDialog(const char* suggestedPath, char* outPath, size_t outPathSize);

#ifdef __cplusplus
}
#endif