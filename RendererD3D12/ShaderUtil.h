#pragma once

bool CreateShaderCodeFromFile(uint8_t** ppOutCodeBuffer, uint* outCodeSize, SYSTEMTIME* outLastWriteTime, const wchar_t* wchFileName);

void DeleteShaderCode(uint8_t* pCodeBuffer);

HRESULT CompileShaderFromFileWithDXC(
	IDxcLibrary* pLibrary,
	IDxcCompiler* pCompiler,
	IDxcIncludeHandler*
	pIncludeHandler,
	const wchar_t* wchFileName,
	const wchar_t* wchEntryPoint,
	const wchar_t* wchShaderModel,
	IDxcBlob** ppOutCodeBlob,
	bool bDisableOptimize,
	SYSTEMTIME* outLastWriteTime,
	uint flags);