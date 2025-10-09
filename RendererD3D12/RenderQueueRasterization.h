#pragma once
#include "IRenderQueue.h"

class RenderQueueRasterization : public IRenderQueue
{	
public:
	RenderQueueRasterization() = default;
	~RenderQueueRasterization() { Cleanup(); };

	bool Initialize(D3D12Renderer* pRenderer, int maxNumItems) override;
	bool Add(const RenderItem* pItem) override;
	int Process(int threadIndex, 
		CommandListPool* pCommandListPool,
		ID3D12CommandQueue* pCommandQueue, 
		int numProcessPerCmdList, 
		D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		D3D12_CPU_DESCRIPTOR_HANDLE dsv, 
		const D3D12_VIEWPORT* pViewport, 
		const D3D12_RECT* pScissorRect) override;
	void Reset() override;
	void Cleanup() override;

private:
	const RenderItem* dispatch();

private:
	D3D12Renderer* m_pRenderer = nullptr;
	char* m_pBuffer = nullptr;
	uint m_MaxBufferSize = 0;
	uint m_AllocatedSize = 0;
	uint m_ReadBufferPos = 0;
	uint m_ItemCount = 0;
};