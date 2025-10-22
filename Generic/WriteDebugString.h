#pragma once

enum DebugOutputType
{
	DEBUG_OUTPUT_TYPE_NULL,
	DEBUG_OUTPUT_TYPE_CONSOLE,
	DEBUG_OUTPUT_TYPE_DEBUG_CONSOLE
};

void WriteDebugStringW(DebugOutputType type, const wchar_t* wchFormat, ...);
void WriteDebugStringA(DebugOutputType type, const char* szFormat, ...);
