#include "pch.h"
#include "CommandListPool.h"

bool CommandListPool::Initialize(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, size_t maxNumCmdLists)
{
	ASSERT(maxNumCmdLists > 1, "At least two command lists must exist.");

	m_pD3DDevice = pDevice;
	m_MaxNumCmdList = maxNumCmdLists;
	m_CommnadListType = type;
	m_NumTotalCmdLists = 0;

	return true;
}

ID3D12GraphicsCommandList6* CommandListPool::GetCurrentCommandList()
{
	if (!m_pCurrCmdList)
	{
		m_pCurrCmdList = allocCmdList();
		ASSERT(m_pCurrCmdList, "No available command list.");
	}
	return m_pCurrCmdList->pDirectCommandList;
}

void CommandListPool::Close()
{
	ASSERT(m_pCurrCmdList, "No current command list.");
	ASSERT(!m_pCurrCmdList->bClosed, "The current command list is already closed.");

	HRESULT hr = m_pCurrCmdList->pDirectCommandList->Close();
	ASSERT(SUCCEEDED(hr), "Failed to close the current command list.");

	m_pCurrCmdList->bClosed = true;
	m_pCurrCmdList = nullptr;
}

void CommandListPool::CloseAndExecute(ID3D12CommandQueue* pCommandQueue)
{
	ASSERT(m_pCurrCmdList, "No current command list.");
	ASSERT(!m_pCurrCmdList->bClosed, "The current command list is already closed.");

	HRESULT hr = m_pCurrCmdList->pDirectCommandList->Close();
	ASSERT(SUCCEEDED(hr), "Failed to close the current command list.");

	m_pCurrCmdList->bClosed = true;

	ID3D12CommandList* ppCmdListArray[] = { m_pCurrCmdList->pDirectCommandList };
	pCommandQueue->ExecuteCommandLists(1, ppCmdListArray);

	m_pCurrCmdList = nullptr;
}

void CommandListPool::Reset()
{
	HRESULT hr = S_OK;
	while (!m_AllocatedCmdListArray.empty())
	{
		CommandList* pCmdList = m_AllocatedCmdListArray.front();
		m_AllocatedCmdListArray.pop_front();

		hr = pCmdList->pDirectCommandAllocator->Reset();
		ASSERT(SUCCEEDED(hr), "Failed to reset Command Allocator.");
		hr = pCmdList->pDirectCommandList->Reset(pCmdList->pDirectCommandAllocator, nullptr);
		ASSERT(SUCCEEDED(hr), "Failed to reset Command List.");

		pCmdList->bClosed = false;

		m_AvailableCmdListArray.emplace_back(pCmdList);
	}
}

void CommandListPool::Cleanup()
{
	Reset();

	while (!m_AvailableCmdListArray.empty())
	{
		CommandList* pCmdList = m_AvailableCmdListArray.front();
		m_AvailableCmdListArray.pop_front();

		ASSERT(pCmdList, "Available command list is null.");

		ASSERT(pCmdList->pDirectCommandList, "Available direct command list is null.");
		SAFE_RELEASE(pCmdList->pDirectCommandList);

		ASSERT(pCmdList->pDirectCommandAllocator, "Available command allocator is null.");
		SAFE_RELEASE(pCmdList->pDirectCommandAllocator);

		--m_NumTotalCmdLists;
		SAFE_DELETE(pCmdList);
	}
}

bool CommandListPool::addCmdList()
{
	HRESULT hr = S_OK;

	ID3D12CommandAllocator* pDirectCommandAllocator = nullptr;
	ID3D12GraphicsCommandList6* pDirectCommandList = nullptr;

	if (m_NumTotalCmdLists >= m_MaxNumCmdList)
	{
		ASSERT(false, "The maximum number of command lists has been reached.");
		return false;
	}

	hr = m_pD3DDevice->CreateCommandAllocator(m_CommnadListType, IID_PPV_ARGS(&pDirectCommandAllocator));
	if (FAILED(hr))
	{
		ASSERT(false, "Failed to create Command Allocator.");
		return false;
	}

	hr = m_pD3DDevice->CreateCommandList(0, m_CommnadListType, pDirectCommandAllocator, nullptr, IID_PPV_ARGS(&pDirectCommandList));
	if (FAILED(hr))
	{
		ASSERT(false, "Failed to create Command List.");
		SAFE_RELEASE(pDirectCommandAllocator);
		return false;
	}

	CommandList* pCmdList = new CommandList{};
	pCmdList->pDirectCommandAllocator = pDirectCommandAllocator;
	pCmdList->pDirectCommandList = pDirectCommandList;
	pCmdList->bClosed = false;

    m_AvailableCmdListArray.emplace_back(pCmdList);
    ++m_NumTotalCmdLists;
    return true;
}

CommandList* CommandListPool::allocCmdList()
{
	if (m_AvailableCmdListArray.empty() && !addCmdList())
	{
		return nullptr;
	}

	CommandList* pCmdList = m_AvailableCmdListArray.front();
	m_AvailableCmdListArray.pop_front();

	m_AllocatedCmdListArray.push_back(pCmdList);
	return pCmdList;
}