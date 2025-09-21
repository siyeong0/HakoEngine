#pragma once

struct CommandList
{
	ID3D12CommandAllocator* pDirectCommandAllocator;
	ID3D12GraphicsCommandList* pDirectCommandList;
	SORT_LINK Link;
	bool bClosed;
};

class CommandListPool
{
public:
	CommandListPool() = default;
	~CommandListPool() { Cleanup(); };

	bool Initialize(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, int maxNumCmdLists);
	ID3D12GraphicsCommandList* GetCurrentCommandList();
	void Close();
	void CloseAndExecute(ID3D12CommandQueue* pCommandQueue);
	void Reset();
	void Cleanup();

	uint32_t GetTotalNumCmdList() const { return m_NumTotalCmdList; }
	uint32_t GetNumAllocatedCmdList() const { return m_NumAllocatedCmdList; }
	uint32_t GetAvailableNumCmdList() const { return m_NumAvailableCmdList; }
	ID3D12Device* GetD3DDevice() { return m_pD3DDevice; }

private:
	bool addCmdList();
	CommandList* allocCmdList();

private:
	ID3D12Device* m_pD3DDevice = nullptr;
	D3D12_COMMAND_LIST_TYPE	m_CommnadListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
	uint32_t m_NumAllocatedCmdList = 0;
	uint32_t m_NumAvailableCmdList = 0;
	uint32_t m_NumTotalCmdList = 0;
	uint32_t m_MaxNumCmdList = 0;
	CommandList* m_pCurrCmdList = nullptr;
	SORT_LINK* m_pAlloatedCmdLinkHead = nullptr;
	SORT_LINK* m_pAlloatedCmdLinkTail = nullptr;
	SORT_LINK* m_pAvailableCmdLinkHead = nullptr;
	SORT_LINK* m_pAvailableCmdLinkTail = nullptr;
};

