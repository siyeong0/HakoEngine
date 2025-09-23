#pragma once
#include "Common/Common.h"

DWORD CreateBoxMesh(BasicVertex** ppOutVertexList, WORD* pOutIndexList, DWORD dwMaxBufferCount, float fHalfBoxLen);
void DeleteBoxMesh(BasicVertex* pVertexList);