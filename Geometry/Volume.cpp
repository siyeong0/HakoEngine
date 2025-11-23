#include "pch.h"
#include "Volume.h"

namespace
{
	constexpr uint32_t TOPO_HAS_CHILDREN = 0x80000000u; // node has children
	constexpr uint32_t TOPO_SOLID_LEAF = 0x40000000u;  // solid leaf node
	constexpr uint32_t TOPO_FLAG_MASK = 0xC0000000u;  // top 2 bits
	constexpr uint32_t TOPO_CHILD_MASK = 0x3FFFFFFFu;  // lower 30 bits for child base index

	constexpr uint32_t INVALID_NODE_INDEX = 0xFFFFFFFFu;
}

Volume::Volume()
	: m_CellSize(DEFAULT_CELL_SIZE)
	, m_Bounds()
{
	m_Nodes.reserve(DEFAULT_NODE_CAPACITY);
	m_Nodes.emplace_back(); // root node
}

Volume::Volume(float cellSize)
	: m_CellSize(cellSize)
	, m_Bounds()
{
	m_Nodes.reserve(DEFAULT_NODE_CAPACITY);
	m_Nodes.emplace_back(); // root node
}

Volume::~Volume()
{
	// Cleanup if necessary
}

Bounds Volume::GetOctreeBounds() const
{
	// The octree covers a cube whose edge length is (2^MAX_OCTREE_DEPTH) * cellSize,
	// centered at the origin.
	float maxOctreeSize = static_cast<float>(1 << MAX_OCTREE_DEPTH) * m_CellSize;

	Bounds maxOctreeBounds;
	maxOctreeBounds.Min = 0.5f * FVector3(-maxOctreeSize, -maxOctreeSize, -maxOctreeSize);
	maxOctreeBounds.Max = 0.5f * FVector3(+maxOctreeSize, +maxOctreeSize, +maxOctreeSize);
	return maxOctreeBounds;
}

void Volume::ApplyUnion(const std::function<float(const FVector3& pos)>& sdf)
{
	constexpr uint32_t HAS_CHILDREN_BIT = 1u << 31;
	constexpr uint32_t OCCUPIED_BIT = 1u << 30;
	constexpr uint32_t CHILD_INDEX_MASK = 0x3FFFFFFFu;

	// Snapshot of the current volume (old volume).
	std::vector<VolumeNode> oldNodes = m_Nodes;

	// New node array that will store the union result.
	std::vector<VolumeNode> newNodes;
	newNodes.reserve(oldNodes.size());
	newNodes.emplace_back(VolumeNode{ 0u, 0u }); // new root at index 0

	Bounds maxOctreeBounds = GetOctreeBounds();

	struct NodeEntry
	{
		uint32_t OldIndex;   // index in oldNodes, or INVALID_NODE_INDEX
		uint32_t NewIndex;   // index in newNodes
		Bounds   NodeBounds; // world-space bounds of this node
		int      Depth;      // tree depth (root = 0)
	};

	std::vector<NodeEntry> stack;
	stack.reserve(1024);
	stack.push_back({ 0u, 0u, maxOctreeBounds, 0 });

	bool   hasAny = false;
	Bounds occupiedBounds{}; // combined bounds of all occupied leaves

	while (!stack.empty())
	{
		NodeEntry entry = stack.back();
		stack.pop_back();

		const Bounds& b = entry.NodeBounds;
		const int     dep = entry.Depth;

		VolumeNode& newNode = newNodes[entry.NewIndex];

		// --------------------------
		// Read old node state
		// --------------------------
		VolumeNode oldNode{ 0u, 0u };
		bool hasOld = (entry.OldIndex != INVALID_NODE_INDEX) &&
			(entry.OldIndex < oldNodes.size());

		if (hasOld)
		{
			oldNode = oldNodes[entry.OldIndex];
		}

		bool oldHasChildren = hasOld && (oldNode.TopologyBits & HAS_CHILDREN_BIT);
		bool oldLeafSolid = hasOld && (oldNode.TopologyBits & OCCUPIED_BIT);
		bool oldSubtreeEmpty = !oldHasChildren && !oldLeafSolid;

		// --------------------------
		// SDF classification (Lipschitz-based)
		// --------------------------
		FVector3 center = b.Center();
		FVector3 extents = b.Extents();

		// half-diagonal length of this node AABB
		float r = std::sqrt(FVector3::Dot(extents, extents));

		float d = sdf(center);   // signed distance at center
		float minD = d - r;         // conservative minimum in the box
		float maxD = d + r;         // conservative maximum in the box

		bool bFullyOutside = (minD > 0.0f);
		bool bFullyInside = (maxD < 0.0f);
		bool bCenterInside = (d <= 0.0f);

		// ---------------------------------------------------------------------
		// Case 1: Node is completely outside SDF and old subtree is empty
		// => this region contributes nothing to the union.
		// ---------------------------------------------------------------------
		if (bFullyOutside && oldSubtreeEmpty)
		{
			newNode.TopologyBits = 0u;
			newNode.BrickBits = 0u;
			continue;
		}

		// ---------------------------------------------------------------------
		// Leaf: maximum depth reached
		// ---------------------------------------------------------------------
		if (dep >= MAX_OCTREE_DEPTH)
		{
			// Old occupancy at this leaf
			bool oldOccupied = oldLeafSolid;

			// SDF occupancy at this leaf (we can use center test)
			bool sdfOccupied = bCenterInside || bFullyInside;

			// Union: either old volume or SDF says this cell is occupied
			bool resultOccupied = oldOccupied || sdfOccupied;

			if (resultOccupied)
			{
				newNode.TopologyBits = OCCUPIED_BIT;
				newNode.BrickBits = 0u;

				if (!hasAny)
				{
					occupiedBounds = b;
					hasAny = true;
				}
				else
				{
					occupiedBounds.Encapsulate(b);
				}
			}
			else
			{
				newNode.TopologyBits = 0u;
				newNode.BrickBits = 0u;
			}

			continue;
		}

		// ---------------------------------------------------------------------
		// Internal node: either we have old data, SDF overlap, or both.
		// We must subdivide unless SDF is fully inside and we want to early-out.
		//
		// To keep the structure consistent (all leaves at MAX_OCTREE_DEPTH),
		// we still subdivide fully inside regions as well.
		// ---------------------------------------------------------------------

		// Allocate 8 children in the new tree.
		uint32_t firstChildIndex = static_cast<uint32_t>(newNodes.size());
		uint32_t childIndexBits = (firstChildIndex & CHILD_INDEX_MASK);

		newNode.TopologyBits = HAS_CHILDREN_BIT | childIndexBits;
		newNode.BrickBits = 0u;

		for (int i = 0; i < 8; ++i)
		{
			newNodes.emplace_back(VolumeNode{ 0u, 0u });
		}

		// Prepare child bounds
		FVector3 parentCenter = b.Center();
		FVector3 parentExtents = b.Extents();
		FVector3 childExtents = parentExtents * 0.5f;

		// Old children base index (if they exist)
		uint32_t oldChildBase = 0;
		if (oldHasChildren)
		{
			oldChildBase = (oldNode.TopologyBits & CHILD_INDEX_MASK);
		}

		for (uint32_t i = 0; i < 8; ++i)
		{
			float sx = (i & 1u) ? +1.0f : -1.0f;
			float sy = (i & 2u) ? +1.0f : -1.0f;
			float sz = (i & 4u) ? +1.0f : -1.0f;

			FVector3 childCenter =
			{
				parentCenter.x + sx * childExtents.x,
				parentCenter.y + sy * childExtents.y,
				parentCenter.z + sz * childExtents.z
			};

			Bounds childBounds;
			childBounds.Min =
			{
				childCenter.x - childExtents.x,
				childCenter.y - childExtents.y,
				childCenter.z - childExtents.z
			};
			childBounds.Max =
			{
				childCenter.x + childExtents.x,
				childCenter.y + childExtents.y,
				childCenter.z + childExtents.z
			};

			// Look up old child state, if any
			uint32_t oldChildIndex = INVALID_NODE_INDEX;
			bool     oldChildNonEmpty = false;

			if (oldHasChildren)
			{
				oldChildIndex = oldChildBase + i;
				if (oldChildIndex < oldNodes.size())
				{
					uint32_t oldTopo = oldNodes[oldChildIndex].TopologyBits;
					oldChildNonEmpty = (oldTopo & (HAS_CHILDREN_BIT | OCCUPIED_BIT)) != 0;
				}
			}

			// SDF classification for the child (for culling)
			FVector3 childCenterLocal = childBounds.Center();
			FVector3 childExtentsLocal = childBounds.Extents();

			float rChild = std::sqrt(FVector3::Dot(childExtentsLocal, childExtentsLocal));
			float dChild = sdf(childCenterLocal);
			float minChildD = dChild - rChild;
			float maxChildD = dChild + rChild;

			bool childFullyOutside = (minChildD > 0.0f);

			// If the child region is fully outside SDF AND the old child is empty,
			// then the child contributes nothing to the union -> skip it.
			if (childFullyOutside && !oldChildNonEmpty)
				continue;

			// Otherwise, recurse into this child.
			stack.push_back(
				{
					oldChildIndex,              // index in oldNodes (if any)
					firstChildIndex + i,        // index in newNodes
					childBounds,
					dep + 1
				}
			);
		}
	}

	// Replace current nodes with the newly built union volume.
	m_Nodes.swap(newNodes);

	if (hasAny)
	{
		m_Bounds = occupiedBounds;
	}
	// If hasAny == false, the union is empty; you can optionally reset m_Bounds here.
}

void Volume::ApplyDifference(const std::function<float(const FVector3& pos)>& sdf)
{
	constexpr uint32_t HAS_CHILDREN_BIT = 1u << 31;
	constexpr uint32_t OCCUPIED_BIT = 1u << 30;
	constexpr uint32_t CHILD_INDEX_MASK = 0x3FFFFFFFu;

	// Snapshot of the current volume (old volume).
	std::vector<VolumeNode> oldNodes = m_Nodes;

	// New node array that will store the "old minus SDF" result.
	std::vector<VolumeNode> newNodes;
	newNodes.reserve(oldNodes.size());
	newNodes.emplace_back(VolumeNode{ 0u, 0u }); // new root at index 0

	Bounds maxOctreeBounds = GetOctreeBounds();

	struct NodeEntry
	{
		uint32_t OldIndex;   // index in oldNodes, or INVALID_NODE_INDEX
		uint32_t NewIndex;   // index in newNodes
		Bounds   NodeBounds; // world-space bounds of this node
		int      Depth;      // tree depth (root = 0)
	};

	std::vector<NodeEntry> stack;
	stack.reserve(1024);
	stack.push_back({ 0u, 0u, maxOctreeBounds, 0 });

	bool   hasAny = false;
	Bounds occupiedBounds{}; // combined bounds of all remaining occupied leaves

	while (!stack.empty())
	{
		NodeEntry entry = stack.back();
		stack.pop_back();

		const Bounds& b = entry.NodeBounds;
		const int     dep = entry.Depth;

		VolumeNode& newNode = newNodes[entry.NewIndex];

		// ---------------------------------------------------------------------
		// Read old node state
		// ---------------------------------------------------------------------
		VolumeNode oldNode{ 0u, 0u };
		bool hasOld = (entry.OldIndex != INVALID_NODE_INDEX) &&
			(entry.OldIndex < oldNodes.size());

		if (hasOld)
		{
			oldNode = oldNodes[entry.OldIndex];
		}

		const bool oldHasChildren = hasOld && (oldNode.TopologyBits & HAS_CHILDREN_BIT);
		const bool oldLeafSolid = hasOld && (oldNode.TopologyBits & OCCUPIED_BIT);
		const bool oldSubtreeEmpty = !oldHasChildren && !oldLeafSolid;

		// If there was no geometry in the old volume, this region is empty
		// regardless of the SDF: nothing to subtract.
		if (oldSubtreeEmpty)
		{
			newNode.TopologyBits = 0u;
			newNode.BrickBits = 0u;
			continue;
		}

		// ---------------------------------------------------------------------
		// SDF classification (Lipschitz-based)
		// ---------------------------------------------------------------------
		FVector3 center = b.Center();
		FVector3 extents = b.Extents();

		// Half-diagonal length of this node AABB.
		float r = std::sqrt(FVector3::Dot(extents, extents));

		float d = sdf(center);   // signed distance at center
		float minD = d - r;         // conservative minimum in the box
		float maxD = d + r;         // conservative maximum in the box

		bool bFullyOutside = (minD > 0.0f); // SDF shape is fully outside this node
		bool bFullyInside = (maxD < 0.0f); // SDF shape fully covers this node
		bool bCenterInside = (d <= 0.0f);   // center is inside SDF

		// ---------------------------------------------------------------------
		// Case 1: SDF volume fully covers this node region.
		// In "difference" (old - SDF), this means the entire old subtree is removed.
		// ---------------------------------------------------------------------
		if (bFullyInside)
		{
			newNode.TopologyBits = 0u;
			newNode.BrickBits = 0u;
			continue;
		}

		// ---------------------------------------------------------------------
		// Leaf: maximum depth reached
		// ---------------------------------------------------------------------
		if (dep >= MAX_OCTREE_DEPTH)
		{
			// Old occupancy at this leaf
			bool oldOccupied = oldLeafSolid;

			// SDF occupancy inside this cell
			bool sdfInsideCell;
			if (bFullyOutside)
			{
				// SDF does not touch this leaf region at all.
				sdfInsideCell = false;
			}
			else if (bFullyInside)
			{
				// Already handled above, but keep for completeness.
				sdfInsideCell = true;
			}
			else
			{
				// Partial overlap: fall back to center test.
				sdfInsideCell = bCenterInside;
			}

			// Difference: keep voxel only if it was occupied AND not inside SDF.
			bool resultOccupied = oldOccupied && !sdfInsideCell;

			if (resultOccupied)
			{
				newNode.TopologyBits = OCCUPIED_BIT;
				newNode.BrickBits = 0u;

				if (!hasAny)
				{
					occupiedBounds = b;
					hasAny = true;
				}
				else
				{
					occupiedBounds.Encapsulate(b);
				}
			}
			else
			{
				newNode.TopologyBits = 0u;
				newNode.BrickBits = 0u;
			}

			continue;
		}

		// ---------------------------------------------------------------------
		// Internal node:
		//  - We know there is some old geometry here (oldSubtreeEmpty == false).
		//  - SDF is not fully covering this node (bFullyInside == false).
		//  => We need to descend into children where old geometry exists.
		// ---------------------------------------------------------------------

		// Allocate 8 children in the new tree.
		uint32_t firstChildIndex = static_cast<uint32_t>(newNodes.size());
		uint32_t childIndexBits = (firstChildIndex & CHILD_INDEX_MASK);

		newNode.TopologyBits = HAS_CHILDREN_BIT | childIndexBits;
		newNode.BrickBits = 0u;

		for (int i = 0; i < 8; ++i)
		{
			newNodes.emplace_back(VolumeNode{ 0u, 0u });
		}

		// Parent bounds decomposition into 8 child bounds.
		FVector3 parentCenter = b.Center();
		FVector3 parentExtents = b.Extents();
		FVector3 childExtents = parentExtents * 0.5f;

		// Old children base index (if they exist)
		uint32_t oldChildBase = 0;
		if (oldHasChildren)
		{
			oldChildBase = (oldNode.TopologyBits & CHILD_INDEX_MASK);
		}

		for (uint32_t i = 0; i < 8; ++i)
		{
			float sx = (i & 1u) ? +1.0f : -1.0f;
			float sy = (i & 2u) ? +1.0f : -1.0f;
			float sz = (i & 4u) ? +1.0f : -1.0f;

			FVector3 childCenter =
			{
				parentCenter.x + sx * childExtents.x,
				parentCenter.y + sy * childExtents.y,
				parentCenter.z + sz * childExtents.z
			};

			Bounds childBounds;
			childBounds.Min =
			{
				childCenter.x - childExtents.x,
				childCenter.y - childExtents.y,
				childCenter.z - childExtents.z
			};
			childBounds.Max =
			{
				childCenter.x + childExtents.x,
				childCenter.y + childExtents.y,
				childCenter.z + childExtents.z
			};

			// Look up old child state, if any.
			uint32_t oldChildIndex = INVALID_NODE_INDEX;
			bool     oldChildNonEmpty = false;

			if (oldHasChildren)
			{
				oldChildIndex = oldChildBase + i;
				if (oldChildIndex < oldNodes.size())
				{
					uint32_t oldTopo = oldNodes[oldChildIndex].TopologyBits;
					oldChildNonEmpty = (oldTopo & (HAS_CHILDREN_BIT | OCCUPIED_BIT)) != 0;
				}
			}

			// If the old child is empty, there is nothing to subtract or preserve.
			if (!oldChildNonEmpty)
				continue;

			// SDF classification for the child region (for early-out culling).
			FVector3 childCenterLocal = childBounds.Center();
			FVector3 childExtentsLocal = childBounds.Extents();

			float rChild = std::sqrt(FVector3::Dot(childExtentsLocal, childExtentsLocal));
			float dChild = sdf(childCenterLocal);
			float minChildD = dChild - rChild;
			float maxChildD = dChild + rChild;

			bool childFullyInside = (maxChildD < 0.0f);
			// bool childFullyOutside = (minChildD > 0.0f); // could be used, but not required

			// If SDF fully covers this child region, the entire old child is removed.
			if (childFullyInside)
				continue;

			// Otherwise, recurse into this child.
			stack.push_back(
				{
					oldChildIndex,              // index in oldNodes
					firstChildIndex + i,        // index in newNodes
					childBounds,
					dep + 1
				}
			);
		}
	}

	// Replace current nodes with the newly built "difference" volume.
	m_Nodes.swap(newNodes);

	if (hasAny)
	{
		m_Bounds = occupiedBounds;
	}
	// If hasAny == false, the volume became completely empty; you can reset m_Bounds here if desired.
}


// ---------------------------------------------------------
// Static factory functions
// ---------------------------------------------------------

// Unit cube: [-0.5, 0.5]^3
Volume Volume::CreateUnitCube(float cellSize)
{
	return CreateBox(1.0f, 1.0f, 1.0f, cellSize);
}

Volume Volume::CreateBox(float width, float height, float depth, float cellSize)
{
	Volume volume(cellSize);

	Bounds boxBounds;
	boxBounds.Min = FVector3(-0.5f * width, -0.5f * height, -0.5f * depth);
	boxBounds.Max = FVector3(+0.5f * width, +0.5f * height, +0.5f * depth);

	// Apply box SDF
	volume.ApplyUnion([&boxBounds](const FVector3& p)
		{
			// Compute SDF for axis-aligned box
			FVector3 q = FVector3::Abs(p - boxBounds.Center()) - boxBounds.Extents();
			FVector3 maxPart = FVector3::Max(q, FVector3::Zero());
			float outsideDist = std::sqrt(FVector3::Dot(maxPart, maxPart));
			float insideDist = std::min(FVector3::MaxComponent(q), 0.0f);
			return outsideDist + insideDist;
		});

	return volume;
}

Volume Volume::CreateSphere(float radius, float cellSize)
{
	Volume volume(cellSize);

	FVector3 center(0.0f, 0.0f, 0.0f);

	volume.ApplyUnion([center, radius](const FVector3& p)
		{
			float d = std::sqrt(FVector3::Dot(p - center, p - center));
			return d - radius;
		});

	return volume;
}


Volume Volume::CreatePlane(float width, float height, float cellSize)
{
	// plane = thin box with height = cellSize
	return CreateBox(width, height, cellSize, cellSize);
}

Volume Volume::CreateCylinder(float radius, float height, float cellSize)
{
	Volume volume(cellSize);

	float halfH = height * 0.5f;

	volume.ApplyUnion([radius, halfH](const FVector3& p)
		{
			// Radial distance in XZ plane (Y-axis is the cylinder axis)
			float radial = std::sqrt(p.x * p.x + p.z * p.z);
			float yAbs = std::abs(p.y);

			// Signed distances along radial and vertical directions
			float dx = radial - radius; // radial direction (XZ)
			float dy = yAbs - halfH;    // height (Y)

			// Outside components
			float ox = std::max(dx, 0.0f);
			float oy = std::max(dy, 0.0f);

			float outsideDist = std::sqrt(ox * ox + oy * oy);

			// Inside distance
			float insideDist = std::min(std::max(dx, dy), 0.0f);

			return outsideDist + insideDist;
		});

	return volume;
}


Volume Volume::CreateCone(float radius, float height, float cellSize)
{
	Volume volume(cellSize);

	float halfH = height * 0.5f;

	volume.ApplyUnion([radius, halfH, height](const FVector3& p)
		{
			// We build a cone whose axis is the Y axis.
			// - Y range: [-halfH, +halfH]
			// - Radius at y = -halfH is 'radius'
			// - Radius at y = +halfH is 0 (apex)

			float y = p.y;

			// Radial distance in XZ plane
			float radial = std::sqrt(p.x * p.x + p.z * p.z);

			// Normalized height parameter:
			//   t = 0 at apex (y = +halfH)
			//   t = 1 at base (y = -halfH)
			float t = (halfH - y) / height;
			t = std::clamp(t, 0.0f, 1.0f);

			// Radius of the cone at height y
			float rAtY = radius * t;

			// Side condition: radial <= rAtY
			float sSide = radial - rAtY;

			// Height slab condition: -halfH <= y <= +halfH
			float sHeight = std::abs(y) - halfH;

			// Approximate SDF for the finite cone as
			// the intersection of side and height conditions.
			// Inside region is where both are <= 0.
			return std::max(sSide, sHeight);
		});

	return volume;
}

