#pragma once

enum RenderItemType
{
	RENDER_ITEM_TYPE_MESH_OBJ,
	RENDER_ITEM_TYPE_SPRITE
};

struct RenderObjectParam
{
	XMMATRIX matWorld;
};

struct RenderSpriteParam
{
	int iPosX;
	int iPosY;
	float fScaleX;
	float fScaleY;
	RECT Rect;
	BOOL bUseRect;
	float Z;
	void* pTexHandle;
};

struct RenderItem
{
	RenderItemType Type;
	void* pObjHandle;
	union
	{
		RenderObjectParam	MeshObjParam;
		RenderSpriteParam	SpriteParam;
	};
};

class CommandListPool;
class D3D12Renderer;

class RenderQueue
{	
public:
	RenderQueue() = default;
	~RenderQueue() { Cleanup(); };

	bool Initialize(D3D12Renderer* pRenderer, int maxNumItems);
	bool Add(const RenderItem* pItem);
	int Process(int threadIndex, CommandListPool* pCommandListPool, ID3D12CommandQueue* pCommandQueue, int numProcessPerCmdList, 
		D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv, const D3D12_VIEWPORT* pViewport, const D3D12_RECT* pScissorRect);
	void Reset();
	void Cleanup();

private:
	const RenderItem* dispatch();

private:
	D3D12Renderer* m_pRenderer = nullptr;
	char* m_pBuffer = nullptr;
	UINT m_MaxBufferSize = 0;
	UINT m_AllocatedSize = 0;
	UINT m_ReadBufferPos = 0;
	UINT m_ItemCount = 0;
};