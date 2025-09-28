#pragma once

struct CommandList
{
	ID3D12CommandAllocator* pDirectCommandAllocator = nullptr;
	ID3D12GraphicsCommandList6* pDirectCommandList = nullptr;
	bool bClosed = false;
};

class CommandListPool
{
public:
	CommandListPool() = default;
	~CommandListPool() { Cleanup(); };

	bool Initialize(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, int maxNumCmdLists);
	ID3D12GraphicsCommandList6* GetCurrentCommandList();
	void Close();
	void CloseAndExecute(ID3D12CommandQueue* pCommandQueue);
	void Reset();
	void Cleanup();

	uint32_t GetTotalNumCmdList() const { return m_NumTotalCmdLists; }
	uint32_t GetNumAllocatedCmdList() const { return m_AllocatedCmdListArray.size(); }
	uint32_t GetAvailableNumCmdList() const { return m_AvailableCmdListArray.size(); }
	ID3D12Device* GetD3DDevice() { return m_pD3DDevice; }

private:
	bool addCmdList();
	CommandList* allocCmdList();

private:
	ID3D12Device* m_pD3DDevice = nullptr;
	D3D12_COMMAND_LIST_TYPE	m_CommnadListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

	uint32_t m_NumTotalCmdLists = 0;
	uint32_t m_MaxNumCmdList = 0;

	CommandList* m_pCurrCmdList = nullptr;
	
	std::list<CommandList*> m_AllocatedCmdListArray;
	std::list<CommandList*> m_AvailableCmdListArray;
};

