#pragma once
/***************************************************************************************************
* Cubiquity - A micro-voxel engine for games and other interactive applications                    *
*                                                                                                  *
* Written in 2023 by David Williams                                                                *
*                                                                                                  *
* To the extent possible under law, the author(s) have dedicated all copyright and related and     *
* neighboring rights to this software to the public domain worldwide. This software is distributed *
* without any warranty.                                                                            *
*                                                                                                  *
* You should have received a copy of the CC0 Public Domain Dedication along with this software.    *
* If not, see http://creativecommons.org/publicdomain/zero/1.0/.                                   *
***************************************************************************************************/
#include "Volume.h"

#include "Generic/Image.h"

#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <algorithm>
#include <numeric>

using namespace Internals;

struct Ray
{
	FLOAT3 origin;
	FLOAT3 direction;
};

// This structure contains an explicit 'hit' member rather than relying on a sentinal value
// such as a negative distance or a material of zero. I have found this simplifies the code
// and we might want to skip material identification anyway (if traversal is stopped early
// due to LOD). There is some redundancy storing both the distance and the position, but we
// get the former basically for free as we traverse and the latter is very often useful.
struct RayVolumeIntersection
{
	bool hit;
	double distance;
	uint material;
	FLOAT3 position;
	FLOAT3 normal;
};

struct SubDAG
{
	IVector3 lowerBound;
	int32_t nodeHeight;

	uint padding0;
	uint nodeIndex;
	uint padding1, padding2;
};

typedef std::array<SubDAG, 8> SubDAGArray;

SubDAGArray findSubDAGs(const Internals::NodeStore& nodes, uint32_t rootNodeIndex);

const float MAX_FOOTPRINT_DISABLED = -1.0f;
RayVolumeIntersection intersectVolume(const Volume& volume, const SubDAGArray& subDAGs,
	float ray_orig_x, float ray_orig_y, float ray_orig_z,
	float ray_dir_x, float ray_dir_y, float ray_dir_z,
	bool computeSurfaceProperties, float maxFootprint = MAX_FOOTPRINT_DISABLED);
RayVolumeIntersection intersectVolume(const Volume& volume, const SubDAGArray& subDAGs, Ray ray, bool computeSurfaceProperties, float maxFootprint = MAX_FOOTPRINT_DISABLED);

RayVolumeIntersection traceRayRef(Volume& volume,
	float ray_orig_x, float ray_orig_y, float ray_orig_z,
	float ray_dir_x, float ray_dir_y, float ray_dir_z);
RayVolumeIntersection traceRayRef(Volume& volume, Ray ray);

void ProjectVolumeFace(const Volume& volume, uint8_t face, uint maxSize, Image* outDiffuseImage, Image* outDepthImage);