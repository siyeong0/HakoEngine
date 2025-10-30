#pragma once
#include "Common/Common.h"

enum MATERIAL_PRESET_TYPE
{
	MATERIAL_TYPE_DEFAULT,
	MATERIAL_TYPE_MATTE,
	MATERIAL_TYPE_MIRROR,
	MATERIAL_TYPE_PLASTIC,
	MATERIAL_TYPE_GLASS,
	MATERIAL_TYPE_WATER,
	MATERIAL_TYPE_METAL,
	MATERIAL_TYPE_COUNT
};

struct UMaterial
{
	std::wstring DiffuseTexturePath;
	std::wstring NormalTexturePath;
	std::wstring SpecularTexturePath;
	std::wstring MetallicTexturePath;
	std::wstring RoughnessTexturePath;

	FLOAT3 BaseColor = DEFAULT_BASE_COLOR;
	float Opacity = DEFAULT_OPACITY;

	FLOAT3 SpecularColor = DEFAULT_SPECULAR_COLOR;
	float SpecularFactor = DEFAULT_SPECULAR_INTENSITY;

	float MetallicFactor = DEFAULT_METALLIC_INTENSITY;
	float RoughnessFactor = DEFAULT_ROUGHNESS_INTENSITY;
	float NormalScale = DEFAULT_NORMAL_SCALE;
	float AmbientOcclusionStrength = DEFAULT_AMBIENT_OCCLUSION_STRENGTH;

	bool bAlphaMasked = false;

	UMaterial() = default;
	UMaterial(
		const wchar_t* diffusePathOrNull,
		const wchar_t* normalPathOrNull,
		const wchar_t* specularPathOrNull,
		const wchar_t* metallicPathOrNull,
		const wchar_t* roughnessPathOrNull,
		const FLOAT3& baseColor,
		float opacity,
		const FLOAT3& specularColor,
		float specularIntensity,
		float metallicIntensity,
		float roughnessIntensity,
		float normalScale,
		float ambientOcclusionStrength,
		bool bAlphaMaskOn)
		: DiffuseTexturePath(diffusePathOrNull ? diffusePathOrNull : L"")
		, NormalTexturePath(normalPathOrNull ? normalPathOrNull : L"")
		, SpecularTexturePath(specularPathOrNull ? specularPathOrNull : L"")
		, MetallicTexturePath(metallicPathOrNull ? metallicPathOrNull : L"")
		, RoughnessTexturePath(roughnessPathOrNull ? roughnessPathOrNull : L"")
		, BaseColor(baseColor)
		, Opacity(opacity)
		, SpecularColor(specularColor)
		, SpecularFactor(specularIntensity)
		, MetallicFactor(metallicIntensity)
		, RoughnessFactor(roughnessIntensity)
		, NormalScale(normalScale)
		, AmbientOcclusionStrength(ambientOcclusionStrength)
		, bAlphaMasked(bAlphaMaskOn)
	{
	}

	UMaterial(
		const wchar_t* diffusePathOrNull,
		const wchar_t* normalPathOrNull,
		const wchar_t* specularPathOrNull = nullptr,
		const wchar_t* metallicPathOrNull = nullptr,
		const wchar_t* roughnessPathOrNull = nullptr,
		MATERIAL_PRESET_TYPE type = MATERIAL_TYPE_DEFAULT,
		bool bAlphaMaskOn = false)
		: DiffuseTexturePath(diffusePathOrNull ? diffusePathOrNull : L"")
		, NormalTexturePath(normalPathOrNull ? normalPathOrNull : L"")
		, SpecularTexturePath(specularPathOrNull ? specularPathOrNull : L"")
		, MetallicTexturePath(metallicPathOrNull ? metallicPathOrNull : L"")
		, RoughnessTexturePath(roughnessPathOrNull ? roughnessPathOrNull : L"")
		, bAlphaMasked(bAlphaMaskOn)
	{
		// Reset to defaults first
		BaseColor = DEFAULT_BASE_COLOR;
		Opacity = DEFAULT_OPACITY;
		SpecularColor = DEFAULT_SPECULAR_COLOR;
		SpecularFactor = DEFAULT_SPECULAR_INTENSITY;
		MetallicFactor = DEFAULT_METALLIC_INTENSITY;
		RoughnessFactor = DEFAULT_ROUGHNESS_INTENSITY;
		NormalScale = DEFAULT_NORMAL_SCALE;
		AmbientOcclusionStrength = DEFAULT_AMBIENT_OCCLUSION_STRENGTH;

		switch (type)
		{
		case MATERIAL_TYPE_DEFAULT:

			break;

		case MATERIAL_TYPE_MATTE:
			BaseColor = { 0.75f, 0.75f, 0.75f };
			RoughnessFactor = 0.90f;
			SpecularColor = { 0.02f, 0.02f, 0.02f };
			SpecularFactor = 0.50f;
			break;

		case MATERIAL_TYPE_MIRROR:
			BaseColor = { 1.0f, 1.0f, 1.0f };
			MetallicFactor = 0.0f;
			RoughnessFactor = 0.01f;
			SpecularColor = { 1.0f, 1.0f, 1.0f };
			break;

		case MATERIAL_TYPE_PLASTIC:
			BaseColor = { 0.8f, 0.1f, 0.1f };
			RoughnessFactor = 0.35f;
			SpecularColor = { 0.04f, 0.04f, 0.04f };
			break;

		case MATERIAL_TYPE_GLASS:
			BaseColor = { 1.0f, 1.0f, 1.0f };
			Opacity = 0.05f;
			RoughnessFactor = 0.02f;
			AmbientOcclusionStrength = 0.10f;
			SpecularColor = { 0.04f, 0.04f, 0.04f };
			break;

		case MATERIAL_TYPE_WATER:
			BaseColor = { 0.8f, 0.8f, 1.0f };
			Opacity = 0.10f;
			RoughnessFactor = 0.02f;
			NormalScale = 1.50f;
			AmbientOcclusionStrength = 0.05f;
			SpecularColor = { 0.02f, 0.02f, 0.02f };
			break;

		case MATERIAL_TYPE_METAL:
			BaseColor = { 0.9f, 0.9f, 0.9f };
			MetallicFactor = 1.0f;
			RoughnessFactor = 0.15f;
			SpecularColor = { 1.0f, 1.0f, 1.0f };
			break;

		default:
			ASSERT(false, "Unknown material preset type.");
			break;
		}
	}

	bool HasDiffuseTexture() const { return !DiffuseTexturePath.empty(); }
	bool HasNormalTexture() const { return !NormalTexturePath.empty(); }
	bool HasSpecularTexture() const { return !SpecularTexturePath.empty(); }
	bool HasMetallicTexture() const { return !MetallicTexturePath.empty(); }
	bool HasRoughnessTexture() const { return !RoughnessTexturePath.empty(); }

	inline static constexpr float OPACITY_THRESHOLD = 0.99f;
	bool IsOpaque() const { return !bAlphaMasked && Opacity >= OPACITY_THRESHOLD; }

private:
	// Physically-plausible defaults (dielectric, mid-roughness)
	static inline constexpr FLOAT3 DEFAULT_BASE_COLOR = { 0.8f, 0.8f, 0.8f };
	static inline constexpr float  DEFAULT_OPACITY = 1.0f;
	static inline constexpr FLOAT3 DEFAULT_SPECULAR_COLOR = { 0.04f, 0.04f, 0.04f };
	//static inline constexpr FLOAT3 DEFAULT_SPECULAR_COLOR = { 0.5f, 0.5f, 0.5f };
	static inline constexpr float  DEFAULT_SPECULAR_INTENSITY = 1.0f;
	static inline constexpr float  DEFAULT_METALLIC_INTENSITY = 0.0f;
	// static inline constexpr float  DEFAULT_ROUGHNESS_INTENSITY = 0.5f;
	static inline constexpr float  DEFAULT_ROUGHNESS_INTENSITY = 0.01f;
	static inline constexpr float  DEFAULT_NORMAL_SCALE = 1.0f;
	static inline constexpr float  DEFAULT_AMBIENT_OCCLUSION_STRENGTH = 0.25f;
};