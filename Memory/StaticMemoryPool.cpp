#include "pch.h"
#include "StaticMemoryPool.h"

namespace
{
	constexpr uint16_t INVALID_INDEX = 0xFFFF;

	inline size_t AlignUp(size_t value, size_t alignment)
	{
		ASSERT(alignment > 0, "alignment must be greater than zero");
		return (value + alignment - 1) / alignment * alignment;
	}
}

// ============================================================================
// StaticMemoryPool (paged, global intrusive free list)
// ============================================================================

StaticMemoryPool::StaticMemoryPool(size_t elementByteSize, size_t capacity, size_t alignment)
	: m_ElementByteSize(elementByteSize)
	, m_Alignment(alignment)
	, m_ElementsPerPage(capacity)
	, m_HeaderSize(0)
	, m_SlotStride(0)
	, m_Pages(nullptr)
	, m_PageCount(0)
	, m_PageCapacity(0)
	, m_FreeHead{ INVALID_INDEX, INVALID_INDEX }
	, m_TotalFreeCount(0)
{
	ASSERT(m_ElementByteSize > 0, "elementSize must be greater than zero");
	ASSERT(m_ElementsPerPage > 0, "capacity (elementsPerPage) must be greater than zero");
	ASSERT(m_ElementsPerPage <= std::numeric_limits<uint16_t>::max(), "elementsPerPage must fit in uint16_t");
	ASSERT(m_Alignment > 0, "alignment must be greater than zero");

	// Header stores: uint16_t NextPageIndex + uint16_t NextSlotIndex
	const size_t rawHeaderSize = sizeof(SlotHeader);

	// We want the user payload (after the header) to satisfy the requested alignment.
	// So we pad the header size up to 'alignment'.
	m_HeaderSize = AlignUp(rawHeaderSize, m_Alignment);

	// Slot layout: [header (padded)] [payload]
	m_SlotStride = m_HeaderSize + m_ElementByteSize;

	// Make sure each slot stride also respects alignment (for subsequent slots).
	ASSERT((m_SlotStride % m_Alignment) == 0, "slotStride must be a multiple of alignment");

	static_assert(__cpp_aligned_new, "C++17 aligned new is required for aligned allocation");

	// Allocate the initial page so the pool is ready to use immediately.
	Page* firstPage = allocateNewPage();
	ASSERT(firstPage != nullptr, "Failed to allocate initial page for StaticMemoryPool");
}

StaticMemoryPool::~StaticMemoryPool()
{
	// We intentionally do NOT invoke destructors for objects that may still
	// reside in this pool.
	// This pool has no knowledge of the object type, so lifetime management
	// must be handled by higher-level code.

	if (m_Pages)
	{
		for (size_t i = 0; i < m_PageCount; ++i)
		{
			Page* page = m_Pages[i];
			if (!page)
				continue;

			::operator delete[](page->Buffer, std::align_val_t(m_Alignment));
			page->Buffer = nullptr;

			delete page;
		}

		delete[] m_Pages;
	}

	m_Pages = nullptr;
	m_PageCount = 0;
	m_PageCapacity = 0;
	m_FreeHead = { INVALID_INDEX, INVALID_INDEX };
	m_TotalFreeCount = 0;

	m_ElementByteSize = 0;
	m_Alignment = 0;
	m_ElementsPerPage = 0;
	m_HeaderSize = 0;
	m_SlotStride = 0;
}

void* StaticMemoryPool::Alloc()
{
	// If there are no free slots left across all pages, allocate a new page.
	if (m_TotalFreeCount == 0)
	{
		Page* newPage = allocateNewPage();
		if (!newPage)
		{
			// Allocation failed (out of memory, etc.)
			return nullptr;
		}
	}

	ASSERT(m_FreeHead.PageIndex != INVALID_INDEX, "FreeHead must be valid when TotalFreeCount > 0");
	ASSERT(m_FreeHead.SlotIndex != INVALID_INDEX, "FreeHead must be valid when TotalFreeCount > 0");
	ASSERT(m_FreeHead.PageIndex < m_PageCount, "FreeHead page index out of range");

	Page* page = m_Pages[m_FreeHead.PageIndex];

	// Compute the slot base and header/payload pointers.
	uint8_t* slotBase = getSlotBase(page, m_FreeHead.SlotIndex);
	SlotHeader* header = reinterpret_cast<SlotHeader*>(slotBase);
	uint8_t* payloadPtr = slotBase + m_HeaderSize;

	// Pop from the global free list:
	SlotHeader nextHead;
	std::memcpy(&nextHead, header, sizeof(SlotHeader));

	m_FreeHead = nextHead;

	--m_TotalFreeCount;

	return static_cast<void*>(payloadPtr);
}

void StaticMemoryPool::Free(void* ptr)
{
	ASSERT(ptr != nullptr, "Cannot deallocate a null pointer");
	if (!ptr)
	{
		return;
	}

	const uint8_t* payloadPtr = static_cast<const uint8_t*>(ptr);

	// Find which page this pointer belongs to.
	uint16_t pageIndex = INVALID_INDEX;
	Page* page = nullptr;

	for (size_t i = 0; i < m_PageCount; ++i)
	{
		Page* p = m_Pages[i];
		if (!p)
			continue;

		const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(p->Buffer);
		const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(payloadPtr);
		const std::uintptr_t end = base + (m_SlotStride * m_ElementsPerPage);

		if (addr >= base && addr < end)
		{
			pageIndex = static_cast<uint16_t>(i);
			page = p;
			break;
		}
	}

	ASSERT(pageIndex != INVALID_INDEX && page != nullptr, "Pointer does not belong to this StaticMemoryPool");

	const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(page->Buffer);
	const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(payloadPtr);

	ASSERT(addr >= base + m_HeaderSize, "Payload pointer must be after header");
	const size_t offsetBytes = static_cast<size_t>(addr - base);

	ASSERT(offsetBytes >= m_HeaderSize, "Offset must be >= header size");
	const size_t payloadOffsetInPage = offsetBytes - m_HeaderSize;

	ASSERT((payloadOffsetInPage % m_SlotStride) == 0, "Pointer is not aligned to payload start of a slot");
	const size_t slotIndexSizeT = payloadOffsetInPage / m_SlotStride;
	ASSERT(slotIndexSizeT < m_ElementsPerPage, "Slot index out of bounds");

	const uint16_t slotIndex = static_cast<uint16_t>(slotIndexSizeT);

	// Now we can access the slot header.
	uint8_t* slotBase = getSlotBase(page, slotIndex);
	SlotHeader* header = reinterpret_cast<SlotHeader*>(slotBase);

	// Push this slot to the front of the global free list.
	std::memcpy(header, &m_FreeHead, sizeof(SlotHeader));

	m_FreeHead.PageIndex = pageIndex;
	m_FreeHead.SlotIndex = slotIndex;

	++m_TotalFreeCount;
}

bool StaticMemoryPool::Owns(const void* ptr) const
{
	if (!ptr || !m_Pages)
	{
		return false;
	}

	const uint8_t* payloadPtr = static_cast<const uint8_t*>(ptr);

	for (size_t i = 0; i < m_PageCount; ++i)
	{
		const Page* page = m_Pages[i];
		if (!page)
			continue;

		if (ownsInPage(page, payloadPtr))
		{
			return true;
		}
	}

	return false;
}

// ============================================================================
// Private helpers
// ============================================================================

StaticMemoryPool::Page* StaticMemoryPool::allocateNewPage()
{
	ensurePageArrayCapacity();

	Page* page = new Page();
	page->Buffer = nullptr;

	// Allocate raw buffer for this page.
	const size_t pageByteSize = m_SlotStride * m_ElementsPerPage;

	page->Buffer = static_cast<uint8_t*>(::operator new[](pageByteSize, std::align_val_t(m_Alignment)));

	// This page will reside at index 'pageIndex' in m_Pages.
	const uint16_t pageIndex = static_cast<uint16_t>(m_PageCount);
	ASSERT(pageIndex < std::numeric_limits<uint16_t>::max(), "Page index overflow");

	// Initialize global free list nodes inside the page's slots.
	// We push each slot onto the global free list head in LIFO order.
	for (size_t i = 0; i < m_ElementsPerPage; ++i)
	{
		uint8_t* slotBase = getSlotBase(page, i);
		auto* header = reinterpret_cast<SlotHeader*>(slotBase);

		// New slot's "next" is the current head.
		std::memcpy(header, &m_FreeHead, sizeof(SlotHeader));

		// New slot becomes the head.
		m_FreeHead.PageIndex = pageIndex;
		m_FreeHead.SlotIndex = static_cast<uint16_t>(i);
	}

	// Every new slot added above is free.
	m_TotalFreeCount += m_ElementsPerPage;

	// Store the page pointer in the array.
	m_Pages[m_PageCount++] = page;

	return page;
}

void StaticMemoryPool::ensurePageArrayCapacity()
{
	if (m_PageCount < m_PageCapacity)
	{
		// There is still room for another page pointer.
		return;
	}

	// Grow the page pointer array.
	const size_t newCapacity = (m_PageCapacity == 0) ? 1 : (m_PageCapacity * 2);

	Page** newArray = new Page * [newCapacity];

	// Initialize new slots to nullptr.
	for (size_t i = 0; i < newCapacity; ++i)
	{
		newArray[i] = nullptr;
	}

	// Copy existing page pointers.
	for (size_t i = 0; i < m_PageCount; ++i)
	{
		newArray[i] = m_Pages[i];
	}

	delete[] m_Pages;
	m_Pages = newArray;
	m_PageCapacity = newCapacity;
}

uint8_t* StaticMemoryPool::getSlotBase(Page* page, size_t slotIndex) const
{
	ASSERT(page != nullptr, "page must not be null");
	ASSERT(slotIndex < m_ElementsPerPage, "slotIndex is out of bounds");

	return page->Buffer + slotIndex * m_SlotStride;
}

bool StaticMemoryPool::ownsInPage(const Page* page, const void* payloadPtr) const
{
	if (!page || !payloadPtr)
	{
		return false;
	}

	const uint8_t* payload = static_cast<const uint8_t*>(payloadPtr);

	const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(page->Buffer);
	const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(payload);
	const std::uintptr_t end = base + (m_SlotStride * m_ElementsPerPage);

	if (addr < base || addr >= end)
	{
		return false;
	}

	const size_t offsetBytes = static_cast<size_t>(addr - base);

	// Payload starts at (slotIndex * m_SlotStride + m_HeaderSize).
	if (offsetBytes < m_HeaderSize)
	{
		return false;
	}

	const size_t payloadOffsetInSlot = (offsetBytes - m_HeaderSize) % m_SlotStride;
	if (payloadOffsetInSlot != 0)
	{
		// Not exactly at the payload start of any slot.
		return false;
	}

	return true;
}
