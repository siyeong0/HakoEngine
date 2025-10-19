#pragma once

class D3D12Renderer;

class ShaderManager
{
public:
	ShaderManager() = default;
	~ShaderManager() { Cleanup(); };

	bool Initialize(D3D12Renderer* pRenderer, const wchar_t* wchShaderPath, bool bDisableOptimize);
	ShaderHandle* CreateShaderDXC(const wchar_t* wchShaderFileName, const wchar_t* wchEntryPoint, const wchar_t* wchShaderModel, uint flags);
	void ReleaseShader(ShaderHandle* pShaderHandle);
	void Cleanup();

private:
	HMODULE m_hDXL = nullptr;
	IDxcLibrary* m_pLibrary = nullptr;
	IDxcCompiler* m_pCompiler = nullptr;
	IDxcIncludeHandler* m_pIncludeHandler = nullptr;
	bool m_bDisableOptimize = false;
	wchar_t m_wchDefaultShaderPath[_MAX_PATH] = {};
};