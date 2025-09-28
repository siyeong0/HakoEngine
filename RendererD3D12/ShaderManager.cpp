#include "pch.h"
#include "Util/WriteDebugString.h"
#include "ShaderHandle.h"
#include "ShaderUtil.h"
#include "ShaderManager.h"

typedef DXC_API_IMPORT HRESULT(__stdcall* DxcCreateInstanceT)(_In_ REFCLSID rclsid, _In_ REFIID riid, _Out_ LPVOID* ppv);

bool ShaderManager::Initialize(D3D12Renderer* pRenderer, const WCHAR* wchShaderPath, bool bDisableOptimize)
{
	bool bResult = false;

	m_bDisableOptimize = bDisableOptimize;

	wcscpy_s(m_wchDefaultShaderPath, _MAX_PATH, wchShaderPath);

	// Initialize DXC.
	const WCHAR* wchDllPath = nullptr;
#if defined(_M_ARM64EC)
	wchDllPath = L"./Dxc/arm64";
#elif defined(_M_ARM64)
	wchDllPath = L"./Dxc/arm64";
#elif defined(_M_AMD64)
	wchDllPath = L"./Dxc/x64";
#elif defined(_M_IX86)
	wchDllPath = L"./Dxc/x86";
#endif
	WCHAR wchOldPath[_MAX_PATH];
	GetCurrentDirectoryW(_MAX_PATH, wchOldPath);
	SetCurrentDirectoryW(wchDllPath);

	m_hDXL = LoadLibrary(L"dxcompiler.dll");
	if (!m_hDXL)
	{
		goto lb_return;
	}

	DxcCreateInstanceT	DxcCreateInstanceFunc = (DxcCreateInstanceT)GetProcAddress(m_hDXL, "DxcCreateInstance");

	HRESULT hr = DxcCreateInstanceFunc(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pLibrary));
	ASSERT(SUCCEEDED(hr), "Failed to create DXC library instance.");

	hr = DxcCreateInstanceFunc(CLSID_DxcCompiler, IID_PPV_ARGS(&m_pCompiler));
	ASSERT(SUCCEEDED(hr), "Failed to create DXC compiler instance.");

	m_pLibrary->CreateIncludeHandler(&m_pIncludeHandler);

	bResult = true;
lb_return:
	SetCurrentDirectoryW(wchOldPath);
	return bResult;
}

ShaderHandle* ShaderManager::CreateShaderDXC(const WCHAR* wchShaderFileName, const WCHAR* wchEntryPoint, const WCHAR* wchShaderModel, UINT flags)
{
	bool bResult = false;

	SYSTEMTIME	creationTime = {};
	ShaderHandle* newShaderHandle = nullptr;

	WCHAR wchOldPath[MAX_PATH];
	GetCurrentDirectory(_MAX_PATH, wchOldPath);

	IDxcBlob* pBlob = nullptr;

	// case DXIL::ShaderKind::Vertex:    entry = L"VSMain"; profile = L"vs_6_1"; break;
	// case DXIL::ShaderKind::Pixel:     entry = L"PSMain"; profile = L"ps_6_1"; break;
	// case DXIL::ShaderKind::Geometry:  entry = L"GSMain"; profile = L"gs_6_1"; break;
	// case DXIL::ShaderKind::Hull:      entry = L"HSMain"; profile = L"hs_6_1"; break;
	// case DXIL::ShaderKind::Domain:    entry = L"DSMain"; profile = L"ds_6_1"; break;
	// case DXIL::ShaderKind::Compute:   entry = L"CSMain"; profile = L"cs_6_1"; break;
	// case DXIL::ShaderKind::Mesh:      entry = L"MSMain"; profile = L"ms_6_5"; break;
	// case DXIL::ShaderKind::Amplification: entry = L"ASMain"; profile = L"as_6_5"; break;

	//"vs_6_0"
	//"ps_6_0"
	//"cs_6_0"
	//"gs_6_0"
	//"ms_6_5"
	//"as_6_5"
	//"hs_6_0"
	//"lib_6_3"

	SetCurrentDirectory(m_wchDefaultShaderPath);
	HRESULT	hr = CompileShaderFromFileWithDXC(
		m_pLibrary,
		m_pCompiler,
		m_pIncludeHandler,
		wchShaderFileName,
		wchEntryPoint,
		wchShaderModel,
		&pBlob,
		m_bDisableOptimize,
		&creationTime, 0);
	if (FAILED(hr))
	{
		WriteDebugStringW(DEBUG_OUTPUT_TYPE_DEBUG_CONSOLE, L"Failed to compile shader : %s-%s\n", wchShaderFileName, wchEntryPoint);
		goto lb_exit;
	}
	size_t codeSize = pBlob->GetBufferSize();
	const char* pCodeBuffer = (const char*)pBlob->GetBufferPointer();

	size_t shaderHandleSize = sizeof(ShaderHandle) - sizeof(DWORD) + codeSize;
	newShaderHandle = (ShaderHandle*)malloc(shaderHandleSize);
	memset(newShaderHandle, 0, shaderHandleSize);

	memcpy(newShaderHandle->CodeBuffer, pCodeBuffer, codeSize);
	newShaderHandle->CodeSize = static_cast<UINT>(codeSize);
	newShaderHandle->ShaderNameLen = swprintf_s(newShaderHandle->wchShaderName, L"%s-%s", wchShaderFileName, wchEntryPoint);
	bResult = true;

lb_exit:
	if (pBlob)
	{
		pBlob->Release();
		pBlob = nullptr;
	}
	SetCurrentDirectory(wchOldPath);

	return newShaderHandle;
}

void ShaderManager::ReleaseShader(ShaderHandle* pShaderHandle)
{
	free(pShaderHandle);
}

void ShaderManager::Cleanup()
{
	// Release DXC.
	if (m_pIncludeHandler)
	{
		m_pIncludeHandler->Release();
		m_pIncludeHandler = nullptr;
	}
	if (m_pCompiler)
	{
		m_pCompiler->Release();
		m_pCompiler = nullptr;
	}
	if (m_pLibrary)
	{
		m_pLibrary->Release();
		m_pLibrary = nullptr;
	}
	if (m_hDXL)
	{
		FreeLibrary(m_hDXL);
		m_hDXL = nullptr;
	}
}