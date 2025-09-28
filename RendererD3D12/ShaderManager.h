#pragma once
#include "ShaderHandle.h"

class D3D12Renderer;

class ShaderManager
{
public:
	ShaderManager() = default;
	~ShaderManager() { Cleanup(); };

	bool Initialize(D3D12Renderer* pRenderer, const WCHAR* wchShaderPath, bool bDisableOptimize);
	ShaderHandle* CreateShaderDXC(const WCHAR* wchShaderFileName, const WCHAR* wchEntryPoint, const WCHAR* wchShaderModel, UINT flags);
	void ReleaseShader(ShaderHandle* pShaderHandle);
	void Cleanup();

private:
	HMODULE m_hDXL = nullptr;
	IDxcLibrary* m_pLibrary = nullptr;
	IDxcCompiler* m_pCompiler = nullptr;
	IDxcIncludeHandler* m_pIncludeHandler = nullptr;
	bool m_bDisableOptimize = false;
	WCHAR m_wchDefaultShaderPath[_MAX_PATH] = {};
};