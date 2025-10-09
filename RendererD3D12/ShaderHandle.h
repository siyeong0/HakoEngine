#pragma once
#include "pch.h"

constexpr uint MAX_SHADER_NAME_BUFFER_LEN = 256;
constexpr uint MAX_SHADER_NAME_LEN = MAX_SHADER_NAME_BUFFER_LEN - 1;
constexpr uint MAX_SHADER_NUM = 2048;
constexpr uint MAX_CODE_SIZE = (1024 * 1024);

struct ShaderHandle
{
	uint Flags;
	uint CodeSize;
	uint ShaderNameLen;
	WCHAR wchShaderName[MAX_SHADER_NAME_BUFFER_LEN];
	uint CodeBuffer[1];
};