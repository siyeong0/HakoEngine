#pragma once
#include <cstdint>

class CIndexCreator
{
	uint32_t* m_pdwIndexTable = nullptr;
	uint32_t m_dwMaxNum = 0;
	uint32_t m_dwAllocatedCount = 0;
public:
	bool Initialize(uint32_t dwNum);

	uint32_t Alloc();
	void Free(uint32_t dwIndex);
	void Cleanup();
	void Check();

	CIndexCreator();
	~CIndexCreator();
};
