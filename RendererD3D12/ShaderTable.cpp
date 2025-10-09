#include "pch.h"
#include "ShaderRecord.h"
#include "ShaderTable.h"

bool ShaderTable::Initiailze(ID3D12Device5* pD3DDevice, uint shaderRecordSize, const WCHAR* wchResourceName)
{
	m_pD3DDevice = pD3DDevice;
	m_ShaderRecordSize = D3DUtil::Align(shaderRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	wcscpy_s(m_wchResourceName, wchResourceName);

	return true;
}

void ShaderTable::Cleanup()
{
	SAFE_RELEASE(m_pResource);
}

uint ShaderTable::CommitResource(uint maxNumShaderRecords)
{
	uint64_t memSize = maxNumShaderRecords * m_ShaderRecordSize;

	// free old resource
	SAFE_RELEASE(m_pResource);

	D3DUtil::CreateUploadBuffer(m_pD3DDevice, nullptr, memSize, &m_pResource, m_wchResourceName);
	ASSERT(m_pResource, "Failed to create shader table buffer");

	// We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	HRESULT hr = m_pResource->Map(0, &readRange, (void**)&m_pMappedPtr);
	ASSERT(SUCCEEDED(hr) && m_pMappedPtr, "Failed to map shader table buffer");

	m_CurrShaderRecordNum = 0;
	m_pCurrWritePtr = m_pMappedPtr;
	m_ShaderRecordSize = m_ShaderRecordSize;
	m_MaxNumShaderRecords = maxNumShaderRecords;

	return m_MaxNumShaderRecords;
}

bool ShaderTable::InsertShaderRecord(const ShaderRecord* pShaderRecord)
{
	ASSERT(m_CurrShaderRecordNum < m_MaxNumShaderRecords, "Too many shader records for shader table");

	uint8_t* byteDest = static_cast<uint8_t*>(m_pCurrWritePtr);
	memcpy(byteDest, pShaderRecord->ShaderIdentifier.Ptr, pShaderRecord->ShaderIdentifier.Size);
	if (pShaderRecord->LocalRootArguments.Ptr)
	{
		memcpy(byteDest + pShaderRecord->ShaderIdentifier.Size, pShaderRecord->LocalRootArguments.Ptr, pShaderRecord->LocalRootArguments.Size);
	}

	m_pCurrWritePtr += m_ShaderRecordSize;
	m_CurrShaderRecordNum++;

	return true;
}