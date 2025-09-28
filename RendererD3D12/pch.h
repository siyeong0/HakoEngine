// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <initguid.h>

// d3d
#pragma warning(push)
#pragma warning(disable : 26827)
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <dxcapi.h>
#include <directx/d3dx12.h>
#include <DirectXMath.h>
#pragma warning(pop)

#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>

#include "Renderer_typedef.h"
#include "Interface/IRenderer.h"
#include "Common/Common.h"
#include "Util/IndexCreator.h"

#include "D3DUtil.h"

#endif //PCH_H
