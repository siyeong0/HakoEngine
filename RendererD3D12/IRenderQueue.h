#pragma once
#include <d3d12.h>

class CommandListPool;
class D3D12Renderer;
struct RenderItem;

interface IRenderQueue
{
	virtual ~IRenderQueue() = default;

	virtual bool Initialize(D3D12Renderer* pRenderer, int maxNumItems) = 0;
	virtual void Cleanup() = 0;
	virtual bool Add(const RenderItem* pItem) = 0;
	virtual int Process(int threadIndex,
		CommandListPool* pCommandListPool,
		ID3D12CommandQueue* pCommandQueue,
		int numProcessPerCmdList,
		D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		D3D12_CPU_DESCRIPTOR_HANDLE dsv,
		const D3D12_VIEWPORT* pViewport,
		const D3D12_RECT* pScissorRect) = 0;
	virtual void Reset() = 0;
};