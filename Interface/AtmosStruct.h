#pragma once

struct AtmosParams
{
	float PlanetRadius;        // Radius of the planet in meters
	float AtmosphereHeight;    // Height of the atmosphere in meters
	float RayleighScattering;  // Rayleigh scattering coefficient
	float MieScattering;       // Mie scattering coefficient
	float SunIntensity;        // Intensity of the sun
	float SunAngularDiameter;  // Angular diameter of the sun in degrees
	float MieG;               // Mie phase function asymmetry factor
};

struct AtmosResult
{
	int TextureWidth;         // Width of the precomputed texture
	int TextureHeight;        // Height of the precomputed texture
	float* Transmittance;     // Transmittance texture data (size: TextureWidth * TextureHeight * 3)
	float* Scattering;        // Scattering texture data (size: TextureWidth * TextureHeight * 3)
	float* Irradiance;        // Irradiance texture data (size: TextureWidth * TextureHeight * 3)
};