#include <Windows.h>
#include "Common.h"
#include "ProcessorInfo.h"

typedef bool(WINAPI* LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);
uint CountSetBits(ULONG_PTR bitMask);

bool GetPhysicalCoreCount(uint* pOutPhysicalCoreCount, uint* pOutLogicalCoreCount)
{
	bool bResult = FALSE;
	{
		LPFN_GLPI glpi;

		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION pBuffer = nullptr;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = nullptr;
		DWORD returnLength = 0;
		DWORD logicalProcessorCount = 0;
		DWORD numaNodeCount = 0;
		DWORD processorCoreCount = 0;
		DWORD processorL1CacheCount = 0;
		DWORD processorL2CacheCount = 0;
		DWORD processorL3CacheCount = 0;
		DWORD processorPackageCount = 0;
		DWORD byteOffset = 0;
		PCACHE_DESCRIPTOR Cache;

#pragma warning(suppress: 6387)
		glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");

		if (nullptr == glpi)
		{
			SYSTEM_INFO systemInfo;
			GetSystemInfo(&systemInfo);
			*pOutPhysicalCoreCount = systemInfo.dwNumberOfProcessors;
			*pOutLogicalCoreCount = systemInfo.dwNumberOfProcessors;

			OutputDebugStringW(L"\nGetLogicalProcessorInformation is not supported.\n");
			goto lb_return;
		}

		bool done = false;
		while (!done)
		{
			DWORD rc = glpi(pBuffer, &returnLength);

			if (rc == 0)
			{
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				{
					SAFE_FREE(pBuffer);
					pBuffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength);
				}
				else
				{
					break;
				}
			}
			else
			{
				done = true;
			}
		}

		ptr = pBuffer;

		while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
		{
			ASSERT(ptr, "Error: Unable to allocate memory for logical processor information.\n");
			switch (ptr->Relationship)
			{
			case RelationNumaNode:
				// Non-NUMA systems report a single record of this type.
				numaNodeCount++;
				break;

			case RelationProcessorCore:
				processorCoreCount++;

				// A hyperthreaded core supplies more than one logical processor.
				logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
				break;

			case RelationCache:
				// Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
				Cache = &ptr->Cache;
				if (Cache->Level == 1)
				{
					processorL1CacheCount++;
				}
				else if (Cache->Level == 2)
				{
					processorL2CacheCount++;
				}
				else if (Cache->Level == 3)
				{
					processorL3CacheCount++;
				}
				break;

			case RelationProcessorPackage:
				// Logical processors share a physical package.
				processorPackageCount++;
				break;

			default:
				OutputDebugStringW(L"\nError: Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n");
				break;
			}
			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			ptr++;
		}
		*pOutPhysicalCoreCount = processorCoreCount;
		*pOutLogicalCoreCount = logicalProcessorCount;
		//numaNodeCount;
		//processorPackageCount;
		//processorCoreCount;
		//logicalProcessorCount;
		//processorL1CacheCount;
		//processorL2CacheCount;
		//processorL3CacheCount

		SAFE_FREE(pBuffer);
		bResult = true;
	}
lb_return:
	return bResult;
}

// Helper function to count set bits in the processor mask.
uint CountSetBits(ULONG_PTR bitMask)
{
	DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
	DWORD bitSetCount = 0;
	ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
	DWORD i;

	for (i = 0; i <= LSHIFT; ++i)
	{
		bitSetCount += ((bitMask & bitTest) ? 1 : 0);
		bitTest /= 2;
	}

	return bitSetCount;
}
