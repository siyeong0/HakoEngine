#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct CB_SkyMatrices
{
	XMFLOAT4X4 InvProj;
	XMFLOAT4X4 InvView; // ȸ������ inverse view(= camera rotation matrix)
};

struct CB_SkyParams
{
	XMFLOAT3 SunDirW;
	float pad1;
};
