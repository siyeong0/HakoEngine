#pragma once

// ============================================================================
// C-style StaticMemoryPool (paged, global intrusive free list)
// ============================================================================

class StaticMemoryPool
{
public:
	// elementByteSize : size in bytes of user payload (e.g., sizeof(T))
	// capacity        : number of elements per page
	// alignment       : alignment of each element (e.g., alignof(T))
	StaticMemoryPool(size_t elementByteSize, size_t capacity, size_t alignment = alignof(std::max_align_t));
	~StaticMemoryPool();

	StaticMemoryPool(const StaticMemoryPool&) = delete;
	StaticMemoryPool& operator=(const StaticMemoryPool&) = delete;
	StaticMemoryPool(StaticMemoryPool&&) = delete;
	StaticMemoryPool& operator=(StaticMemoryPool&&) = delete;

	void* Alloc();
	void  Free(void* ptr);

	bool Owns(const void* ptr) const;

	size_t Capacity() const noexcept { return m_PageCount * m_ElementsPerPage; }
	size_t Size() const noexcept { return Capacity() - m_TotalFreeCount; }

	bool IsEmpty() const noexcept { return Size() == 0; }
	bool IsFull()  const noexcept { return m_TotalFreeCount == 0; }

	size_t GetElementByteSize() const noexcept { return m_ElementByteSize; }
	size_t GetElementsPerPage() const noexcept { return m_ElementsPerPage; }

private:
	struct Page
	{
		uint8_t* Buffer; // Raw storage: slotStride * elementsPerPage
	};

	// Stored at the beginning of each slot, representing "next free node"
	struct SlotHeader
	{
		uint16_t PageIndex; // 0xFFFF if none
		uint16_t SlotIndex; // 0xFFFF if none
	};

private:
	Page* allocateNewPage();
	void   ensurePageArrayCapacity();
	uint8_t* getSlotBase(Page* page, size_t slotIndex) const;
	bool     ownsInPage(const Page* page, const void* payloadPtr) const;

private:
	size_t   m_ElementByteSize;
	size_t   m_Alignment;
	size_t   m_ElementsPerPage;

	size_t   m_HeaderSize;
	size_t   m_SlotStride;

	Page** m_Pages;
	size_t   m_PageCount;
	size_t   m_PageCapacity;

	SlotHeader m_FreeHead;       // Head of the global free-list
	size_t   m_TotalFreeCount; // Total number of free slots across all pages
};
