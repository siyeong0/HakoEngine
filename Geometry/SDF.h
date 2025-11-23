#pragma once
#include "Common/Common.h"

// You can adjust the namespace to match your engine layout.
// For example: namespace Geometry::SDF { ... }
namespace SDF
{
	// ------------------------------------------------------------
	// Axis-aligned box SDF
	//
	// Box defined by center C and half extents E:
	//   B = { x | |x - C| <= E }
	//
	// Returns signed distance:
	//   > 0 outside, 0 on surface, < 0 inside.
	// ------------------------------------------------------------

	inline float Box(const FVector3& p, const FVector3& center, const FVector3& halfExtents)
	{
		FVector3 q = FVector3::Abs(p - center) - halfExtents;

		// Outside distance
		FVector3 maxPart = FVector3::Max(q, FVector3::Zero());
		float outsideDist = std::sqrt(FVector3::Dot(maxPart, maxPart));

		// Inside distance (max component of q, clamped to <= 0)
		float insideDist = std::min(FVector3::MaxComponent(q), 0.0f);

		return outsideDist + insideDist;
	}

	inline float Box(const FVector3& p, const Bounds& box)
	{
		return Box(p, box.Center(), box.Extents());
	}

	// Unit cube: [-0.5, 0.5]^3
	inline float UnitCube(const FVector3& p)
	{
		return Box(p, FVector3(0.0f, 0.0f, 0.0f), FVector3(0.5f, 0.5f, 0.5f));
	}

	// ------------------------------------------------------------
	// Sphere SDF
	//
	// Sphere centered at C with radius R:
	//   S = { x | ||x - C|| <= R }
	// ------------------------------------------------------------

	inline float Sphere(const FVector3& p, const FVector3& center, float radius)
	{
		FVector3 d = p - center;
		float len = std::sqrt(FVector3::Dot(d, d));
		return len - radius;
	}

	// Centered at origin
	inline float Sphere(const FVector3& p, float radius)
	{
		return Sphere(p, FVector3(0.0f, 0.0f, 0.0f), radius);
	}

	// ------------------------------------------------------------
	// Infinite(-ish) thin box "plane"
	// (used in your code as a plane = thin box with thickness = cellSize)
	// ------------------------------------------------------------

	inline float ThinBoxPlane(
		const FVector3& p,
		float width,
		float height,
		float thickness /* e.g. cellSize */)
	{
		FVector3 halfExtents(0.5f * width, 0.5f * height, 0.5f * thickness);
		return Box(p, FVector3(0.0f, 0.0f, 0.0f), halfExtents);
	}

	// ------------------------------------------------------------
	// Cylinder SDF (Y axis)
	//
	// Finite cylinder:
	//   - Axis: Y
	//   - Radius: R
	//   - Height: H  (Y in [-H/2, +H/2])
	// ------------------------------------------------------------

	inline float Cylinder(const FVector3& p, float radius, float height)
	{
		float halfH = 0.5f * height;

		// Radial distance in XZ plane (Y is cylinder axis)
		float radial = std::sqrt(p.x * p.x + p.z * p.z);
		float yAbs = std::abs(p.y);

		// Signed distances along radial and vertical directions
		float dx = radial - radius;  // radial direction (XZ)
		float dy = yAbs - halfH;     // height along Y

		// Outside region components
		float ox = std::max(dx, 0.0f);
		float oy = std::max(dy, 0.0f);

		float outsideDist = std::sqrt(ox * ox + oy * oy);

		// Inside distance (max of dx, dy), clamped to <= 0
		float insideDist = std::min(std::max(dx, dy), 0.0f);

		return outsideDist + insideDist;
	}

	// ------------------------------------------------------------
	// Cone SDF (Y axis)
	//
	// Finite cone:
	//   - Axis: Y
	//   - Y range: [-H/2, +H/2]
	//   - Radius at y = -H/2 is 'radius'
	//   - Radius at y = +H/2 is 0 (apex)
	//
	// This is an approximate SDF modeled as the intersection of:
	//   - side condition   : radial <= rAtY
	//   - height condition : -H/2 <= y <= +H/2
	// ------------------------------------------------------------

	inline float Cone(const FVector3& p, float radius, float height)
	{
		float halfH = 0.5f * height;

		float y = p.y;

		// Radial distance in XZ plane
		float radial = std::sqrt(p.x * p.x + p.z * p.z);

		// Normalized height parameter:
		//   t = 0 at apex (y = +H/2)
		//   t = 1 at base (y = -H/2)
		float t = (halfH - y) / height;
		t = std::clamp(t, 0.0f, 1.0f);

		// Radius of the cone at height y
		float rAtY = radius * t;

		// Side condition (radial vs cone radius)
		float sSide = radial - rAtY;

		// Vertical slab condition
		float sHeight = std::abs(y) - halfH;

		// Approximate SDF as intersection: inside if both <= 0
		return std::max(sSide, sHeight);
	}
}
