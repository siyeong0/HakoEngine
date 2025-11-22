#include "pch.h"
#include "Generic/Color.h"
#include "TraceVolumeCubq.h"

#include <cfloat>
#include <climits>

#define COMPILE_AS_CPP 1 // Not defined in GLSL version

// The full DAG has 33 levels, from zero (for leaves) to 32 (for the root).
const int RootNodeHeight = 32;

IVector3 childIdToIVec3(int childId)
{
	return IVector3({ (childId) & 0x1, (childId >> 1) & 0x1, (childId >> 2) & 0x1 });
}

////////////////////////////////////////////////////////////////////////////////////////////////
//  ______              _                  _                                                  //
//  | ___ \            | |                (_)                                                 //
//  | |_/ /__ _ _   _  | |_ _ __ __ _  ___ _ _ __   __ _                                      //
//  |    // _` | | | | | __| '__/ _` |/ __| | '_ \ / _` |                                     //
//  | |\ \ (_| | |_| | | |_| | | (_| | (__| | | | | (_| |                                     //
//  \_| \_\__,_|\__, |  \__|_|  \__,_|\___|_|_| |_|\__, |                                     //
//               __/ |                              __/ |                                     //
//              |___/                              |___/                                      //
//                                                                                            //
////////////////////////////////////////////////////////////////////////////////////////////////

SubDAG findSubDAG(const Internals::NodeStore& nodes, uint rootNodeIndex, uint childId)
{
	// Initialised for root, but updated on first iteration of the loop.
	int childHeight = 32;
	IVector3 lowerBound = { INT_MIN , INT_MIN , INT_MIN };

	// We never return the root node as a subDAG, instead
	// we always descend into at least the first child.
	uint onlyChildId = childId;
	uint nextNodeIndex = nodes[rootNodeIndex][onlyChildId];
	uint nodeIndex = 0;
	uint childCount = 1;

	// Executed at least once as initialised to '1' above
	while (childCount == 1)
	{
		childHeight--;
		nodeIndex = nextNodeIndex;

		IVector3 childIdAsIVec3 = childIdToIVec3(int(onlyChildId));
		childIdAsIVec3 <<= childHeight;
		lowerBound ^= IVector3(childIdAsIVec3);

		childCount = 0;
		for (uint i = 0; i < 8; i++)
		{
			uint childNodeIndex = nodes[nodeIndex][i];
			if (childNodeIndex > 0)
			{
				// Store value we just received to avoid GPU memory access
				// cost of getting it again on next iteration of outer loop.
				nextNodeIndex = childNodeIndex;
				childCount++;
				onlyChildId = i;
			}
		}
	}

	SubDAG subDAG;
	subDAG.nodeIndex = nodeIndex;
	subDAG.lowerBound = lowerBound;
	subDAG.nodeHeight = childHeight;

	return subDAG;
}

SubDAGArray findSubDAGs(const Internals::NodeStore& nodes, uint32_t rootNodeIndex)
{
	SubDAGArray subDAGs;
	for (uint childId = 0; childId < 8; childId++)
	{
		subDAGs[childId] = findSubDAG(nodes, rootNodeIndex, childId);
	}
	return subDAGs;
}

SubDAG getSubDAG(const Internals::NodeStore& nodes, uint rootNodeIndex, const SubDAGArray& subDAGs, uint childId)
{
	int method = 1;
	switch (method) {
	case 1:
		// Use the real precomputed SubDAG
		return subDAGs[childId];
	case 2:
		// Compute the SubDAG on demand
		return findSubDAG(nodes, rootNodeIndex, childId);
	case 3:
	default:
		// Bypass use of the SubDAG by returning one which directly references the relevant child of the
		// root. This means the ESVO algorithm has to traverse the full tree. It's a slower option, but is
		// the simplest and can be used to validate the pecision of the ESVO traversal for large volumes.
		SubDAG bypassSubDAG;
		bypassSubDAG.lowerBound[0] = bool(childId & 0x1) ? 0 : INT_MIN;
		bypassSubDAG.lowerBound[1] = bool(childId & 0x2) ? 0 : INT_MIN;
		bypassSubDAG.lowerBound[2] = bool(childId & 0x4) ? 0 : INT_MIN;
		bypassSubDAG.nodeHeight = RootNodeHeight - 1;
		bypassSubDAG.nodeIndex = nodes[rootNodeIndex][childId];
		return bypassSubDAG;
	}
}

// Find the material of the nearest occupied child based on the direction ray is travelling (i.e.
// the direction we are viewing the node from). Note that this does not use an ESVO-type traversal
// (though perhaps it should, I haven't tested) but instead simply iterates over all child nodes
// in near-to-far order until an occupied one is found. The traversal approach is described here:
//
// https://www.flipcode.com/archives/Harmless_Algorithms-Issue_02_Scene_Traversal_Algorithms.shtml#octh
//
// It does not check for intersection and so might be faster than an ESVO-type approach (less logic,
// but maybe more memory accesses?), at the expense of some precision (the ray might actually miss
// the nearest occupied child). I'm assuming this doesn't matter too much as voxels are tiny on screen.
uint findNearestMaterial(const Internals::NodeStore& nodes, uint nodeIndex, uint rayDirSignBits)
{
	// We use a clever packing trick for the array of child IDs to iterate over. Each of the 8 child
	// IDs (0-7) needs only three bits, and a fourth 'valid' bit is used to indicate that a value is
	// stored. We iterate over the values with right-shifting, so the valid bit is cleared for new
	// values which lets us know when we are done.
	// I'm not sure it's really any better than a simple array, but is more compact and does let us do
	// the reflection (based on ray dir sign) in one XOR prior to the loop, rather than per-iteration.
	uint nearToFarPacked = 0x76534210; // '3' and '4' swapped on purpose, see link above.
	nearToFarPacked |= 0x88888888; // Set every 4th bit high as a marker

	const uint rayDirSignBitsDup = rayDirSignBits * 0x11111111; // Duplicate lower nibble across uint
	const uint orderedChildIds = nearToFarPacked ^ rayDirSignBitsDup; // Reflect all ids in one go

	while (nodeIndex >= MaterialCount)
	{
		for (uint childIds = orderedChildIds; childIds != 0; childIds >>= 4)
		{
			uint childNodeIndex = nodes[nodeIndex][childIds & 0x7];
			if (childNodeIndex > 0) // Skip empty nodes
			{
				nodeIndex = childNodeIndex;
				break;
			}
		}
	}

	return nodeIndex;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// See "An Efficient Parametric Algorithm for Octree Traversal"
///////////////////////////////////////////////////////////////////////////////////////////////////

float min3(FVector3 vec)
{
	return std::min(std::min(vec.x, vec.y), vec.z);
}

float max3(FVector3 vec)
{
	return std::max(std::max(vec.x, vec.y), vec.z);
}

IVector3 findFirstChild(float nodeEntry, FVector3 rayOrigin, FVector3 invRayDir, IVector3 nodeCentre)
{
	// Distances to midpoint along each axis.
	FVector3 nodeTm = (ToFVector3(nodeCentre) - rayOrigin) * invRayDir;

	// Find first child by determining the axes along which the ray has already passed the midpoint.
	// Logic reversed compared to ESVO paper, probabaly because they have a negative ray direction?
	IVector3 childId = IVector3(nodeTm.x < nodeEntry, nodeTm.y < nodeEntry, nodeTm.z < nodeEntry);

	// As an extension to the ESVO paper we skip children which are behind the ray origin. In many
	// cases there aren't any (as the ray origin is usually outside the node), but there can be
	// during the initial part of the traversal if the ray starts inside the volume. It saves us
	// having to check that the node intersection distance is greater than zero on every iteration.
	if (nodeEntry <= 0) // Only do skipping of children if node is at least partly behind camera.
	{
		childId |= IVector3(rayOrigin.x > nodeCentre.x, rayOrigin.y > nodeCentre.y, rayOrigin.z > nodeCentre.z);
	}

	return childId;
}

// This is the core function used to intersect a ray against our DAG. The algorithm is based on
// 'Efficient Sparse Voxel Octrees ?Analysis, Extensions, and Implementation' by S. Laine et al
// (hereafter referred to as the ESVO paper or algorithm).
//
// This algorithm was developed on a GTX 660 which is already 10 years old at the time of writing
// and may have different performance characteristics to modern GPUs. I found that one of the
// biggest factors affecting performance was pushing and popping too much data on the stack. 
// possible cause of this is that the stack may get allocated in local memory and accessing
// (perhaps especially writing? this is much slower than accessing registers. Fortunately the ESVO
// algorithm is quite conservative in this regard. See https://stackoverflow.com/q/49818414 
//
// Note: Compared to the ESVO paper we do not need to maintain the 'active span' because that is
// used to culling child voxels against the contours (which are potentially stored for eah level
// and combined when descending the tree). ESVO paper also tracks child entry point incrementally,
// but we don't need to do that here (the exit point is enough).
RayVolumeIntersection intersectRayNodeESVO(const Internals::NodeStore& nodes,
	uint nodeIndex, IVector3 nodePos, int nodeHeight,
	Ray ray, FVector3 rayDirSign, uint rayDirSignBits,
	bool computeSurfaceProperties, float maxFootprint)
{
	RayVolumeIntersection intersection = { false, 0, 0, {0, 0, 0}, {0, 0, 0} }; // Miss

	uint nodeSize = 1u << nodeHeight;

	// Intersect ray wth the node. The cast to float before
	// addition prevents integer overflow for largest nodes.
	FVector3 invRayDir = FVector3({ 1,1,1 }) / ray.direction;
	FVector3 nodeT0 = (ToFVector3(nodePos) - ray.origin) * invRayDir;
	FVector3 nodeT1 = ((ToFVector3(nodePos) + float(nodeSize)) - ray.origin) * invRayDir;

	float nodeEntry = max3(nodeT0);
	float nodeExit = min3(nodeT1);

	// Check if the ray actually hits the node.
	if (nodeEntry < nodeExit)
	{
		const int startHeight = nodeHeight;

		// Child size fits in signed type as it is half node size.
		int childNodeSize = int(nodeSize / 2);

		// We store the child id as an vector type whereas the ESVO paper uses a simple uint bitfield.
		// Using the vector type gives shorter, cleaner code but does appear to be slightly slower.
		// Later on we have to unpack our vector type to a uint bitfield when we use it as an index.
		IVector3 childId = findFirstChild(nodeEntry, ray.origin, invRayDir, nodePos + childNodeSize);

		// Child position is stored as integers to maximise precision for when dealing with very large
		// volumes. But we do quite often cast to floats, and floats can hold a wide range of integer
		// values anyway. We may want to reassess this (maybe cache the float values?) when we actually
		// have large volumes for testing.
		IVector3 childPos = nodePos + childId * childNodeSize;

		float lastExit = nodeExit; // Used to prevent unnecessary stack writes ('h' in ESVO paper)
		uint nodeStack[RootNodeHeight + 1]; // Valid range is [0, RootNodeHeight] inclusive

		do
		{
			// The exit point is computed explicitly for every child. We could instead compute it for
			// only the first child and then increment it as we move through the children, but the tiny
			// floating point error which accumulates is enough to break the stack protection (because
			// the exit point of the last child may not exactly match the exit point of the parent node).
			FVector3 childT1 = (ToFVector3(childPos + childNodeSize) - ray.origin) * invRayDir;
			float tChildExit = min3(childT1);
			assert(tChildExit > 0.0); // We only process node in front of the camera

			// Only touch memory once we are in front of the ray start.
			uint childIdBits = (childId[0] & 0x1) | ((childId[1] & 0x1) << 1) | ((childId[2] & 0x1) << 2);
			uint childNodeIndex = nodes[nodeIndex][childIdBits ^ rayDirSignBits];

			if (childNodeIndex > 0) // Child is occupied
			{
				// Compute the entry point
				FVector3 childT0 = (ToFVector3(childPos) - ray.origin) * invRayDir;
				float tChildEntry = max3(childT0);

				// If we have an internal (non-leaf) node we can traverse it further.
				// Traverse further if the node is large in screen space.
				const bool isInternalNode = childNodeIndex >= MaterialCount;
				const bool hasLargeFootprint = (childNodeSize / tChildExit) > maxFootprint;

				// Descend if we can and if we want to.
				if (isInternalNode && hasLargeFootprint)
				{
					// Push the current state onto the stack if not already there.
					// Note: It's not clear that the stack write protection actually helps
					// performance. This should be tested on a more recent GPU. Without it
					// we might be able to update the child exit point incrementally.
					if (tChildExit < lastExit)
					{
						nodeStack[nodeHeight] = nodeIndex;
					}
					lastExit = tChildExit;

					// As we descend the current child becomes the new current node.
					nodeHeight--;
					nodeIndex = childNodeIndex;
					childNodeSize /= 2;

					// Detemine which child of the child we will enter first.
					childId = findFirstChild(tChildEntry, ray.origin, invRayDir, childPos + childNodeSize);

					// Compute position of new child and restart loop to process it.
					childPos = childPos + childId * childNodeSize;
				}
				else // Node is occupied or we chose not to descend further, so this is where we stop.
				{
					intersection.hit = true;
					intersection.distance = tChildEntry;

					if (computeSurfaceProperties)
					{
						// Find the material of the intesected node (which might not be a leaf due to LOD).
						intersection.material = findNearestMaterial(nodes, childNodeIndex, rayDirSignBits);

						// Determining a true voxel normal is tricky because we don't have easy access to neighbouring
						// voxels. Therefore we currently determine a face normal instead by looking at which side of
						// the cube the ray entered. This is fine (even desirable, depending on user requirements) and
						// we can probably smooth out voxels in screen space if we want to. But I wonder if a true 
						// voxel normal would be better when bouncing rays around inside the volume for pathtracing.
						intersection.normal = FVector3(childT0.x == tChildEntry, childT0.y == tChildEntry, childT0.z == tChildEntry);
						intersection.normal *= -rayDirSign;
					}
				}
			}
			else
			{

				// Advance forward to the next the child node (sibling). The ESVO paper uses an equality
				// test in the text but '<=' in the sample code. I don't know why (though I can see it
				// is valid) but perhaps it adds robustness in the face of floating point inaccuracy?
				IVector3 nextChildFlipsVec = IVector3(childT1.x <= tChildExit, childT1.y <= tChildExit, childT1.z <= tChildExit);
				childId ^= nextChildFlipsVec;

				IVector3 oldChildPos = childPos;
				childPos += nextChildFlipsVec * childNodeSize;

				// Check that we sucessfully moved forward in every direction in which we tried to
				// do so. If not we have left the node and it is time to pop. Note that the sample
				// code in the ESVO paper actually checks against zero  here - I *think* that is a
				// bug and they fail to corectly handle the case where the ray leaves a child via
				// the corner and wraps around on one axis while moving forward on another.
				if (!((childId & nextChildFlipsVec) == nextChildFlipsVec))
				{
					// Find the highest node boundary crossed by the last step. This may not be the
					// direct parent (as we can ascend multiple levels in one go). Use the unsigned
					// version of GLSL findMSB() which has different behaviour to signed version.
					IVector3 differingBits = oldChildPos ^ childPos;
					int combinedDiffBits = differingBits[0] | differingBits[1] | differingBits[2];

					// Unsigned version
					auto findMSB = [](uint32_t x) ->int { return std::bit_width(x) - 1; };
					int msb = findMSB(uint(combinedDiffBits));

					// The next node up is the one contining both our previous and next siblings.
					nodeHeight = msb + 1;

					// We will terminate traversal if we get higher than the start of our subDAG,
					// but we should never get higher than the root of the full DAG. This also means
					// we are still within the bounds of the stack.
					assert(nodeHeight <= RootNodeHeight);

					// Retrieve the node index and compute child node properties
					nodeIndex = nodeStack[nodeHeight];
					childNodeSize = 1 << msb;
					childId = (childPos >> msb) & 1;
					childPos = (childPos >> nodeHeight) << nodeHeight;
					childPos += childId * childNodeSize;

					lastExit = 0.0f; // Prevent parent being written to stack again
				}
			}

		} while (intersection.hit == false && nodeHeight <= startHeight);
	}

	return intersection;
}

// Intersect a ray with a volume by performing using the ESVO on each octant.
//
// A Cubiquity volume can be very large (2^32 voxels along each side) but it practice most volume
// are much smaller than this. It is most natural for the actual voxel geometry to be in the
// centre of the volume, which is also the worst case because it means all eight octants are
// occupied. This mens a typical DAG will have a root with eight children, but each of these
// children will then contain a long sequence of nodes with no splits before the real geometry
// starts. Rather than starting traversal at the root eight times we instead precompute the first
// split point, which becaomes a sort of virtual root for each of the eight 'SubDAGs'. For each
// of these subDAGs we jump straight to the virtual root and perform an ESVO traversal from there.
//
// We could potentially compute the bounds of the actual occupied part of the volume and of the
// subdags, in order to optimise for the case when the ray mises the volume/subdag completely.
// But for now I prefer to assume that the ray hits the volume (e.g. because the camera is inside
// the volume, or because it is a secondary ray) in order to optimise for this worst case. This
// also keeps things simpler until we have determined our real requirements. We might also consider
// rendering the occupied bounds (rather than a fullscreen quad) to generate a more conservative
// set of fragments.
//
// Note: This function does not implement special handling of the case where a component of the
// ray direction is zero. This is discussed in Section 3.3 of An 'Efficient Parametric Algorithm
// for Octree Traversal'. I think that the standard behaviour of IEEE 754 handling of +/-infinity
// and NaNs might be enough but I am not certain. If it proves to be a problem (if we ever see
// NaNs?) then it can be solved by nudging tiny direction components away from zero.
RayVolumeIntersection intersectVolume(const VolumeCubq& volume, const SubDAGArray& subDAGs,
	float ray_orig_x, float ray_orig_y, float ray_orig_z,
	float ray_dir_x, float ray_dir_y, float ray_dir_z,
	bool computeSurfaceProperties, float maxFootprint)
{
	return intersectVolume(volume, subDAGs,
		Ray(FVector3(ray_orig_x, ray_orig_y, ray_orig_z), FVector3(ray_dir_x, ray_dir_y, ray_dir_z)),
		computeSurfaceProperties, maxFootprint);
}

RayVolumeIntersection intersectVolume(const VolumeCubq& volume, const SubDAGArray& subDAGs,
	Ray ray, bool computeSurfaceProperties, float maxFootprint)
{
	RayVolumeIntersection out = { false, 0, 0, {0,0,0}, {0,0,0} };

	const Internals::NodeStore& nodes = Internals::getNodes(volume).nodes();
	const uint rootNodeIndex = Internals::getRootNodeIndex(volume);

	// 부호 비트
	IVector3 rayDirSignBitsAsVec = IVector3(ray.direction.x < 0, ray.direction.y < 0, ray.direction.z < 0);
	uint     rayDirSignBits = rayDirSignBitsAsVec[0] | (rayDirSignBitsAsVec[1] << 1) | (rayDirSignBitsAsVec[2] << 2);
	FVector3 rayDirSign = ToFVector3(rayDirSignBitsAsVec * (-2) + 1);

	// near-to-far 순서 (있으면 사용) — 하지만 "첫 히트에서 break"는 금지
	uint nearToFarPacked = 0x76534210;
	uint orderedChildIds = nearToFarPacked ^ (rayDirSignBits * 0x11111111);

	bool   found = false;
	float  bestT = FLT_MAX;
	RayVolumeIntersection bestHit = { false, 0, 0, {0,0,0}, {0,0,0} };

	for (uint packed = orderedChildIds; packed != 0; packed >>= 4)
	{
		uint childId = packed & 0x7;

		SubDAG subDAG = getSubDAG(nodes, rootNodeIndex, subDAGs, childId ^ rayDirSignBits);
		uint   childNodeIndex = subDAG.nodeIndex;
		if (childNodeIndex == 0) continue;
		assert(childNodeIndex >= MaterialCount);

		// ESVO에 넣기 직전에만 “지역적으로” 반사 좌표계 사용
		Ray r = ray;
		r.origin += FVector3(0.5f, 0.5f, 0.5f);
		r.origin = r.origin * rayDirSign;
		r.direction = FVector3::Abs(r.direction);

		int nodeSize = int(1u << subDAG.nodeHeight);
		IVector3 refNodeLowerBound = subDAG.lowerBound * ToIVector3(rayDirSign);
		refNodeLowerBound -= rayDirSignBitsAsVec * IVector3({ nodeSize, nodeSize, nodeSize });

		RayVolumeIntersection hit = intersectRayNodeESVO(
			nodes,
			childNodeIndex, refNodeLowerBound, subDAG.nodeHeight,
			r, rayDirSign, rayDirSignBits,
			/*computeSurfaceProperties*/ computeSurfaceProperties,
			maxFootprint
		);

		if (hit.hit)
		{
			// t(거리)는 반사 좌표계에서도 동일 파라미터이므로 그대로 비교 가능
			if (hit.distance < bestT && hit.distance >= 0.0f)
			{
				found = true;
				bestT = (float)hit.distance;
				bestHit = hit;
			}
		}
	}

	if (found)
	{
		bestHit.position = ray.origin + (ray.direction * bestT); // 원래 ray 기준으로 위치 복원
		return bestHit;
	}
	return out; // miss
}



//RayVolumeIntersection intersectVolume(const Volume& volume, const SubDAGArray& subDAGs, Ray ray, bool computeSurfaceProperties, float maxFootprint)
//{
//	RayVolumeIntersection intersection = { false, 0, 0, {0, 0, 0}, {0, 0, 0} }; // Miss
//
//	const Internals::NodeStore& nodes = Internals::getNodes(volume).nodes();
//
//	const uint rootNodeIndex = Internals::getRootNodeIndex(volume);
//
//	// Store the sign of the ray direction
//	IVector3 rayDirSignBitsAsVec = IVector3(ray.direction.x < 0, ray.direction.y < 0, ray.direction.z < 0); // 0 means +ve, 1 means -ve
//	uint rayDirSignBits = rayDirSignBitsAsVec[0] | (rayDirSignBitsAsVec[1] << 1) | (rayDirSignBitsAsVec[2] << 2);
//	FVector3 rayDirSign = ToFVector3(rayDirSignBitsAsVec * (-2) + 1); // Binary (-1, 1) not ternary (-1, 0, 1)
//
//	// We reflect the ray so that it points along the positive axes. This simplifies the traversal.
//	// Shifting the ray origin lets us pretend voxel corners have integer coordinates (not +/-0.5).
//	Ray reflectedRay = ray;
//	reflectedRay.origin += FVector3({ 0.5f, 0.5f, 0.5f });
//	reflectedRay.origin = reflectedRay.origin * rayDirSign;
//	reflectedRay.direction = FVector3::Abs(reflectedRay.direction);
//
//	// Work out our starting octant by determining which of the axes we are already in front of.
//	// If we are in front of a given axis, the set the corresponding distance to a large value
//	// so we ignore it when later searching for the minimum.
//	int childId = 0;
//	FVector3 distToOrigin = (-reflectedRay.origin) / reflectedRay.direction;
//	if (distToOrigin[0] < 0.0) { childId |= 1; distToOrigin[0] += FLT_MAX; }
//	if (distToOrigin[1] < 0.0) { childId |= 2; distToOrigin[1] += FLT_MAX; }
//	if (distToOrigin[2] < 0.0) { childId |= 4; distToOrigin[2] += FLT_MAX; }
//
//	// Iteration over the octants is similar to the ESVO approach, except that we consider
//	// the octants to be (semi) infinite. Because the root is so large it is challenging
//	// (and probably unnecessary) to do proper intersection calculations against the children.
//	// This also means a ray will always reach the final octant (it will never miss it)
//	// unless it hits geometry first, which simplifies the loop termination condition.
//	do
//	{
//		SubDAG subDAG = getSubDAG(nodes, rootNodeIndex, subDAGs, childId ^ rayDirSignBits);
//		uint childNodeIndex = subDAG.nodeIndex;
//
//		if (childNodeIndex > 0)
//		{
//			// FIXME - Handle fully occupied direct child of root node.
//			assert(childNodeIndex >= MaterialCount);
//
//			// We can store the lower bound as integers because we offset ray origin by 0.5.
//			// On reflected axis the 'upper' corner becomes lower than the 'lower' corner.
//			// Node size might be large and negative, but I *think* the logic still works.
//			int nodeSize = int(1u << subDAG.nodeHeight);
//			IVector3 refNodeLowerBound = subDAG.lowerBound * ToIVector3(rayDirSign);
//			refNodeLowerBound -= rayDirSignBitsAsVec * IVector3({ nodeSize, nodeSize, nodeSize });
//
//			intersection = intersectRayNodeESVO(nodes,
//				childNodeIndex, refNodeLowerBound, subDAG.nodeHeight,
//				reflectedRay, rayDirSign, rayDirSignBits, computeSurfaceProperties, maxFootprint);
//
//			// Return on first hit. Position computed from real ray (not reflected version).
//			if (intersection.hit) {
//				intersection.position = ray.origin + (ray.direction * (float)intersection.distance);
//				break;
//			}
//		}
//
//		// Determine which axis is closest to work out which child we intersect next.
//		float minDist = min3(distToOrigin);
//		if (distToOrigin[0] <= minDist) { childId += 1; distToOrigin[0] += FLT_MAX; }
//		if (distToOrigin[1] <= minDist) { childId += 2; distToOrigin[1] += FLT_MAX; }
//		if (distToOrigin[2] <= minDist) { childId += 4; distToOrigin[2] += FLT_MAX; }
//
//	} while (childId <= 7); // 8 children, number 7 is the last.
//
//	return intersection;
//}
//


class IntersectionFinder
{
public:
	IntersectionFinder(const Ray& ray) : mRay(ray)
	{
		mIntersection.hit = false;
		mIntersection.material = 0;
		mIntersection.distance = DBL_MAX; // Note: Might change type to float in the future?
	}

	bool operator()(NodeDAG& nodes, uint32_t nodeIndex, const IBounds& bounds)
	{
		Bounds dilatedBounds = Bounds(
			ToFVector3(bounds.Min) - 0.5f,
			ToFVector3(bounds.Max) + 0.5f);
		RayBoxIntersection intersection = intersect(mRay, dilatedBounds);

		if (!intersection) { return false; } // Stop traversal if the ray missed the node.

		if ((isMaterialNode(nodeIndex)) && (nodeIndex > 0)) // Non-empty leaf node
		{
			// Discard nodes behind the start point, but include the node we start in.
			if (intersection.exit > 0.0)
			{
				if (intersection.entry < mIntersection.distance) // Is it closer than any other interection we foud?
				{
					mIntersection.hit = true;
					mIntersection.distance = intersection.entry;
					mIntersection.material = static_cast<MaterialId>(nodeIndex);
				}
			}
		}

		// Should early out here if we miss the node?
		return true;
	}

public:
	Ray mRay;
	RayVolumeIntersection mIntersection;
};

// TODO: FIXME - Can we make this take a const volume reference?
template<typename Functor>
void visitVolumeNodes(VolumeCubq& volume, Functor&& callback)
{
	Internals::NodeDAG& mDAG = Internals::getNodes(volume);
	const uint32_t rootNodeIndex = Internals::getRootNodeIndex(volume);

	const uint32_t rootHeight = 32; // FIXME - Make this a constant somewhere?
	const IBounds rootBounds;

	// Call the handler on the root.
	const bool processChildren = callback(mDAG, rootNodeIndex, rootBounds);

	// Process the root's children if requested and possible.
	const bool hasChildren = !Internals::isMaterialNode(rootNodeIndex);
	if (hasChildren && processChildren)
	{
		visitChildNodes(mDAG, rootNodeIndex, rootBounds, rootHeight, callback);
	}
}

// Call the callback on each child of the specified node.
template<typename Functor>
void visitChildNodes(Internals::NodeDAG& mDAG, uint32_t nodeIndex, const IBounds& bounds, uint32_t height, Functor&& callback)
{
	// Determine which bit may need to be flipped
	// to derive child bounds from parent bounds.
	const uint32_t childHeight = height - 1;
	const uint32_t bitToFlip = 0x01 << childHeight;

	for (uint32_t childId = 0; childId < 8; childId++)
	{
		const uint32_t childNodeIndex = mDAG[nodeIndex][childId];

		// Set the child bounds to be the same as the parent bounds and then collapse 
		// three of the faces (selected via the child id). Note that we could actually
		// skip the copy and modify the parent bounds in place as long as we flipped
		// the bits again after proessing. We could also iterate over the three child
		// components directly (rather than iterating over the child id and then
		// extracting the components), which also lets us avoid flipping all three
		// axes on every iteration. However, these changes complicate the code and
		// gave only a small speed improvement.
		IBounds childBounds = bounds;
		if ((((~childId) >> 0) & 0x01) == 0)	childBounds.Min.x ^= bitToFlip;
		else 									childBounds.Max.x ^= bitToFlip;
		if ((((~childId) >> 1) & 0x01) == 0)	childBounds.Min.y ^= bitToFlip;
		else 									childBounds.Max.y ^= bitToFlip;
		if ((((~childId) >> 2) & 0x01) == 0)	childBounds.Min.z ^= bitToFlip;
		else 									childBounds.Max.z ^= bitToFlip;

		// Call the handler on this child.
		const bool processChildren = callback(mDAG, childNodeIndex, childBounds);

		// Process this child's children if requested and possible.
		const bool hasChildren = !Internals::isMaterialNode(childNodeIndex);
		if (hasChildren && processChildren)
		{
			visitChildNodes(mDAG, childNodeIndex, childBounds, childHeight, callback);
		}
	}
}

RayVolumeIntersection traceRayRef(VolumeCubq& volume,
	float ray_orig_x, float ray_orig_y, float ray_orig_z,
	float ray_dir_x, float ray_dir_y, float ray_dir_z)
{
	return traceRayRef(volume,
		Ray(FVector3(ray_orig_x, ray_orig_y, ray_orig_z), FVector3(ray_dir_x, ray_dir_y, ray_dir_z)));
}

RayVolumeIntersection traceRayRef(VolumeCubq& volume, Ray ray)
{
	IntersectionFinder intersectionFinder(ray);
	visitVolumeNodes(volume, intersectionFinder);

	return intersectionFinder.mIntersection;
}

static inline int mapToGrid(uint32_t p, uint32_t P, int D)
{
	float t = (float(p) + 0.5f) / float(P);
	int idx = (int)std::floor(t * (float)D);
	if (idx < 0) idx = 0;
	if (idx > D - 1) idx = D - 1;
	return idx;
}

void ProjectVolumeFace(
	const VolumeCubq& volume,
	uint8_t face,
	uint maxSize,
	Image* outDiffuseImage,
	Image* outDepthImage)
{
	if (!outDiffuseImage || !outDepthImage)
		return;

	// 1) 점유 바운즈
	const IBounds& bounds = volume.getOccupiedBounds();
	const IVector3& bmin = bounds.Min;
	const IVector3& bmax = bounds.Max;

	// 비어있음 → 1x1 반환
	if (bmin.x > bmax.x || bmin.y > bmax.y || bmin.z > bmax.z)
	{
		*outDiffuseImage = Image::CreateBlank(1, 1, Image::FORMAT_RGBA8_UNORM, 1, 1, false);
		*outDepthImage = Image::CreateBlank(1, 1, Image::FORMAT_R16_UNORM, 1, 1, false);
		if (auto* c = const_cast<uint8_t*>(static_cast<const uint8_t*>(outDiffuseImage->GetDataPtr(0, 0, 0))))
			c[0] = c[1] = c[2] = 0, c[3] = 0;
		if (auto* d = const_cast<uint8_t*>(static_cast<const uint8_t*>(outDepthImage->GetDataPtr(0, 0, 0))))
			d[0] = 0xFF, d[1] = 0xFF; // miss = 0xFFFF
		return;
	}

	// 2) 바운즈 크기
	const IVector3 dim(
		(int32_t((int64_t)bmax.x - (int64_t)bmin.x + 1)),
		(int32_t((int64_t)bmax.y - (int64_t)bmin.y + 1)),
		(int32_t((int64_t)bmax.z - (int64_t)bmin.z + 1))
	);

	// 3) face별 축 매핑/플립/스캔 방향 (CASPER과 동일한 컨벤션)
	int projAxis = 0, axisU = 0, axisV = 1;
	int stepSign = +1;                 // +면 +축으로 레이 쏨, -면 -축
	bool flipU = false, flipV = false;

	switch (face)
	{
	case 0: /*POSX*/ projAxis = 0; axisU = 2; axisV = 1; stepSign = -1; flipU = true;  flipV = true;  break;
	case 1: /*NEGX*/ projAxis = 0; axisU = 2; axisV = 1; stepSign = +1; flipU = false; flipV = true;  break;
	case 2: /*POSY*/ projAxis = 1; axisU = 0; axisV = 2; stepSign = -1; flipU = false; flipV = false; break;
	case 3: /*NEGY*/ projAxis = 1; axisU = 0; axisV = 2; stepSign = +1; flipU = false; flipV = true;  break;
	case 4: /*POSZ*/ projAxis = 2; axisU = 0; axisV = 1; stepSign = -1; flipU = false; flipV = true;  break;
	case 5: /*NEGZ*/ projAxis = 2; axisU = 0; axisV = 1; stepSign = +1; flipU = true;  flipV = true;  break;
	default: break;
	}

	const int dimS = dim[projAxis];
	const int dimU = dim[axisU];
	const int dimV = dim[axisV];

	// 4) 출력 해상도 결정(정사각, 상한 maxSize)
	const uint32_t W = std::max(1u, std::min<uint32_t>(maxSize, std::max(dim.z, std::max(dim.x, dim.y))));
	// const uint32_t base = (uint32_t)std::max(dimU, dimV);
	// const uint32_t W = std::max(1u, std::min<uint32_t>(maxSize, base));
	const uint32_t H = W;

	*outDiffuseImage = Image::CreateBlank(W, H, Image::FORMAT_RGBA8_UNORM, 1, 1, false);
	*outDepthImage = Image::CreateBlank(W, H, Image::FORMAT_R16_UNORM, 1, 1, false);

	uint8_t* colorBase = const_cast<uint8_t*>(static_cast<const uint8_t*>(outDiffuseImage->GetDataPtr(0, 0, 0)));
	uint8_t* depthBase = const_cast<uint8_t*>(static_cast<const uint8_t*>(outDepthImage->GetDataPtr(0, 0, 0)));
	const size_t colorPitch = outDiffuseImage->GetRowPitch(0, 0, 0);
	const size_t depthPitch = outDepthImage->GetRowPitch(0, 0, 0);

	auto px4 = [&](uint32_t u, uint32_t v)->uint8_t* { return colorBase + v * colorPitch + size_t(u) * 4; };
	auto px2 = [&](uint32_t u, uint32_t v)->uint8_t* { return depthBase + v * depthPitch + size_t(u) * 2; };

	// 5) ESVO 준비(서브DAG 8개 프리컴퓨트)
	const Internals::NodeStore& nodes = Internals::getNodes(volume).nodes();
	const uint32_t rootNodeIndex = Internals::getRootNodeIndex(volume);
	SubDAGArray subDAGs = findSubDAGs(nodes, rootNodeIndex);

	// 6) 픽셀별 직교 레이 생성 → ESVO 교차
	// 레이 시작 평면은 투영축에 대해 바운즈의 "바깥쪽 면"을 선택
	//  - stepSign = +1 → min(projAxis) 평면에서 +축 방향
	//  - stepSign = -1 → max(projAxis) 평면에서 -축 방향
	const float startPlane = (stepSign > 0) ? bmin[projAxis] - 0.5f : bmax[projAxis] + 0.5f;
	const float dirSign = (stepSign > 0) ? +1.0f : -1.0f;

	// 깊이 정규화 기준(0..1) → 0..65535
	const float depthDen = (dimS > 0) ? float(dimS) : 1.0f; // 시작 평면을 포함한 길이 기준

	for (uint32_t v = 0; v < H; ++v)
	{
		const uint32_t vv = flipV ? (H - 1 - v) : v;
		const int gridV = mapToGrid(v, H, dimV);

		for (uint32_t u = 0; u < W; ++u)
		{
			const uint32_t uu = flipU ? (W - 1 - u) : u;
			const int gridU = mapToGrid(u, W, dimU);

			// (u,v) → 정수 격자 좌표(두 평면축)
			int32_t coordU = bmin[axisU] + gridU;
			int32_t coordV = bmin[axisV] + gridV;

			// 원점 구성: 투영축 좌표=startPlane, 나머지 두 축은 (coordU, coordV)
			FLOAT3 origin{};
			origin[projAxis] = startPlane;
			origin[axisU] = (float)coordU;
			origin[axisV] = (float)coordV;

			// 방향 벡터: 투영축으로 ±1
			FLOAT3 dir{ 0,0,0 };
			dir[projAxis] = dirSign;

			// ESVO 교차 (표면 속성 불필요: false)
			Ray ray{ origin, dir };
			RayVolumeIntersection hit = intersectVolume(volume, subDAGs, ray, /*computeSurfaceProperties*/true, /*maxFootprint*/MAX_FOOTPRINT_DISABLED);

			// 결과 쓰기
			// - 컬러: hit → (200,200,255,255), miss → 투명
			// - 깊이: miss=0xFFFF, hit = (distance / depthDen) * 65535
			uint16_t depth16 = 0xFFFF;
			uint8_t* c = px4(uu, vv);
			uint8_t* d = px2(uu, vv);

			if (hit.hit)
			{
				float depthF = std::clamp(float(hit.distance) / depthDen, 0.0f, 1.0f);
				uint32_t di = (uint32_t)std::lround(depthF * 65535.0f);
				depth16 = (uint16_t)std::min(di, 65535u);

				const RGBA rgba = volume.getColor(hit.material);
				c[0] = rgba.r; c[1] = rgba.g; c[2] = rgba.b; c[3] = rgba.a;
			}
			else
			{
				c[0] = 0; c[1] = 0; c[2] = 0; c[3] = 0;
			}

			d[0] = (uint8_t)(depth16 & 0xFF);        // lo
			d[1] = (uint8_t)((depth16 >> 8) & 0xFF); // hi
		}
	}
}