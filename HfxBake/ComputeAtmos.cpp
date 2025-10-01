#include "pch.h"
#include "ComputeAtmos.h"

// Planet geometry
struct PlanetGeom
{
	float Rg; // ground radius
	float Rt; // top-of-atmosphere radius
};

static void IntegrateTransmittanceRGB(
	float r0, float mu, 
	const PlanetGeom& pg, const AtmosParams& in, 
	float* outTrRGB, int numSteps = 64);
static void IntegrateSingleScatteringUnphased(
	float r0, float mu, float muS, 
	const PlanetGeom& pg, const AtmosParams& in,
	float* outRGBA, int numViewSteps = 64, int numSunSteps = 48);
static void ComputeDirectIrradianceRGB(
	float r0, float muS,
	const PlanetGeom& pg, const AtmosParams& in,
	float* outRGB, int numStepsSun = 64);

void ComputeAtmosCPU(const AtmosParams& in, AtmosResult* out)
{
	std::cout << "HfxBake::ComputeAtmos (CPU, single-scattering only)..." << std::endl;
	ASSERT(out, "Output pointer is null.");

	// 행성 지오메트리
	PlanetGeom pg = {};
	pg.Rg = in.PlanetRadius;
	pg.Rt = in.PlanetRadius + in.AtmosphereHeight;
	ASSERT(pg.Rg > 0.0f && pg.Rt > pg.Rg, "Invalid planet/atmosphere radius.");

	// 해상도 읽기
	const int TW = in.TransmittanceW, TH = in.TransmittanceH;
	const int SR = in.ScatteringR, SMU = in.ScatteringMu, SMUS = in.ScatteringMuS, SNU = in.ScatteringNu;
	const int EW = in.IrradianceW, EH = in.IrradianceH;

	ASSERT(TW > 0 && TH > 0 && SR > 0 && SMU > 0 && SMUS > 0 && SNU > 0 && EW > 0 && EH > 0,
		"Invalid LUT dimensions.");

	// 출력 버퍼 준비(기존 포인터가 있다면 해제는 호출자 규약에 따르세요)
	out->TransmittanceW = TW; out->TransmittanceH = TH;
	out->ScatteringR = SR; out->ScatteringMu = SMU; out->ScatteringMuS = SMUS; out->ScatteringNu = SNU;
	out->IrradianceW = EW; out->IrradianceH = EH;

	size_t Tsize = size_t(TW) * size_t(TH) * 3;
	size_t Ssize = size_t(SR) * size_t(SMU) * size_t(SMUS) * size_t(SNU) * 4;
	size_t Esize = size_t(EW) * size_t(EH) * 3;

	out->TransmittanceRGB = new float[Tsize];
	out->ScatteringRGBA = new float[Ssize];
	out->IrradianceRGB = new float[Esize];

	if (!out->TransmittanceRGB || !out->ScatteringRGBA || !out->IrradianceRGB)
	{
		std::cerr << "[HfxBake] Allocation failed." << std::endl;
		delete[] out->TransmittanceRGB; out->TransmittanceRGB = nullptr;
		delete[] out->ScatteringRGBA;   out->ScatteringRGBA = nullptr;
		delete[] out->IrradianceRGB;    out->IrradianceRGB = nullptr;
		return;
	}

	// ----------------------------- Transmittance 2D -----------------------------
	// 매핑(간단 버전): r ∈ [Rg, Rt], mu ∈ [-1, 1]를 균등 샘플
	for (int j = 0; j < TH; ++j)
	{
		float v = (j + 0.5f) / float(TH);
		float r = pg.Rg + (pg.Rt - pg.Rg) * v; // 균등 고도 (추후 Bruneton 매핑으로 교체 권장)

		for (int i = 0; i < TW; ++i)
		{
			float u = (i + 0.5f) / float(TW);
			float mu = -1.0f + 2.0f * u;       // 균등 코사인

			float Tr[3];
			IntegrateTransmittanceRGB(r, mu, pg, in, Tr, /*steps*/96);

			size_t idx = (size_t(j) * TW + i) * 3;
			out->TransmittanceRGB[idx + 0] = Tr[0];
			out->TransmittanceRGB[idx + 1] = Tr[1];
			out->TransmittanceRGB[idx + 2] = Tr[2];
		}
	}

	// ----------------------------- Scattering 4D (packed) -----------------------
	// 차원: (r, mu, mu_s, nu). 현재 구현은 "단산란 & 위상 미적용" → nu에 무관, 모든 nu slice 동일.
	for (int ir = 0; ir < SR; ++ir)
	{
		float fr = (ir + 0.5f) / float(SR);
		float r = pg.Rg + (pg.Rt - pg.Rg) * fr;

		for (int imu = 0; imu < SMU; ++imu)
		{
			float fmu = (imu + 0.5f) / float(SMU);
			float mu = -1.0f + 2.0f * fmu;

			for (int imus = 0; imus < SMUS; ++imus)
			{
				float fmus = (imus + 0.5f) / float(SMUS);
				float muS = -1.0f + 2.0f * fmus;

				// 단산란 적분 (phase 미적용)
				float RGBA[4];
				IntegrateSingleScatteringUnphased(r, mu, muS, pg, in, RGBA, /*viewSteps*/80, /*sunSteps*/64);

				for (int inu = 0; inu < SNU; ++inu)
				{
					size_t linear4 = (((size_t)ir * SMU + imu) * SMUS + imus) * SNU + inu;
					size_t base = linear4 * 4;
					out->ScatteringRGBA[base + 0] = RGBA[0]; // Rayleigh R
					out->ScatteringRGBA[base + 1] = RGBA[1]; // Rayleigh G
					out->ScatteringRGBA[base + 2] = RGBA[2]; // Rayleigh B
					out->ScatteringRGBA[base + 3] = RGBA[3]; // Mie (scalar)
				}
			}
		}
	}

	// ----------------------------- Irradiance 2D --------------------------------
	// (r, mu_s) 맵으로 정의: j→r, i→mu_s
	for (int j = 0; j < EH; ++j) 
	{
		float fr = (j + 0.5f) / float(EH);
		float r = pg.Rg + (pg.Rt - pg.Rg) * fr;

		for (int i = 0; i < EW; ++i) 
		{
			float fmus = (i + 0.5f) / float(EW);
			float muS = -1.0f + 2.0f * fmus;

			float E[3];
			ComputeDirectIrradianceRGB(r, muS, pg, in, E, /*sunSteps*/96);

			size_t idx = (size_t(j) * EW + i) * 3;
			out->IrradianceRGB[idx + 0] = E[0];
			out->IrradianceRGB[idx + 1] = E[1];
			out->IrradianceRGB[idx + 2] = E[2];
		}
	}

	std::cout << "HfxBake::ComputeAtmos done (CPU reference: single scattering, no phase in LUT)." << std::endl;
}

// ---------------------------- Internal helpers ----------------------------

// Safe macros
#ifndef HFX_MIN
#  define HFX_MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef HFX_MAX
#  define HFX_MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef HFX_CLAMP
#  define HFX_CLAMP(x,a,b) ( (x)<(a) ? (a) : ((x)>(b) ? (b) : (x)) )
#endif

static inline float dot3(float ax, float ay, float az, float bx, float by, float bz)
{
	return ax * bx + ay * by + az * bz;
}

static inline float length3(float x, float y, float z)
{
	return std::sqrt(x * x + y * y + z * z);
}

// DensityProfile: rho(h) = clamp( Σ (expTerm * exp(-h/scale) + linearTerm*h + constantTerm), 0..1 )
// Width > 0 인 레이어는 h > Width 구간에서 기여 0.
static float SampleDensity(const DensityProfile& prof, float h /* altitude meters */)
{
	float rho = 0.0f;
	for (int i = 0; i < 2; ++i)
	{
		const DensityLayer& L = prof.Layers[i];
		if (L.Scale == 0.0f && L.ExpTerm != 0.0f)
		{
			continue;
		}
		if (L.Width > 0.0f && h > L.Width)
		{
			continue;
		}
		float e = (L.Scale > 0.0f) ? std::exp(-h / L.Scale) : 0.0f;
		rho += L.ExpTerm * e + L.LinearTerm * h + L.ConstantTerm;
	}
	return HFX_CLAMP(rho, 0.0f, 1.0f);
}

// 반지름 r0(행성중심), 방향 코사인 mu 에 대해 구 위와의 교차 t(>0) 계산
// r(t)^2 = r0^2 + t^2 + 2 t r0 mu
static bool RaySphereIntersectT(float r0, float mu, float R, float& t0, float& t1)
{
	// t^2 + 2 r0 mu t + (r0^2 - R^2) = 0
	float B = 2.0f * r0 * mu;
	float C = r0 * r0 - R * R;
	float D = B * B - 4.0f * C;
	if (D < 0.0f)
	{
		return false;
	}
	float sD = std::sqrt(D);
	// 두 해 (작은 t가 입사, 큰 t가 출사)
	t0 = (-B - sD) * 0.5f;
	t1 = (-B + sD) * 0.5f;
	return true;
}

// 주어진 r0, mu 에서 상부 경계/지표로 향하는 유효 적분 구간 길이 tEnd를 결정
// down ray가 지표에 먼저 닿으면 그 지점까지만 적분 (지표는 불투명 가정)
static float PathLengthToBoundary(float r0, float mu, const PlanetGeom& pg, bool towardSun, bool* sunHitsGround = nullptr)
{
	float tTop0 = 0, tTop1 = 0, tG0 = 0, tG1 = 0;
	bool hitTop = RaySphereIntersectT(r0, mu, pg.Rt, tTop0, tTop1);
	bool hitGnd = RaySphereIntersectT(r0, mu, pg.Rg, tG0, tG1);

	float tExitTop = hitTop ? HFX_MAX(tTop0, tTop1) : -1.0f;
	float tEnterG = hitGnd ? HFX_MIN(tG0, tG1) : -1.0f;
	float tExitG = hitGnd ? HFX_MAX(tG0, tG1) : -1.0f;

	// 시작점은 대기 내부(r0∈[Rg, Rt])라고 가정.
	// view/sun 공통: 앞으로 진행(t>0)만 고려.
	float tEnd = 0.0f;

	if (mu >= 0.0f)
	{
		// 위로 향함: top 경계로 나감
		tEnd = (tExitTop > 0.0f) ? tExitTop : 0.0f;
		if (sunHitsGround) *sunHitsGround = false;
	}
	else
	{
		// 아래로 향함: 지표에 먼저 닿는지 확인
		if (tEnterG > 0.0f)
		{
			tEnd = tEnterG; // 지표까지
			if (sunHitsGround) *sunHitsGround = true;
		}
		else
		{
			// 지표 미교차 → 곧바로 top에서 나감
			tEnd = (tExitTop > 0.0f) ? tExitTop : 0.0f;
			if (sunHitsGround) *sunHitsGround = false;
		}
	}
	(void)tExitG; (void)tG1; // 경고 억제
	return (tEnd > 0.0f) ? tEnd : 0.0f;
}

// 고도 r(행성중심거리)에서 주어진 mu 방향으로의 광학두께 적분 → 채널별 Transmittance 반환
// extinction = RayleighScattering + MieExtinction + OzoneAbsorption (각각 밀도 가중)
static void IntegrateTransmittanceRGB(
	float r0, float mu,
	const PlanetGeom& pg, const AtmosParams& in,
	float* outTrRGB, int numSteps)
{
	float tEnd = PathLengthToBoundary(r0, mu, pg, /*towardSun*/false, nullptr);
	if (tEnd <= 0.0f)
	{
		outTrRGB[0] = outTrRGB[1] = outTrRGB[2] = 1.0f;
		return;
	}

	// 누적 광학두께
	double tauR = 0.0, tauG = 0.0, tauB = 0.0;

	const float ds = tEnd / float(numSteps);
	const float Rg = pg.Rg;

	for (int i = 0; i < numSteps; ++i)
	{
		float t = (i + 0.5f) * ds;
		// r(t) = sqrt(r0^2 + t^2 + 2 t r0 mu)
		float r = std::sqrt(r0 * r0 + t * t + 2.0f * t * r0 * mu);
		float h = HFX_MAX(0.0f, r - Rg);

		float rhoR = SampleDensity(in.Rayleigh, h);
		float rhoM = SampleDensity(in.Mie, h);
		float rhoO = SampleDensity(in.Ozone, h);

		// channel별 extinction [1/m]
		tauR += (in.RayleighScattering.x * rhoR
			+ in.MieExtinction.x * rhoM
			+ in.OzoneAbsorption.x * rhoO) * ds;
		tauG += (in.RayleighScattering.y * rhoR
			+ in.MieExtinction.y * rhoM
			+ in.OzoneAbsorption.y * rhoO) * ds;
		tauB += (in.RayleighScattering.z * rhoR
			+ in.MieExtinction.z * rhoM
			+ in.OzoneAbsorption.z * rhoO) * ds;
	}

	outTrRGB[0] = float(std::exp(-tauR));
	outTrRGB[1] = float(std::exp(-tauG));
	outTrRGB[2] = float(std::exp(-tauB));
}

// 샘플 지점 r0에서 태양 방향(muS)으로의 태양 투과도(직달광) 계산.
// 태양이 지평선 아래이면 0.
static void TransmittanceToSunRGB(
	float r0, float muS,
	const PlanetGeom& pg, const AtmosParams& in,
	float* outTrSunRGB, int numSteps = 64)
{
	bool sunHitsGround = false;
	float tEnd = PathLengthToBoundary(r0, muS, pg, /*towardSun*/true, &sunHitsGround);
	if (tEnd <= 0.0f || sunHitsGround)
	{
		outTrSunRGB[0] = outTrSunRGB[1] = outTrSunRGB[2] = 0.0f;
		return;
	}
	IntegrateTransmittanceRGB(r0, muS, pg, in, outTrSunRGB, numSteps);
}

// 단산란(phase 미적용) 적분: Rayleigh/Mie 성분을 분리해 반환 (RGB=Rayleigh, A=Mie)
// view 경로 감쇠 * (beta_s * rho) * 태양직달감쇠 * ds 를 적분. (phase는 런타임에서 곱)
static void IntegrateSingleScatteringUnphased(
	float r0, float mu, float muS,
	const PlanetGeom& pg, const AtmosParams& in,
	float* outRGBA, int numViewSteps, int numSunSteps)
{
	// view 경로 길이
	float tEndView = PathLengthToBoundary(r0, mu, pg, /*towardSun*/false, nullptr);
	if (tEndView <= 0.0f)
	{
		outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 0.0f;
		return;
	}

	// 누적 결과
	double SR = 0.0, SG = 0.0, SB = 0.0; // Rayleigh
	double SM = 0.0;                 // Mie (단일산란 성분)

	// view 경로 누적 tau (채널별)
	double tauVR = 0.0, tauVG = 0.0, tauVB = 0.0;

	const float ds = tEndView / float(numViewSteps);
	const float Rg = pg.Rg;

	for (int i = 0; i < numViewSteps; ++i)
	{
		float t = (i + 0.5f) * ds;
		float r = std::sqrt(r0 * r0 + t * t + 2.0f * t * r0 * mu);
		float h = HFX_MAX(0.0f, r - Rg);

		float rhoR = SampleDensity(in.Rayleigh, h);
		float rhoM = SampleDensity(in.Mie, h);
		float rhoO = SampleDensity(in.Ozone, h);

		// view 경로 감쇠 (midpoint 누적)
		tauVR += (in.RayleighScattering.x * rhoR
			+ in.MieExtinction.x * rhoM
			+ in.OzoneAbsorption.x * rhoO) * ds;
		tauVG += (in.RayleighScattering.y * rhoR
			+ in.MieExtinction.y * rhoM
			+ in.OzoneAbsorption.y * rhoO) * ds;
		tauVB += (in.RayleighScattering.z * rhoR
			+ in.MieExtinction.z * rhoM
			+ in.OzoneAbsorption.z * rhoO) * ds;

		float TrView[3] =
		{
			float(std::exp(-tauVR)),
			float(std::exp(-tauVG)),
			float(std::exp(-tauVB))
		};

		// 태양 방향 직달 감쇠
		float TrSun[3];
		TransmittanceToSunRGB(r, muS, pg, in, TrSun, numSunSteps);

		// 산란 계수(산란량용, Rayleigh는 extinction==scattering 가정)
		double bR_x = double(in.RayleighScattering.x) * double(rhoR);
		double bR_y = double(in.RayleighScattering.y) * double(rhoR);
		double bR_z = double(in.RayleighScattering.z) * double(rhoR);

		double bM = ((double)in.MieScattering.x + (double)in.MieScattering.y + (double)in.MieScattering.z) / 3.0; // 스칼라 근사
		bM *= double(rhoM);

		// 태양 복사 (TOA)
		double SunR = in.SolarIrradiance.x;
		double SunG = in.SolarIrradiance.y;
		double SunB = in.SolarIrradiance.z;

		// 단산란 기여 (phase 제외): Tr_view * (beta_s * rho) * Tr_sun * Sun * ds
		SR += TrView[0] * bR_x * TrSun[0] * SunR * ds;
		SG += TrView[1] * bR_y * TrSun[1] * SunG * ds;
		SB += TrView[2] * bR_z * TrSun[2] * SunB * ds;

		// Mie는 채널 독립 스칼라로 누적 (A 채널에 저장)
		double TrViewM = (TrView[0] + TrView[1] + TrView[2]) / 3.0;
		double TrSunM = (TrSun[0] + TrSun[1] + TrSun[2]) / 3.0;
		double SunM = (SunR + SunG + SunB) / 3.0;
		SM += TrViewM * bM * TrSunM * SunM * ds;
	}

	outRGBA[0] = float(SR);
	outRGBA[1] = float(SG);
	outRGBA[2] = float(SB);
	outRGBA[3] = float(SM);
}

// 단순화된 직접 조도(irradiance): E ≈ SolarIrradiance * max(muS,0) * TransmittanceToSun
static void ComputeDirectIrradianceRGB(
	float r0, float muS,
	const PlanetGeom& pg, const AtmosParams& in,
	float* outRGB, int numStepsSun)
{
	float TrSun[3];
	TransmittanceToSunRGB(r0, muS, pg, in, TrSun, numStepsSun);
	float cosTerm = HFX_MAX(0.0f, muS);
	outRGB[0] = in.SolarIrradiance.x * TrSun[0] * cosTerm;
	outRGB[1] = in.SolarIrradiance.y * TrSun[1] * cosTerm;
	outRGB[2] = in.SolarIrradiance.z * TrSun[2] * cosTerm;
}
