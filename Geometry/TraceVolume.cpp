#include "pch.h"
#include "TraceVolume.h"

#include <cfloat>
#include <climits>
#include <cassert>
#include <algorithm>

// 전체 DAG는 0(리프)~32(루트) 레벨
static constexpr int RootNodeHeight = 32;

// --- 소형 헬퍼들 ---------------------------------------------------------

inline IVector3 ChildIdToIVec3(int childId)
{
	return IVector3{
		(childId) & 0x1,
		(childId >> 1) & 0x1,
		(childId >> 2) & 0x1
	};
}

inline float min3(const FVector3& v) { return std::min(std::min(v.x, v.y), v.z); }
inline float max3(const FVector3& v) { return std::max(std::max(v.x, v.y), v.z); }

// 비교 → 0/1 마스크(정수) 반환
inline IVector3 LT(const FVector3& a, const FVector3& b) {
	return IVector3{ a.x < b.x ? 1 : 0, a.y < b.y ? 1 : 0, a.z < b.z ? 1 : 0 };
}
inline IVector3 LE(const FVector3& a, const FVector3& b) {
	return IVector3{ a.x <= b.x ? 1 : 0, a.y <= b.y ? 1 : 0, a.z <= b.z ? 1 : 0 };
}
inline IVector3 GE(const FVector3& a, const FVector3& b) {
	return IVector3{ a.x >= b.x ? 1 : 0, a.y >= b.y ? 1 : 0, a.z >= b.z ? 1 : 0 };
}

// 정수 벡터 비트연산/시프트(컴포넌트별)
inline IVector3 SHL(const IVector3& v, int s) { return IVector3{ v.x << s, v.y << s, v.z << s }; }
inline IVector3 SHR(const IVector3& v, int s) { return IVector3{ v.x >> s, v.y >> s, v.z >> s }; }
inline IVector3 AND1(const IVector3& v, int m) { return IVector3{ v.x & m, v.y & m, v.z & m }; }
inline IVector3 XORV(const IVector3& a, const IVector3& b) { return IVector3{ a.x ^ b.x, a.y ^ b.y, a.z ^ b.z }; }

// float≈float 비교 → 마스크(float 0/1) (법선 계산용)
inline FVector3 FEQmask(const FVector3& a, const FVector3& b, float eps = 1e-6f) {
	return FVector3{
		(std::fabs(a.x - b.x) <= eps) ? 1.0f : 0.0f,
		(std::fabs(a.y - b.y) <= eps) ? 1.0f : 0.0f,
		(std::fabs(a.z - b.z) <= eps) ? 1.0f : 0.0f
	};
}

// rayDir 부호(-1,+1) 벡터(부호비트 0/1 → -1/+1)
inline FVector3 SignFromBits(const IVector3& bits01) {
	return FVector3{
		bits01.x ? -1.0f : 1.0f,
		bits01.y ? -1.0f : 1.0f,
		bits01.z ? -1.0f : 1.0f
	};
}
inline uint RayDirBits3(const FVector3& dir) {
	IVector3 b{ dir.x < 0 ? 1 : 0, dir.y < 0 ? 1 : 0, dir.z < 0 ? 1 : 0 };
	return (uint)b.x | ((uint)b.y << 1) | ((uint)b.z << 2);
}

////////////////////////////////////////////////////////////////////////////////////////////////
// SubDAG 전처리
////////////////////////////////////////////////////////////////////////////////////////////////

SubDAG findSubDAG(const Internals::NodeStore& nodes, uint rootNodeIndex, uint childId)
{
	int childHeight = 32;
	IVector3 lowerBound{ INT_MIN, INT_MIN, INT_MIN };

	uint onlyChildId = childId;
	uint nextNodeIndex = nodes[rootNodeIndex][onlyChildId];
	uint nodeIndex = 0;
	uint childCount = 1;

	while (childCount == 1)
	{
		childHeight--;
		nodeIndex = nextNodeIndex;

		IVector3 childBits = ChildIdToIVec3((int)onlyChildId);
		IVector3 shifted = SHL(childBits, childHeight);
		lowerBound = XORV(lowerBound, shifted);

		childCount = 0;
		for (uint i = 0; i < 8; i++)
		{
			uint childNodeIndex = nodes[nodeIndex][i];
			if (childNodeIndex > 0)
			{
				nextNodeIndex = childNodeIndex;
				childCount++;
				onlyChildId = i;
			}
		}
	}

	SubDAG sub;
	sub.nodeIndex = nodeIndex;
	sub.lowerBound = lowerBound;
	sub.nodeHeight = childHeight;
	sub.padding0 = sub.padding1 = sub.padding2 = 0;
	return sub;
}

SubDAGArray findSubDAGs(const Internals::NodeStore& nodes, uint32_t rootNodeIndex)
{
	SubDAGArray out{};
	for (uint c = 0; c < 8; ++c) out[c] = findSubDAG(nodes, rootNodeIndex, c);
	return out;
}

static SubDAG getSubDAG(const Internals::NodeStore& nodes, uint rootNodeIndex, const SubDAGArray& subDAGs, uint childId)
{
	const int method = 1;
	switch (method) {
	case 1: // 프리컴퓨트 사용
		return subDAGs[childId];
	case 2: // 온디맨드 계산
		return findSubDAG(nodes, rootNodeIndex, childId);
	default: // 바이패스(루트 자식에서 시작)
	{
		SubDAG s{};
		s.lowerBound = IVector3{
			(childId & 0x1) ? 0 : INT_MIN,
			(childId & 0x2) ? 0 : INT_MIN,
			(childId & 0x4) ? 0 : INT_MIN
		};
		s.nodeHeight = RootNodeHeight - 1;
		s.nodeIndex = nodes[rootNodeIndex][childId];
		return s;
	}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////
// 자식 탐색 (near→far) 머티리얼 찾기
////////////////////////////////////////////////////////////////////////////////////////////////

static uint findNearestMaterial(const Internals::NodeStore& nodes, uint nodeIndex, uint rayDirSignBits)
{
	uint nearToFarPacked = 0x76534210; // near→far 순서(3,4 스왑 주의)
	nearToFarPacked |= 0x88888888;     // 각 nibble의 msb를 valid 마커로 사용

	const uint dup = rayDirSignBits * 0x11111111u;
	const uint orderedChildIds = nearToFarPacked ^ dup;

	while (nodeIndex >= MaterialCount)
	{
		for (uint childIds = orderedChildIds; childIds != 0; childIds >>= 4)
		{
			uint cid = childIds & 0x7;
			uint child = nodes[nodeIndex][cid];
			if (child > 0) { nodeIndex = child; break; }
		}
	}
	return nodeIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// ESVO 보조
////////////////////////////////////////////////////////////////////////////////////////////////

static IVector3 findFirstChild(float nodeEntry, const FVector3& rayOrigin, const FVector3& invRayDir, const IVector3& nodeCentre)
{
	// 축 중점까지의 거리
	FVector3 nodeTm = (FVector3{ (float)nodeCentre.x, (float)nodeCentre.y, (float)nodeCentre.z } - rayOrigin) * invRayDir;

	// 이미 중점을 지난 축 판단 → 첫 자식 비트
	IVector3 childId = LT(nodeTm, FVector3{ nodeEntry, nodeEntry, nodeEntry });

	// 카메라 뒤쪽에 걸친 노드는, 레이 시작점 뒤쪽 자식들을 스킵
	if (nodeEntry <= 0.0f) {
		FVector3 originF = rayOrigin;
		FVector3 centreF = FVector3{ (float)nodeCentre.x, (float)nodeCentre.y, (float)nodeCentre.z };
		childId = IVector3{
			childId.x | (originF.x >= centreF.x ? 1 : 0),
			childId.y | (originF.y >= centreF.y ? 1 : 0),
			childId.z | (originF.z >= centreF.z ? 1 : 0)
		};
	}
	return childId;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// ESVO 주 로직
////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_BitScanReverse)
#endif

// 가장 높은 set bit의 인덱스(0..31). v==0이면 -1 반환.
inline int msb_index_u32(uint32_t v)
{
	if (v == 0) return -1;

#if defined(_MSC_VER)
	unsigned long idx;
	_BitScanReverse(&idx, v);        // MSVC: idx is 0..31
	return static_cast<int>(idx);
#elif defined(__GNUC__) || defined(__clang__)
	// __builtin_clz: leading zeros count (32-bit). v!=0 가정.
	return 31 - __builtin_clz(v);
#else
	// 완전한 포터블 폴백
	int idx = 0;
	while (v >>= 1) ++idx;
	return idx;
#endif
}

static RayVolumeIntersection intersectRayNodeESVO(
	const Internals::NodeStore& nodes,
	uint nodeIndex, IVector3 nodePos, int nodeHeight,
	const Ray& ray, const FVector3& rayDirSign, uint rayDirSignBits,
	bool computeSurfaceProperties, float maxFootprint)
{
	RayVolumeIntersection isect{ false, 0.0, 0, FLOAT3{0,0,0}, FLOAT3{0,0,0} };

	uint nodeSize = 1u << nodeHeight;

	FVector3 invRayDir = FVector3::One() / ray.direction;
	FVector3 nodeT0 = (FVector3{ (float)nodePos.x, (float)nodePos.y, (float)nodePos.z } - ray.origin) * invRayDir;
	FVector3 nodeT1 = (FVector3{
		(float)nodePos.x + (float)nodeSize,
		(float)nodePos.y + (float)nodeSize,
		(float)nodePos.z + (float)nodeSize } - ray.origin) * invRayDir;

	float nodeEntry = max3(nodeT0);
	float nodeExit = min3(nodeT1);

	if (nodeEntry < nodeExit)
	{
		const int startHeight = nodeHeight;
		int childNodeSize = int(nodeSize / 2);

		IVector3 childId = findFirstChild(nodeEntry, ray.origin, invRayDir,
			IVector3{ nodePos.x + childNodeSize, nodePos.y + childNodeSize, nodePos.z + childNodeSize });

		IVector3 childPos{
			nodePos.x + childId.x * childNodeSize,
			nodePos.y + childId.y * childNodeSize,
			nodePos.z + childId.z * childNodeSize
		};

		float lastExit = nodeExit;
		uint nodeStack[RootNodeHeight + 1] = { 0 };

		do
		{
			FVector3 childT1 = (FVector3{
				(float)(childPos.x + childNodeSize),
				(float)(childPos.y + childNodeSize),
				(float)(childPos.z + childNodeSize) } - ray.origin) * invRayDir;

			float tChildExit = min3(childT1);
			assert(tChildExit > 0.0f);

			uint childBits = (childId.x & 1u) | ((childId.y & 1u) << 1) | ((childId.z & 1u) << 2);
			uint childNodeIndex = nodes[nodeIndex][childBits ^ rayDirSignBits];

			if (childNodeIndex > 0)
			{
				FVector3 childT0 = (FVector3{
					(float)childPos.x, (float)childPos.y, (float)childPos.z } - ray.origin) * invRayDir;
				float tChildEntry = max3(childT0);

				const bool isInternal = (childNodeIndex >= MaterialCount);
				const bool largeFoot = (maxFootprint > 0.0f) ? ((float)childNodeSize / tChildExit) > maxFootprint : true;

				if (isInternal && largeFoot)
				{
					if (tChildExit < lastExit) { nodeStack[nodeHeight] = nodeIndex; }
					lastExit = tChildExit;

					nodeHeight--;
					nodeIndex = childNodeIndex;
					childNodeSize /= 2;

					IVector3 half{ childNodeSize, childNodeSize, childNodeSize };
					IVector3 centre{ childPos.x + half.x, childPos.y + half.y, childPos.z + half.z };

					childId = findFirstChild(tChildEntry, ray.origin, invRayDir, centre);
					childPos = IVector3{ childPos.x + childId.x * childNodeSize,
										 childPos.y + childId.y * childNodeSize,
										 childPos.z + childId.z * childNodeSize };
				}
				else
				{
					isect.hit = true;
					isect.distance = tChildEntry;

					if (computeSurfaceProperties)
					{
						isect.material = findNearestMaterial(nodes, childNodeIndex, rayDirSignBits);

						FVector3 entry3{ tChildEntry, tChildEntry, tChildEntry };
						FVector3 face = FEQmask(entry3, childT0);
						isect.normal = face * (rayDirSign * -1.0f);
					}
				}
			}
			else
			{
				// 다음 형제(child)로 진행
				FVector3 exit3{ tChildExit, tChildExit, tChildExit };
				IVector3 nextFlip = LE(childT1, exit3); // <= 비교
				IVector3 oldPos = childPos;

				childId = IVector3{ childId.x ^ nextFlip.x, childId.y ^ nextFlip.y, childId.z ^ nextFlip.z };
				childPos = IVector3{ childPos.x + nextFlip.x * childNodeSize,
									 childPos.y + nextFlip.y * childNodeSize,
									 childPos.z + nextFlip.z * childNodeSize };

				// 코너 탈출 시 상위로 점프
				bool okX = ((childId.x & nextFlip.x) == nextFlip.x);
				bool okY = ((childId.y & nextFlip.y) == nextFlip.y);
				bool okZ = ((childId.z & nextFlip.z) == nextFlip.z);
				if (!(okX && okY && okZ))
				{
					IVector3 diff{ oldPos.x ^ childPos.x, oldPos.y ^ childPos.y, oldPos.z ^ childPos.z };
					int combined = diff.x | diff.y | diff.z;

					// unsigned findMSB 대체
					int msb = msb_index_u32(static_cast<uint32_t>(combined));

					nodeHeight = msb + 1;
					assert(nodeHeight <= RootNodeHeight);

					nodeIndex = nodeStack[nodeHeight];
					childNodeSize = 1 << msb;

					IVector3 mask{ 1,1,1 };
					mask = SHL(mask, msb);
					childId = AND1(SHR(childPos, msb), 1);

					IVector3 parentAlign = SHL(SHR(childPos, nodeHeight), nodeHeight);
					childPos = IVector3{ parentAlign.x + childId.x * childNodeSize,
										  parentAlign.y + childId.y * childNodeSize,
										  parentAlign.z + childId.z * childNodeSize };

					lastExit = 0.0f;
				}
			}

		} while (!isect.hit && nodeHeight <= startHeight);
	}

	return isect;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// 전체 볼륨 교차
////////////////////////////////////////////////////////////////////////////////////////////////

RayVolumeIntersection intersectVolume(const Volume& volume, const SubDAGArray& subDAGs,
	float ray_orig_x, float ray_orig_y, float ray_orig_z,
	float ray_dir_x, float ray_dir_y, float ray_dir_z,
	bool computeSurfaceProperties, float maxFootprint)
{
	return intersectVolume(
		volume, subDAGs,
		Ray{ FLOAT3{ray_orig_x, ray_orig_y, ray_orig_z}, FLOAT3{ray_dir_x, ray_dir_y, ray_dir_z} },
		computeSurfaceProperties, maxFootprint);
}

RayVolumeIntersection intersectVolume(const Volume& volume, const SubDAGArray& subDAGs, Ray ray, bool computeSurfaceProperties, float maxFootprint)
{
	RayVolumeIntersection out{ false, 0.0, 0, FLOAT3{0,0,0}, FLOAT3{0,0,0} };

	const Internals::NodeStore& nodes = Internals::getNodes(volume).nodes();
	const uint rootNodeIndex = Internals::getRootNodeIndex(volume);

	IVector3 signBits{ ray.direction.x < 0 ? 1 : 0, ray.direction.y < 0 ? 1 : 0, ray.direction.z < 0 ? 1 : 0 };
	uint rayDirSignBits = (uint)signBits.x | ((uint)signBits.y << 1) | ((uint)signBits.z << 2);
	FVector3 rayDirSign = SignFromBits(signBits);

	// 반사 레이(양의 축 방향)
	Ray r = ray;
	r.origin = r.origin + FVector3{ 0.5f, 0.5f, 0.5f };
	r.origin = r.origin * rayDirSign;
	r.direction = FVector3::Abs(r.direction); // |dir|

	// 시작 옥탄트 결정
	int childId = 0;
	FVector3 distToOrigin = FVector3{ -r.origin.x / r.direction.x, -r.origin.y / r.direction.y, -r.origin.z / r.direction.z };
	if (distToOrigin.x < 0.0f) { childId += 1; distToOrigin.x += FLT_MAX; }
	if (distToOrigin.y < 0.0f) { childId += 2; distToOrigin.y += FLT_MAX; }
	if (distToOrigin.z < 0.0f) { childId += 4; distToOrigin.z += FLT_MAX; }

	do
	{
		SubDAG sub = getSubDAG(nodes, rootNodeIndex, subDAGs, (uint)(childId ^ rayDirSignBits));
		uint childNodeIndex = sub.nodeIndex;

		if (childNodeIndex > 0)
		{
			assert(childNodeIndex >= MaterialCount); // 루트 직속이 풀 머티리얼인 케이스는 TODO

			int nodeSize = int(1u << sub.nodeHeight);

			// 반사축 보정: lowerBound를 반사 및 시프트
			IVector3 refLower = IVector3{
				sub.lowerBound.x * (signBits.x ? -1 : 1),
				sub.lowerBound.y * (signBits.y ? -1 : 1),
				sub.lowerBound.z * (signBits.z ? -1 : 1)
			};
			refLower = IVector3{
				refLower.x - (signBits.x ? nodeSize : 0),
				refLower.y - (signBits.y ? nodeSize : 0),
				refLower.z - (signBits.z ? nodeSize : 0)
			};

			RayVolumeIntersection hit = intersectRayNodeESVO(
				nodes, childNodeIndex, refLower, sub.nodeHeight,
				r, rayDirSign, rayDirSignBits, computeSurfaceProperties, maxFootprint);

			if (hit.hit) {
				// 원래(비반사) 레이로 포지션 복원
				out = hit;
				out.position = ray.origin + ray.direction * (float)hit.distance;
				break;
			}
		}

		// 다음 옥탄트
		float m = min3(distToOrigin);
		if (distToOrigin.x <= m) { childId += 1; distToOrigin.x += FLT_MAX; }
		if (distToOrigin.y <= m) { childId += 2; distToOrigin.y += FLT_MAX; }
		if (distToOrigin.z <= m) { childId += 4; distToOrigin.z += FLT_MAX; }

	} while (childId <= 7);

	return out;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// 참고용 레퍼런스 교차 (트리 방문자)
////////////////////////////////////////////////////////////////////////////////////////////////

struct RayBoxIntersection
{
	explicit operator bool() { return exit >= entry; }

	float entry;
	float exit;
};

RayBoxIntersection intersect(const Ray& ray, const Bounds& box)
{
	// Inverse direction could be precomputed and stored in the ray
	// if we find we often intersect the same ray with multiple boxes.
	const FVector3 invDir = FVector3({ 1.0f, 1.0f, 1.0f }) / ray.direction;

	const FVector3 lower = (box.Min - ray.origin) * invDir;
	const FVector3 upper = (box.Max - ray.origin) * invDir;

	const FVector3 minCorner = FVector3::Min(lower, upper);
	const FVector3 maxCorner = FVector3::Max(lower, upper);

	RayBoxIntersection intersection;
	intersection.entry = FVector3::MinComponent(minCorner);
	intersection.exit = FVector3::MaxComponent(maxCorner);
	return intersection;
}

class IntersectionFinder
{
public:
	explicit IntersectionFinder(const Ray& ray) : mRay(ray)
	{
		mIntersection.hit = false;
		mIntersection.material = 0;
		mIntersection.distance = DBL_MAX;
		mIntersection.position = FLOAT3{ 0,0,0 };
		mIntersection.normal = FLOAT3{ 0,0,0 };
	}

	bool operator()(NodeDAG& nodes, uint32_t nodeIndex, const IBounds& bounds)
	{
		Bounds dilated(
			FVector3{ (float)bounds.Min.x, (float)bounds.Min.y, (float)bounds.Min.z } - FVector3{ 0.5f, 0.5f, 0.5f },
			FVector3{ (float)bounds.Max.x, (float)bounds.Max.y, (float)bounds.Max.z } + FVector3{ 0.5f, 0.5f, 0.5f });
		RayBoxIntersection rb = intersect(mRay, dilated);
		if (!rb) return false;

		if (isMaterialNode(nodeIndex) && (nodeIndex > 0))
		{
			if (rb.exit > 0.0)
			{
				if (rb.entry < mIntersection.distance)
				{
					mIntersection.hit = true;
					mIntersection.distance = rb.entry;
					mIntersection.material = static_cast<MaterialId>(nodeIndex);
				}
			}
		}
		return true;
	}

public:
	Ray mRay;
	RayVolumeIntersection mIntersection;
};

RayVolumeIntersection traceRayRef(Volume& volume,
	float rx, float ry, float rz,
	float dx, float dy, float dz)
{
	return traceRayRef(volume, Ray{ FLOAT3{rx,ry,rz}, FLOAT3{dx,dy,dz} });
}

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
		if (((~childId) >> 0) & 0x01) childBounds.Min.x ^= bitToFlip;
		else childBounds.Max.x ^= bitToFlip;

		if (((~childId) >> 1) & 0x01) childBounds.Min.y ^= bitToFlip;
		else childBounds.Max.y ^= bitToFlip;

		if (((~childId) >> 2) & 0x01) childBounds.Min.z ^= bitToFlip;
		else childBounds.Max.z ^= bitToFlip;

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

// FIXME - Can we make this take a const volume reference?
template<typename Functor>
void visitVolumeNodes(Volume& volume, Functor&& callback)
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

RayVolumeIntersection traceRayRef(Volume& volume, Ray ray)
{
	IntersectionFinder finder(ray);
	visitVolumeNodes(volume, finder);
	return finder.mIntersection;
}


static inline int mapToGrid(uint32_t p, uint32_t P, int D)
{
	// 픽셀 중심 → 최근접 정수 인덱스
	float t = (float(p) + 0.5f) / float(P);
	int idx = (int)std::floor(t * (float)D);
	if (idx < 0) idx = 0;
	if (idx > D - 1) idx = D - 1;
	return idx;
}

void ProjectVolumeFace(
	const Volume& volume,
	uint8_t face,
	uint maxSize,
	Image* outDiffuseImage,
	Image* outDepthImage)
{
	if (!outDiffuseImage || !outDepthImage)
		return;

	// 1) 점유 바운즈
	const IBounds& bounds = volume.getOccupiedBounds();
	int32_t minx = bounds.Min.x;
	int32_t miny = bounds.Min.y;
	int32_t minz = bounds.Min.z;
	int32_t maxx = bounds.Max.x;
	int32_t maxy = bounds.Max.y;
	int32_t maxz = bounds.Max.z;

	// 비어있음 → 1x1 반환
	if (minx > maxx || miny > maxy || minz > maxz)
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
	const int dimX = int((int64_t)maxx - (int64_t)minx + 1);
	const int dimY = int((int64_t)maxy - (int64_t)miny + 1);
	const int dimZ = int((int64_t)maxz - (int64_t)minz + 1);

	// 3) face별 축 매핑/플립/스캔 방향 (CASPER과 동일한 컨벤션)
	int projAxis = 0, axisU = 0, axisV = 1;
	int stepSign = +1;                 // +면 +축으로 레이 쏨, -면 -축
	bool flipU = false, flipV = false;

	auto dimOf = [&](int axis)->int {
		return (axis == 0) ? dimX : (axis == 1) ? dimY : dimZ;
		};
	auto minOf = [&](int axis)->int32_t {
		return (axis == 0) ? minx : (axis == 1) ? miny : minz;
		};
	auto maxOf = [&](int axis)->int32_t {
		return (axis == 0) ? maxx : (axis == 1) ? maxy : maxz;
		};

	switch (face)
	{
	case 0: /*NEGX*/ projAxis = 0; axisU = 2; axisV = 1; stepSign = +1; flipU = false; flipV = true;  break;
	case 1: /*POSX*/ projAxis = 0; axisU = 2; axisV = 1; stepSign = -1; flipU = true;  flipV = true;  break;
	case 2: /*NEGY*/ projAxis = 1; axisU = 0; axisV = 2; stepSign = +1; flipU = false; flipV = true;  break;
	case 3: /*POSY*/ projAxis = 1; axisU = 0; axisV = 2; stepSign = -1; flipU = false; flipV = false; break;
	case 4: /*NEGZ*/ projAxis = 2; axisU = 0; axisV = 1; stepSign = +1; flipU = true;  flipV = true;  break;
	case 5: /*POSZ*/ projAxis = 2; axisU = 0; axisV = 1; stepSign = -1; flipU = false; flipV = true;  break;
	default: break;
	}

	const int dimS = dimOf(projAxis);
	const int dimU = dimOf(axisU);
	const int dimV = dimOf(axisV);

	// 4) 출력 해상도 결정(정사각, 상한 maxSize)
	const uint32_t W = std::max(1u, std::min<uint32_t>(maxSize, (uint32_t)std::max(dimZ, std::max(dimX, dimY))));
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
	const float startPlane = (stepSign > 0) ? (float)minOf(projAxis) : (float)maxOf(projAxis);
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
			int32_t coordU = (axisU == 0 ? minx : axisU == 1 ? miny : minz) + gridU;
			int32_t coordV = (axisV == 0 ? minx : axisV == 1 ? miny : minz) + gridV;

			// 원점 구성: 투영축 좌표=startPlane, 나머지 두 축은 (coordU, coordV)
			FLOAT3 origin{};
			origin.x = (projAxis == 0) ? startPlane : (axisU == 0 ? (float)coordU : (axisV == 0 ? (float)coordV : (float)minx));
			origin.y = (projAxis == 1) ? startPlane : (axisU == 1 ? (float)coordU : (axisV == 1 ? (float)coordV : (float)miny));
			origin.z = (projAxis == 2) ? startPlane : (axisU == 2 ? (float)coordU : (axisV == 2 ? (float)coordV : (float)minz));

			// 방향 벡터: 투영축으로 ±1
			FLOAT3 dir{ 0,0,0 };
			if (projAxis == 0) dir.x = dirSign;
			else if (projAxis == 1) dir.y = dirSign;
			else dir.z = dirSign;

			// ESVO 교차 (표면 속성 불필요: false)
			Ray ray{ origin, dir };
			RayVolumeIntersection hit = intersectVolume(volume, subDAGs, ray, /*computeSurfaceProperties*/false, /*maxFootprint*/MAX_FOOTPRINT_DISABLED);

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

				c[0] = 200; c[1] = 200; c[2] = 255; c[3] = 255;
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