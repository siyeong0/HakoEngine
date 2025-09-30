#pragma once
#include "Common/Common.h"

/**
 * Density layer used to describe how a gas concentration varies with altitude.
 * The profile is expressed as:
 *   rho(h) = clamp( expTerm * exp(-alt / scale) + linearTerm * alt + constantTerm, 0, 1 )
 * where:
 *   - h/alt : altitude above planet surface in meters
 *   - scale : exponential scale height in meters
 * The final species density is typically a weighted sum of up to 2 layers.
 */
struct DensityLayer
{
	float Width;	// Effective thickness of this layer in meters (used by some implementations as a clamp/window).
	float ExpTerm;	// Coefficient for the exponential term (unitless).
	float LinearTerm;	// Coefficient for the linear term (1/m).
	float ConstantTerm; // Constant term (unitless).
	float Scale;	// Exponential scale height in meters. Must be > 0 for the exp term to be meaningful.
};

/**
 * Vertical density profile composed of up to two layers.
 * This matches the common parameterization used in Bruneton/Neyret (2008/2017).
 */
struct DensityProfile
{
	DensityLayer Layers[2]; // Two layers are generally sufficient for Earth-like atmospheres.
};

/**
 * Input parameters for precomputing atmospheric scattering LUTs.
 * UNITS: lengths in meters, angles in radians, spectral quantities in RGB per meter unless stated.
 * Top-of-atmosphere (TOA) radius = PlanetRadius + AtmosphereHeight.
 */
struct AtmosParams
{
	// ---------------------- Physical atmosphere parameters ----------------------
	float PlanetRadius;		// Planet surface radius (meters). Example (Earth): ~6,360,000 m.
	float AtmosphereHeight;	// Height of the atmosphere above the planet surface, in meters.

	FLOAT3 RayleighScattering;	// Rayleigh scattering coefficients per color channel [1/m] (air molecules).
	DensityProfile Rayleigh;	// Vertical density profile for Rayleigh species (usually a single exponential-like layer).

	FLOAT3 MieScattering;	// Mie scattering coefficients per color channel [1/m] (aerosols).
	FLOAT3 MieExtinction;	// Mie extinction coefficients per color channel [1/m] (scattering + absorption).
	DensityProfile Mie;		// Vertical density profile for Mie aerosols (lower scale height than Rayleigh).
	float MieG;				// Mie phase function asymmetry g in [-1, 1]. Forward scattering ~0.8–0.9 for hazy air.

	FLOAT3 OzoneAbsorption;	// Ozone absorption coefficients per color channel [1/m] (optional but improves twilight).
	DensityProfile Ozone;	// Vertical density profile for ozone (often a thick band centered ~20–30 km).

	FLOAT3 GroundAlbedo;	// Ground (surface) diffuse albedo RGB in [0,1]. Affects multiple scattering and horizon tint.
	FLOAT3 SolarIrradiance;	// Solar irradiance at top of atmosphere (RGB). Acts as a white balance/energy scale.
	float SunAngularRadius;	// Apparent solar angular radius in radians. Sun disk ≈ 0.004675 rad (~0.267°).

	// ---------------------- LUT resolutions ----------------------
	// These control the size of the precomputed textures. Larger sizes increase quality and cost.

	/// Transmittance 2D texture resolution (commonly 256x64).
	int TransmittanceW;
	int TransmittanceH;

	/// Scattering 4D resolution packed into 3D layers:
	/// R    : radius (altitude) slices
	/// Mu   : view zenith cosine bins (μ = cos θ)
	/// MuS  : sun zenith cosine bins (μ_s = cos θ_s)
	/// Nu   : view-sun cosine (phase) bins (ν = cos φ)
	/// Common choice: R=32, Mu=128, MuS=32, Nu=8 (packed as a 3D texture).
	int ScatteringR;
	int ScatteringMu;
	int ScatteringMuS;
	int ScatteringNu;

	/// Irradiance 2D texture resolution (commonly 64x16).
	int IrradianceW;
	int IrradianceH;

	/// Number of multiple-scattering orders to accumulate (≥2; 4 is a good default).
	int MultipleScatteringOrders;
};

/**
 * Output buffers holding the precomputed LUT data.
 * Memory ownership/lifetime should be defined by the API contract (e.g., producer allocates and
 * provides a Destroy/Free function, or caller supplies buffers). Data is row-major.
 */
struct AtmosResult
{
	// ---------------------- Transmittance (2D) ----------------------
	/// Transmittance texture size.
	int TransmittanceW;
	int TransmittanceH;

	/// RGB transmittance values. Size = TransmittanceW * TransmittanceH * 3.
	/// Mapping: (u,v) -> index = ((v * TransmittanceW) + u)*3 + c, c∈{0,1,2}.
	float* TransmittanceRGB;

	// ---------------------- Scattering (4D packed) ----------------------
	/// Scattering logical dimensions: radius, view μ, sun μ_s, and phase ν bins.
	int ScatteringR;
	int ScatteringMu;
	int ScatteringMuS;
	int ScatteringNu;

	/// RGBA scattering values. Size = R * Mu * MuS * Nu * 4.
	/// Convention: RGB = Rayleigh, A = single Mie (additional MS orders are accumulated in RGB/A).
	/// Linear index example:
	///   idx4 = (((r * ScatteringMu + mu) * ScatteringMuS + mus) * ScatteringNu + nu);
	///   idx  = idx4 * 4 + channel;  // channel ∈ {0..3}
	float* ScatteringRGBA;

	// ---------------------- Irradiance (2D) ----------------------
	/// Irradiance texture size.
	int IrradianceW;
	int IrradianceH;

	/// RGB irradiance values at ground/TOA. Size = IrradianceW * IrradianceH * 3.
	float* IrradianceRGB;
};
