#include "pch.h"
#include "CommandListPool.h"

bool CommandListPool::Initialize(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, int maxNumCmdLists)
{
	ASSERT(maxNumCmdLists > 1, "At least two command lists must exist.");

	m_MaxNumCmdList = maxNumCmdLists;
	m_pD3DDevice = pDevice;

	return true;
}

ID3D12GraphicsCommandList6* CommandListPool::GetCurrentCommandList()
{
	ID3D12GraphicsCommandList6* pCommandList = nullptr;
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
	pCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&m_pCurrCmdList->pDirectCommandList);
	m_pCurrCmdList = nullptr;
}

void CommandListPool::Reset()
{
	HRESULT hr = S_OK;
	while (m_pAlloatedCmdLinkHead)
	{
		CommandList* pCmdList = (CommandList*)m_pAlloatedCmdLinkHead->pItem;

		hr = pCmdList->pDirectCommandAllocator->Reset();
		ASSERT(SUCCEEDED(hr), "Failed to reset Command Allocator.");
		hr = pCmdList->pDirectCommandList->Reset(pCmdList->pDirectCommandAllocator, nullptr);
		ASSERT(SUCCEEDED(hr), "Failed to reset Command Allocator.");
		pCmdList->bClosed = false;

		UnLinkFromLinkedList(&m_pAlloatedCmdLinkHead, &m_pAlloatedCmdLinkTail, &pCmdList->Link);
		m_NumAllocatedCmdList--;

		LinkToLinkedListFIFO(&m_pAvailableCmdLinkHead, &m_pAvailableCmdLinkTail, &pCmdList->Link);
		m_NumAvailableCmdList++;
	}
}

void CommandListPool::Cleanup()
{
	Reset();

	while (m_pAvailableCmdLinkHead)
	{
		CommandList* pCmdList = (CommandList*)m_pAvailableCmdLinkHead->pItem;
		pCmdList->pDirectCommandList->Release();
		pCmdList->pDirectCommandList = nullptr;

		pCmdList->pDirectCommandAllocator->Release();
		pCmdList->pDirectCommandAllocator = nullptr;
		m_NumTotalCmdList--;

		UnLinkFromLinkedList(&m_pAvailableCmdLinkHead, &m_pAvailableCmdLinkTail, &pCmdList->Link);
		m_NumAvailableCmdList--;

		delete pCmdList;
	}
}

bool CommandListPool::addCmdList()
{
	CommandList* pCmdList = nullptr;
	ID3D12CommandAllocator* pDirectCommandAllocator = nullptr;
	ID3D12GraphicsCommandList6* pDirectCommandList = nullptr;

	if (m_NumTotalCmdList >= m_MaxNumCmdList)
	{
		ASSERT(false, "The maximum number of command lists has been reached.");
		return false;
	}

	if (FAILED(m_pD3DDevice->CreateCommandAllocator(m_CommnadListType, IID_PPV_ARGS(&pDirectCommandAllocator))))
	{
		ASSERT(false, "Failed to create Command Allocator.");
		return false;
	}

	if (FAILED(m_pD3DDevice->CreateCommandList(0, m_CommnadListType, pDirectCommandAllocator, nullptr, IID_PPV_ARGS(&pDirectCommandList))))
	{
		ASSERT(false, "Failed to create Command List.");
		pDirectCommandAllocator->Release();
		pDirectCommandAllocator = nullptr;
		return false;

	}
	pCmdList = new CommandList;
	memset(pCmdList, 0, sizeof(CommandList));
	pCmdList->Link.pItem = pCmdList;
	pCmdList->pDirectCommandList = pDirectCommandList;
	pCmdList->pDirectCommandAllocator = pDirectCommandAllocator;
	m_NumTotalCmdList++;

	LinkToLinkedListFIFO(&m_pAvailableCmdLinkHead, &m_pAvailableCmdLinkTail, &pCmdList->Link);
	m_NumAvailableCmdList++;
	return true;
}

CommandList* CommandListPool::allocCmdList()
{
	if (!m_pAvailableCmdLinkHead && !addCmdList())
	{
		return nullptr;
		// ASSERT(m_pAvailableCmdLinkHead != nullptr, "No available command list.");
	}

	CommandList* pCmdList = (CommandList*)m_pAvailableCmdLinkHead->pItem;

	UnLinkFromLinkedList(&m_pAvailableCmdLinkHead, &m_pAvailableCmdLinkTail, &pCmdList->Link);
	m_NumAvailableCmdList--;

	LinkToLinkedListFIFO(&m_pAlloatedCmdLinkHead, &m_pAlloatedCmdLinkTail, &pCmdList->Link);
	m_NumAllocatedCmdList++;

	return pCmdList;
}