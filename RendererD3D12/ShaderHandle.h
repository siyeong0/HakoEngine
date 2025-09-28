#pragma once
#include "pch.h"

constexpr UINT MAX_SHADER_NAME_BUFFER_LEN = 256;
constexpr UINT MAX_SHADER_NAME_LEN = MAX_SHADER_NAME_BUFFER_LEN - 1;
constexpr UINT MAX_SHADER_NUM = 2048;
constexpr UINT MAX_CODE_SIZE = (1024 * 1024);

struct ShaderHandle
{
	UINT Flags;
	UINT CodeSize;
	UINT ShaderNameLen;
	WCHAR wchShaderName[MAX_SHADER_NAME_BUFFER_LEN];
	UINT CodeBuffer[1];
};