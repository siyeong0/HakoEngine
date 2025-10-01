#include <iostream>
#include <vector>
#include <string>
#include <cassert>

#include <DirectXTex.h>
#pragma comment(lib, "DirectXTex.lib")

#include "Common/Common.h"
#include "Interface/IHfxBake.h" // hfx::PrecomputeAtmos

#include "Atmos.h"

static AtmosParams MakeEarthLikeParams();
static bool Save2DRGBasDDS(const float* srcRGB, int W, int H, const std::wstring& path);
static bool SaveScattering3D_RGBA_DDS(const float* srcRGBA, int R, int MU, int MU_S, int NU, const std::wstring& path);

// ---------------- 실행 샘플 ----------------
void RunAtmosPrecomputeAndSave()
{
	AtmosParams atmosParams = MakeEarthLikeParams();

	AtmosResult atmosResult{};
	if (!hfx::PrecomputeAtmos(atmosParams, &atmosResult))
	{
		std::cerr << "[Sample] PrecomputeAtmos failed.\n";
		return;
	}

	// 저장 경로
	const std::wstring tPath = L"../../Resources/Atmos/Transmittance.dds";
	const std::wstring sPath = L"../../Resources/Atmos/Scattering.dds";
	const std::wstring ePath = L"../../Resources/Atmos/Irradiance.dds";

	bool bOkT = Save2DRGBasDDS(atmosResult.TransmittanceRGB, atmosResult.TransmittanceW, atmosResult.TransmittanceH, tPath);

	bool bOkS = SaveScattering3D_RGBA_DDS(
		atmosResult.ScatteringRGBA,
		atmosResult.ScatteringR, atmosResult.ScatteringMu,
		atmosResult.ScatteringMuS, atmosResult.ScatteringNu,
		sPath);

	bool bOkE = Save2DRGBasDDS(atmosResult.IrradianceRGB, atmosResult.IrradianceW, atmosResult.IrradianceH, ePath);

	std::cout << "[Sample] Save DDS:"
		<< " T=" << (bOkT ? "OK" : "FAIL")
		<< " S=" << (bOkS ? "OK" : "FAIL")
		<< " E=" << (bOkE ? "OK" : "FAIL") << std::endl;

	// 결과 버퍼 해제 (현재 구현이 new[]라면)
	delete[] atmosResult.TransmittanceRGB;
	delete[] atmosResult.ScatteringRGBA;
	delete[] atmosResult.IrradianceRGB;
}

// ---------------- 샘플 파라미터 빌더 ----------------
static AtmosParams MakeEarthLikeParams()
{
	AtmosParams p{};
	p.PlanetRadius = 6'360'000.0f;
	p.AtmosphereHeight = 60'000.0f;   // TOA = 6,420km (샘플값)

	// Rayleigh (approx Earth)
	p.RayleighScattering = FLOAT3{ 5.802e-6f, 13.558e-6f, 33.1e-6f };
	p.Rayleigh.Layers[0] = DensityLayer{ p.AtmosphereHeight, 1.0f, 0.0f, 0.0f, 8'000.0f };
	p.Rayleigh.Layers[1] = DensityLayer{ 0,0,0,0,0 }; // unused

	// Mie (approx Earth haze)
	p.MieScattering = FLOAT3{ 3.996e-6f, 3.996e-6f, 3.996e-6f };
	p.MieExtinction = FLOAT3{ 4.4e-6f,   4.4e-6f,   4.4e-6f };
	p.Mie.Layers[0] = DensityLayer{ p.AtmosphereHeight, 1.0f, 0.0f, 0.0f, 1'200.0f };
	p.Mie.Layers[1] = DensityLayer{ 0,0,0,0,0 };
	p.MieG = 0.8f;

	// Ozone (간단화: 0으로 시작. 필요하면 추후 레이어 구성)
	p.OzoneAbsorption = FLOAT3{ 0.0f, 0.0f, 0.0f };
	p.Ozone.Layers[0] = DensityLayer{ 0,0,0,0,0 };
	p.Ozone.Layers[1] = DensityLayer{ 0,0,0,0,0 };

	p.GroundAlbedo = FLOAT3{ 0.1f, 0.1f, 0.1f };
	p.SolarIrradiance = FLOAT3{ 1.0f, 1.0f, 1.0f };   // 스케일 팩터
	p.SunAngularRadius = 0.004675f;                    // ≈0.267°

	// LUT sizes (Bruneton 스타일)
	p.TransmittanceW = 256; p.TransmittanceH = 64;
	p.ScatteringR = 32;  p.ScatteringMu = 128; p.ScatteringMuS = 32; p.ScatteringNu = 8;
	p.IrradianceW = 64;  p.IrradianceH = 16;

	p.MultipleScatteringOrders = 1; // 현재 CPU 레퍼런스는 단산란만
	return p;
}

// ---------------- DDS 저장 유틸 ----------------
// 2D RGB(float*) -> RGBA32F DDS
static bool Save2DRGBasDDS(const float* srcRGB, int W, int H, const std::wstring& path)
{
	using namespace DirectX;
	ScratchImage img;
	HRESULT hr = img.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, W, H, /*arraySize*/1, /*mips*/1);
	if (FAILED(hr))
	{
		return false;
	}

	const Image* im = img.GetImage(0, 0, 0);
	for (int y = 0; y < H; ++y)
	{
		float* row = reinterpret_cast<float*>(im->pixels + size_t(y) * im->rowPitch);
		for (int x = 0; x < W; ++x)
		{
			size_t s = (size_t(y) * W + x) * 3;
			row[x * 4 + 0] = srcRGB[s + 0];
			row[x * 4 + 1] = srcRGB[s + 1];
			row[x * 4 + 2] = srcRGB[s + 2];
			row[x * 4 + 3] = 1.0f;
		}
	}

	return SUCCEEDED(SaveToDDSFile(*im, DDS_FLAGS_NONE, path.c_str()));
}

// 3D RGBA(float*) -> DDS (Scattering: NU*MU_S × MU × R 로 패킹한 3D)
// srcRGBA의 인덱싱은: ((((r * MU + mu) * MU_S + mu_s) * NU + nu) * 4 + c)
static bool SaveScattering3D_RGBA_DDS(
	const float* srcRGBA,
	int R, int MU, int MU_S, int NU,
	const std::wstring& path)
{
	using namespace DirectX;

	// 3D 텍스처 물리 해상도: X = NU*MU_S, Y = MU, Z = R
	const int W = NU * MU_S;
	const int H = MU;
	const int D = R;

	ScratchImage img;
	HRESULT hr = img.Initialize3D(DXGI_FORMAT_R32G32B32A32_FLOAT, W, H, D, /*mips*/1);
	if (FAILED(hr))
	{
		return false;
	}

	// DirectXTex 3D 접근: GetImage(mip=0, item=zSlice, array=0)
	for (int z = 0; z < D; ++z)
	{
		const Image* slice = img.GetImage(0, 0, z);
		for (int y = 0; y < H; ++y)
		{
			float* row = reinterpret_cast<float*>(slice->pixels + size_t(y) * slice->rowPitch);
			for (int x = 0; x < W; ++x)
			{
				// x -> (mu_s, nu) 로 역매핑
				int mu = y;
				int r = z;
				int mus = x / NU;
				int nu = x % NU;

				size_t idx4 = ((((size_t)r * MU + mu) * MU_S + mus) * NU + nu);
				size_t s = idx4 * 4;

				row[x * 4 + 0] = srcRGBA[s + 0];
				row[x * 4 + 1] = srcRGBA[s + 1];
				row[x * 4 + 2] = srcRGBA[s + 2];
				row[x * 4 + 3] = srcRGBA[s + 3];
			}
		}
	}

	// Save entire volume
	return SUCCEEDED(SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), DDS_FLAGS_NONE, path.c_str()));
}