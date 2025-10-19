#ifndef HLSL_CPP_COMMON_TYPES
#define HLSL_CPP_COMMON_TYPES

// bxdf.hlsli에 맞춤
enum MATERIAL_TYPE
{
    MATERIAL_TYPE_DEFAULT,
    MATERIAL_TYPE_MATTE, // Lambertian scattering
    MATERIAL_TYPE_MIRROR, // Specular reflector that isn't modified by the Fresnel equations.
    MATERIAL_TYPE_GLASS,
    MATERIAL_TYPE_ANALYICAL_CHECKERBOARD_TEXTURE,
};

// Light type
enum LIGHT_TYPE
{
    LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
	LIGHT_TYPE_COUNT
};

const static uint MAX_LIGHT_COUNT = 16;

#endif