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

	bool Initiailze(ID3D12Device5* pD3DDevice, uint shaderRecordSize, const WCHAR* wchResourceName);
	void Cleanup();
	uint CommitResource(uint maxNumShaderRecords);
	bool  InsertShaderRecord(const ShaderRecord* pShaderRecord);

	ID3D12Resource* GetResource() const { return m_pResource; }
	uint GetShaderRecordSize() const { return m_ShaderRecordSize; }
	uint GetShaderRecordNum() const { return m_CurrShaderRecordNum; }
	uint GetMaxShaderRecordNum() const { return m_MaxNumShaderRecords; }
	uint GetHitGroupShaderTableSize() const { return (m_ShaderRecordSize * m_CurrShaderRecordNum); }

private:
	ID3D12Device5* m_pD3DDevice = nullptr;
	ID3D12Resource* m_pResource = nullptr;
	uint8_t* m_pMappedPtr = nullptr;
	uint8_t* m_pCurrWritePtr = nullptr;
	uint m_ShaderRecordSize = 0;
	uint m_MaxNumShaderRecords = 0;
	uint m_CurrShaderRecordNum = 0;
	WCHAR m_wchResourceName[128] = {};
};


