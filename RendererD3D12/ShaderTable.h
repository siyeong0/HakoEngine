#pragma once

struct ShaderRecord;

// Shader record = {{Shader ID}, {RootArguments}}
// Shader table = {{ ShaderRecord 1}, {ShaderRecord 2}, ...}

class D3DResourceRecycleBin;
class ShaderTable
{
public:
	ShaderTable() = default;
	~ShaderTable() { Cleanup(); }

	bool Initiailze(ID3D12Device5* pD3DDevice, UINT shaderRecordSize, const WCHAR* wchResourceName);
	void Cleanup();
	UINT CommitResource(UINT maxNumShaderRecords);
	bool  InsertShaderRecord(const ShaderRecord* pShaderRecord);

	ID3D12Resource* GetResource() const { return m_pResource; }
	UINT GetShaderRecordSize() const { return m_ShaderRecordSize; }
	UINT GetShaderRecordNum() const { return m_CurrShaderRecordNum; }
	UINT GetMaxShaderRecordNum() const { return m_MaxNumShaderRecords; }
	UINT GetHitGroupShaderTableSize() const { return (m_ShaderRecordSize * m_CurrShaderRecordNum); }

private:
	ID3D12Device5* m_pD3DDevice = nullptr;
	ID3D12Resource* m_pResource = nullptr;
	uint8_t* m_pMappedPtr = nullptr;
	uint8_t* m_pCurrWritePtr = nullptr;
	UINT m_ShaderRecordSize = 0;
	UINT m_MaxNumShaderRecords = 0;
	UINT m_CurrShaderRecordNum = 0;
	WCHAR m_wchResourceName[128] = {};
};


