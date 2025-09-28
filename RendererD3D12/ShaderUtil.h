#pragma once

bool CreateShaderCodeFromFile(
	uint8_t** ppOutCodeBuffer, UINT* outCodeSize, SYSTEMTIME* outLastWriteTime, const WCHAR* wchFileName);

void DeleteShaderCode(uint8_t* pCodeBuffer);

HRESULT CompileShaderFromFileWithDXC(
	IDxcLibrary* pLibrary,
	IDxcCompiler* pCompiler,
	IDxcIncludeHandler*
	pIncludeHandler,
	const WCHAR* wchFileName,
	const WCHAR* wchEntryPoint,
	const WCHAR* wchShaderModel,
	IDxcBlob** ppOutCodeBlob,
	bool bDisableOptimize,
	SYSTEMTIME* outLastWriteTime,
	UINT flags);