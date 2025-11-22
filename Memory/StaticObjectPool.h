#pragma once
#include "StaticMemoryPool.h"

template <typename T>
class StaticObjectPool
{
public:
	StaticObjectPool(size_t capacity)
		: m_Pool(sizeof(T), capacity, alignof(T))
	{

	}

	~StaticObjectPool()
	{

	}

	StaticObjectPool(const StaticObjectPool&) = delete;
	StaticObjectPool& operator=(const StaticObjectPool&) = delete;

	// Create a T object in the pool using placement-new.
	template <typename... Args>
	T* Create(Args&&... args)
	{
		void* mem = m_Pool.Alloc();
		ASSERT(mem != nullptr, "StaticObjectPool out of memory");
		if (!mem)
		{
			return nullptr;
		}

		// Construct T in-place
		return new (mem) T(std::forward<Args>(args)...);
	}

	// Destroy the object and return the memory back to the pool.
	void Destroy(T* obj)
	{
		ASSERT(!obj, "StaticObjectPool::Destroy called with nullptr");
		if (!obj)
		{
			return;
		}

		obj->~T();
		m_Pool.Free(obj);
	}

	// Optional: typed wrappers around the underlying pool API
	bool Owns(const T* ptr) const { return m_Pool.Owns(ptr); }

	size_t Capacity() const noexcept { return m_Pool.Capacity(); }
	size_t Size() const noexcept { return m_Pool.Size(); }

	bool   IsEmpty() const noexcept { return m_Pool.IsEmpty(); }
	bool   IsFull() const noexcept { return m_Pool.IsFull(); }

	// If you ever need raw access
	const StaticMemoryPool& RawPool() const { return m_Pool; }

private:
	StaticMemoryPool m_Pool;
};
