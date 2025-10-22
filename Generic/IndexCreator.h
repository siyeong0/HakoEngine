#pragma once

class CIndexCreator
{
public:
	bool Initialize(uint32_t dwNum);

	uint32_t Alloc();
	void Free(uint32_t dwIndex);
	void Cleanup();
	void Check();

	CIndexCreator();
	~CIndexCreator();
private:
	uint32_t* m_pdwIndexTable = nullptr;
	uint32_t m_dwMaxNum = 0;
	uint32_t m_dwAllocatedCount = 0;
};
