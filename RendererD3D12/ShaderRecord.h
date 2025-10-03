#pragma once
#include <cstdint>
#include <memory>

struct PointerWithSize
{
	void* Ptr;
	size_t Size;

	PointerWithSize() : Ptr(nullptr), Size(0) {}
	PointerWithSize(void* _ptr, size_t _size) : Ptr(_ptr), Size(_size) {};
};

struct ShaderRecord
{
	PointerWithSize ShaderIdentifier;
	PointerWithSize LocalRootArguments;

	ShaderRecord(void* pShaderIdentifier, size_t shaderIdentifierSize) 
		: ShaderIdentifier(pShaderIdentifier, shaderIdentifierSize)
	{

	}

	ShaderRecord(void* pShaderIdentifier, size_t shaderIdentifierSize, void* pLocalRootArguments, size_t localRootArgumentsSize) 
		: ShaderIdentifier(pShaderIdentifier, shaderIdentifierSize)
		, LocalRootArguments(pLocalRootArguments, localRootArgumentsSize)
	{

	}

	void CopyTo(void* dest) const
	{
		uint8_t* byteDest = static_cast<uint8_t*>(dest);
		memcpy(byteDest, ShaderIdentifier.Ptr, ShaderIdentifier.Size);
		ASSERT(LocalRootArguments.Ptr && LocalRootArguments.Size > 0);
		memcpy(byteDest + ShaderIdentifier.Size, LocalRootArguments.Ptr, LocalRootArguments.Size);
	}
};