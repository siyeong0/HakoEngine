#include "pch.h"
#include <D3DCompiler.h>
#include "Common/WriteDebugString.h"
#include "ShaderUtil.h"

bool CreateShaderCodeFromFile(uint8_t** ppOutCodeBuffer, UINT* outCodeSize, SYSTEMTIME* pOutLastWriteTime, const WCHAR* wchFileName)
{
	bool bResult = false;

	UINT openFlags = OPEN_EXISTING;
	UINT accessMode = GENERIC_READ;
	UINT share = FILE_SHARE_READ;

	WCHAR	wchTxt[256] = {};

	CREATEFILE2_EXTENDED_PARAMETERS extendedParams = {};
	extendedParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
	extendedParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	extendedParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
	extendedParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
	extendedParams.lpSecurityAttributes = nullptr;
	extendedParams.hTemplateFile = nullptr;

	HANDLE	hFile = CreateFile2(wchFileName, accessMode, share, openFlags, &extendedParams);
	if (INVALID_HANDLE_VALUE == hFile)
	{
		swprintf_s(wchTxt, L"Shader File Not Found : %s\n", wchFileName);
		OutputDebugStringW(wchTxt);
		goto lb_return;
	}

	UINT fileSize = GetFileSize(hFile, nullptr);
	if (fileSize > 1024 * 1024)
	{
		swprintf_s(wchTxt, L"Invalid Shader File : %s\n", wchFileName);
		OutputDebugStringW(wchTxt);
		goto lb_close_return;
	}
	UINT codeSize = fileSize + 1;

	uint8_t* pCodeBuffer = new uint8_t[codeSize];
	memset(pCodeBuffer, 0, codeSize);

	DWORD readBytes = 0;
	if (!ReadFile(hFile, pCodeBuffer, fileSize, &readBytes, nullptr))
	{
		swprintf_s(wchTxt, L"Failed to Read File : %s\n", wchFileName);
		OutputDebugStringW(wchTxt);
		goto lb_close_return;
	}
	FILETIME createTime, lastAccessTime, lastWriteTime;

	GetFileTime(hFile, &createTime, &lastAccessTime, &lastWriteTime);

	SYSTEMTIME sysLastWriteTime;
	FileTimeToSystemTime(&lastWriteTime, &sysLastWriteTime);

	*ppOutCodeBuffer = pCodeBuffer;
	*outCodeSize = codeSize;
	*pOutLastWriteTime = sysLastWriteTime;
	bResult = TRUE;

lb_close_return:
	CloseHandle(hFile);

lb_return:
	return bResult;
}

void DeleteShaderCode(uint8_t* pCodeBuffer)
{
	delete[] pCodeBuffer;
}

HRESULT CompileShaderFromFileWithDXC(
	IDxcLibrary* pLibrary,
	IDxcCompiler* pCompiler,
	IDxcIncludeHandler* pIncludeHandler,
	const WCHAR* wchFileName,
	const WCHAR* wchEntryPoint,
	const WCHAR* wchShaderModel,
	IDxcBlob** ppOutCodeBlob,
	bool bDisableOptimize,
	SYSTEMTIME* outLastWriteTime,
	UINT flags)
{
	HRESULT hr = S_OK;

	SYSTEMTIME lastWriteTime;
	uint8_t* pCodeBuffer = nullptr;
	UINT codeSize = 0;

	if (!CreateShaderCodeFromFile(&pCodeBuffer, &codeSize, &lastWriteTime, wchFileName))
	{
		return E_FAIL;
	}

	UINT optimizeFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	if (bDisableOptimize)
	{
		// Enable better shader debugging with the graphics debugging tools.
		optimizeFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	}

	UINT shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
	shaderFlags |= optimizeFlags;

	/*
	{
		L"-Zpr",			//Row-major matrices
		L"-WX",				//Warnings as errors
#ifdef _DEBUG
		L"-Zi",				//Debug info
		L"-Qembed_debug",	//Embed debug info into the shader
		L"-Od",				//Disable optimization
#else
		L"-O3",				//Optimization level 3
#endif
	};
	*/

	LPCWSTR arg[16] = {};
	UINT argCount = 0;
	if (bDisableOptimize)
	{
		arg[argCount] = L"-Zi";
		argCount++;
		arg[argCount] = L"-Qembed_debug";
		argCount++;
		arg[argCount] = L"-Od";
		argCount++;
	}
	else
	{
		arg[argCount] = L"-O3";				//Optimization level 3
		argCount++;
	}

	IDxcBlobEncoding* pCodeTextBlob = nullptr;
	hr = pLibrary->CreateBlobWithEncodingFromPinned(pCodeBuffer, (UINT32)codeSize, CP_ACP, &pCodeTextBlob);
	ASSERT(SUCCEEDED(hr), "Failed to create blob from shader code.");

	IDxcOperationResult* pCompileResult = nullptr;
	hr = pCompiler->Compile(pCodeTextBlob, wchFileName, wchEntryPoint, wchShaderModel, arg, argCount, nullptr, 0, pIncludeHandler, &pCompileResult);

	HRESULT hrCompile;
	hr = pCompileResult->GetStatus(&hrCompile);

	if (SUCCEEDED(hrCompile))
	{
		pCompileResult->GetResult(ppOutCodeBlob);
	}
	else
	{
		IDxcBlobEncoding* pErrorBlob = nullptr;
		hr = pCompileResult->GetErrorBuffer(&pErrorBlob);
		const char* szErrMsg = (const char*)pErrorBlob->GetBufferPointer();
		WriteDebugStringA(DEBUG_OUTPUT_TYPE_DEBUG_CONSOLE, "Failed Compile Shader: %s\n", szErrMsg);
		if (pErrorBlob)
		{
			pErrorBlob->Release();
			pErrorBlob = nullptr;
		}
	}

	if (pCompileResult)
	{
		pCompileResult->Release();
		pCompileResult = nullptr;
	}
	if (pCodeTextBlob)
	{
		pCodeTextBlob->Release();
		pCodeTextBlob = nullptr;
	}

	DeleteShaderCode(pCodeBuffer);
	*outLastWriteTime = lastWriteTime;

	return hrCompile;
}