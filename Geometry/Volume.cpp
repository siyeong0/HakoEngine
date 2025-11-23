#include "pch.h"
#include "SDF.h"
#include "Volume.h"

namespace
{
	constexpr uint32_t HAS_CHILDREN_BIT = 1u << 31; // node has children
	constexpr uint32_t OCCUPIED_BIT = 1u << 30;  // solid leaf node
	constexpr uint32_t CHILD_INDEX_MASK = 0x3FFFFFFFu;  // top 2 bits

	constexpr uint32_t INVALID_NODE_INDEX = 0xFFFFFFFFu;
}

Volume::Volume()
	: m_CellSize(DEFAULT_CELL_SIZE)
	, m_PseudoOccupiedBounds()
{
	m_Nodes.reserve(DEFAULT_NODE_CAPACITY);
	m_Nodes.emplace_back(); // root node
}

Volume::Volume(float cellSize)
	: m_CellSize(cellSize)
	, m_PseudoOccupiedBounds()
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
	// Ensure the volume has at least a root node.
	if (m_Nodes.empty())
	{
		m_Nodes.emplace_back(VolumeNode{ 0u, 0u });
	}

	struct NodeEntry
	{
		uint32_t Index;
		Bounds NodeBounds;
		int Depth;
	};

	std::vector<NodeEntry> stack;
	stack.reserve(1024);
	stack.emplace_back(NodeEntry{ 0u, GetOctreeBounds(), 0 });

	while (!stack.empty())
	{
		NodeEntry entry = stack.back();
		stack.pop_back();

		VolumeNode& currNode = m_Nodes[entry.Index];
		const Bounds& currBounds = entry.NodeBounds;
		const int currDepth = entry.Depth;

		// ------------------------------------------------------------
		// Read existing node state
		// ------------------------------------------------------------
		uint32_t topoWord = currNode.TopologyBits;
		bool bHasChildren = (topoWord & HAS_CHILDREN_BIT) != 0;
		bool bLeafSolid = (topoWord & OCCUPIED_BIT) != 0;
		bool bSubtreeEmpty = !bHasChildren && !bLeafSolid;

		// ------------------------------------------------------------
		// Conservative SDF classification using Lipschitz bound
		// ------------------------------------------------------------
		const FVector3 center = currBounds.Center();
		const FVector3 extents = currBounds.Extents();

		float r = std::sqrt(FVector3::Dot(extents, extents)); // half-diagonal length
		float d = sdf(center);
		float minD = d - r;
		float maxD = d + r;

		bool bFullyOutside = (minD > 0.0f);
		bool bFullyInside = (maxD < 0.0f);
		bool bCenterInside = (d <= 0.0f);

		// ------------------------------------------------------------
		// If SDF is fully outside AND the existing subtree is empty,
		// this region contributes nothing to the union.
		// ------------------------------------------------------------
		if (bFullyOutside && bSubtreeEmpty)
		{
			currNode.TopologyBits = 0u;
			currNode.BrickBits = 0u;
			continue;
		}

		// ------------------------------------------------------------
		// Leaf reached (max resolution)
		// ------------------------------------------------------------
		if (currDepth >= MAX_OCTREE_DEPTH)
		{
			bool bOldOccupied = bLeafSolid;
			bool bSdfOccupied = bCenterInside || bFullyInside;

			bool bResultOccupied = bOldOccupied || bSdfOccupied;

			if (bResultOccupied)
			{
				currNode.TopologyBits = OCCUPIED_BIT;
				currNode.BrickBits = 0u;

				m_PseudoOccupiedBounds.Encapsulate(currBounds);
			}
			else
			{
				currNode.TopologyBits = 0u;
				currNode.BrickBits = 0u;
			}

			continue;
		}

		// ------------------------------------------------------------
		// If SDF is fully outside but the subtree is not empty,
		// keep the existing content as part of the union.
		// No need to traverse children.
		// ------------------------------------------------------------
		if (bFullyOutside && !bSubtreeEmpty)
		{
			continue;
		}

		// ------------------------------------------------------------
		// At this point SDF intersects this node's region (or at least
		// cannot be proven fully outside). If no children exist yet,
		// allocate them.
		// ------------------------------------------------------------
		uint32_t childBase = 0;
		if (!bHasChildren)
		{
			childBase = static_cast<uint32_t>(m_Nodes.size());
			currNode.TopologyBits = HAS_CHILDREN_BIT | (childBase & CHILD_INDEX_MASK);
			currNode.BrickBits = 0u;

			for (int i = 0; i < 8; ++i)
			{
				m_Nodes.emplace_back(VolumeNode{ 0u, 0u });
			}

			bHasChildren = true;
		}
		else
		{
			childBase = (topoWord & CHILD_INDEX_MASK);
		}

		// ------------------------------------------------------------
		// Subdivide the node and push all children for further processing.
		// Each child will run its own SDF classification when popped.
		// This ensures we only evaluate sdf() once per node.
		// ------------------------------------------------------------
		const FVector3 parentCenter = center;
		const FVector3 parentExtents = extents;
		const FVector3 childExtents = parentExtents * 0.5f;

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

			uint32_t childIndex = childBase + i;

			stack.emplace_back(NodeEntry{ childIndex, childBounds, currDepth + 1 });
		}
	}

	// If bAnyNew == false, the union did not introduce new occupied cells.
	// Existing volume remains valid.
}


void Volume::ApplyDifference(const std::function<float(const FVector3& pos)>& sdf)
{
	// If there is no tree yet, there is nothing to subtract from.
	if (m_Nodes.empty())
	{
		ASSERT(false, "Volume has no data to subtract from.");
		return;
	}

	struct NodeEntry
	{
		uint32_t Index;
		Bounds NodeBounds;
		int Depth; // root = 0
	};

	std::vector<NodeEntry> stack;
	stack.reserve(1024);
	stack.emplace_back(NodeEntry{ 0u, GetOctreeBounds(), 0 });

	while (!stack.empty())
	{
		NodeEntry entry = stack.back();
		stack.pop_back();

		VolumeNode& currNode = m_Nodes[entry.Index];
		const Bounds& currBounds = entry.NodeBounds;
		const int currDepth = entry.Depth;

		// ------------------------------------------------------------
		// Read existing node state
		// ------------------------------------------------------------
		uint32_t topoWord = currNode.TopologyBits;
		bool bHasChildren = (topoWord & HAS_CHILDREN_BIT) != 0;
		bool bLeafSolid = (topoWord & OCCUPIED_BIT) != 0;
		bool bSubtreeEmpty = !bHasChildren && !bLeafSolid;

		// If the original subtree is empty, there is nothing to subtract or keep.
		if (bSubtreeEmpty)
		{
			currNode.TopologyBits = 0u;
			currNode.BrickBits = 0u;
			continue;
		}

		// ------------------------------------------------------------
		// Conservative SDF classification using Lipschitz bound
		// ------------------------------------------------------------
		const FVector3 center = currBounds.Center();
		const FVector3 extents = currBounds.Extents();

		float r = std::sqrt(FVector3::Dot(extents, extents)); // half-diagonal length
		float d = sdf(center);
		float minD = d - r;
		float maxD = d + r;

		bool bFullyOutside = (minD > 0.0f);  // SDF does not intersect this node's AABB
		bool bFullyInside = (maxD < 0.0f);  // SDF fully covers this node's AABB
		bool bCenterInside = (d <= 0.0f);    // SDF contains the center

		// ------------------------------------------------------------
		// If the SDF completely covers this node, remove the whole subtree.
		// In "difference" (old - SDF) this region becomes empty.
		// ------------------------------------------------------------
		if (bFullyInside)
		{
			currNode.TopologyBits = 0u;
			currNode.BrickBits = 0u;
			continue;
		}

		// ------------------------------------------------------------
		// Leaf: maximum depth reached
		// ------------------------------------------------------------
		if (currDepth >= MAX_OCTREE_DEPTH)
		{
			// Original occupancy at this leaf
			bool bOldOccupied = bLeafSolid;

			// Decide whether the SDF occupies this leaf cell.
			bool bSdfInsideCell = bCenterInside;

			// Difference: keep voxel only if it was occupied AND not inside SDF.
			bool bResultOccupied = bOldOccupied && !bSdfInsideCell;

			if (bResultOccupied)
			{
				currNode.TopologyBits = OCCUPIED_BIT;
				currNode.BrickBits = 0u;
			}
			else
			{
				currNode.TopologyBits = 0u;
				currNode.BrickBits = 0u;
			}

			continue;
		}

		// ------------------------------------------------------------
		// Internal node:
		//  - We know there is some existing geometry here (bSubtreeEmpty == false).
		//  - SDF does not fully cover this node (bFullyInside == false).
		//  - If SDF is fully outside, nothing is removed -> keep subtree as-is.
		//    No need to visit children.
		// ------------------------------------------------------------
		if (bFullyOutside)
		{
			// Current subtree remains untouched; skip children.
			continue;
		}

		// ------------------------------------------------------------
		// SDF partially overlaps this node. We must descend into children
		// to selectively remove voxels.
		// Ensure this node has 8 children so we can represent the remaining geometry.
		// ------------------------------------------------------------
		uint32_t childBase = 0;
		if (!bHasChildren)
		{
			childBase = static_cast<uint32_t>(m_Nodes.size());
			currNode.TopologyBits = HAS_CHILDREN_BIT | (childBase & CHILD_INDEX_MASK);
			currNode.BrickBits = 0u;

			for (int i = 0; i < 8; ++i)
			{
				m_Nodes.emplace_back(VolumeNode{ 0u, 0u });
			}

			bHasChildren = true;
		}
		else
		{
			childBase = (topoWord & CHILD_INDEX_MASK);
		}

		// Split parent bounds into 8 child bounds.
		const FVector3 parentCenter = center;
		const FVector3 parentExtents = extents;
		const FVector3 childExtents = parentExtents * 0.5f;

		// Descend into non-empty children to apply the difference.
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

			uint32_t childIndex = childBase + i;
			VolumeNode& childNode = m_Nodes[childIndex];

			uint32_t childTopo = childNode.TopologyBits;
			bool bChildHasChildren = (childTopo & HAS_CHILDREN_BIT) != 0;
			bool bChildLeafSolid = (childTopo & OCCUPIED_BIT) != 0;
			bool bChildSubtreeEmpty = !bChildHasChildren && !bChildLeafSolid;

			// Skip children that have no geometry to begin with.
			if (bChildSubtreeEmpty)
			{
				continue;
			}

			stack.emplace_back(NodeEntry{ childIndex, childBounds, currDepth + 1 });
		}
	}

	// Decapsulate the new bounds after difference.
}

void Volume::Compact()
{
	if (m_Nodes.empty())
	{
		// Nothing to compact
		return;
	}

	// Snapshot current nodes
	const std::vector<VolumeNode> oldNodes = m_Nodes;
	const uint32_t oldNodeCount = static_cast<uint32_t>(oldNodes.size());
	if (oldNodeCount == 0)
	{
		return;
	}

	// ------------------------------------------------------------------------
	// 1) Bottom-up: mark which old nodes have at least one occupied leaf
	//    in their subtree (including themselves).
	// ------------------------------------------------------------------------
	std::vector<uint8_t> subtreeNonEmpty(oldNodeCount, 0);
	std::vector<uint8_t> visited(oldNodeCount, 0);

	std::function<bool(uint32_t)> MarkSubtreeNonEmpty;
	MarkSubtreeNonEmpty = [&](uint32_t idx) -> bool
		{
			if (idx >= oldNodeCount)
			{
				return false;
			}

			if (visited[idx])
			{
				return subtreeNonEmpty[idx] != 0;
			}

			visited[idx] = 1;

			const VolumeNode& node = oldNodes[idx];
			const uint32_t topo = node.TopologyBits;
			const bool bHasChildren = (topo & HAS_CHILDREN_BIT) != 0;
			const bool bLeafSolid = (topo & OCCUPIED_BIT) != 0;

			bool bAny = bLeafSolid;

			if (bHasChildren)
			{
				const uint32_t childBase = (topo & CHILD_INDEX_MASK);
				for (uint32_t i = 0; i < 8; ++i)
				{
					const uint32_t childIdx = childBase + i;
					if (childIdx < oldNodeCount)
					{
						if (MarkSubtreeNonEmpty(childIdx))
						{
							bAny = true;
						}
					}
				}
			}

			subtreeNonEmpty[idx] = bAny ? 1u : 0u;
			return bAny;
		};

	const bool bRootNonEmpty = MarkSubtreeNonEmpty(0u);

	// ------------------------------------------------------------------------
	// 2) If the whole volume became empty, keep a single empty root and reset
	//    pseudo-bounds.
	// ------------------------------------------------------------------------
	if (!bRootNonEmpty)
	{
		m_Nodes.clear();
		m_Nodes.emplace_back(VolumeNode{ 0u, 0u });
		m_PseudoOccupiedBounds = Bounds{};
		return;
	}

	// ------------------------------------------------------------------------
	// 3) Rebuild new node array top-down, only for non-empty subtrees.
	//    We also recompute m_PseudoOccupiedBounds from occupied leaves.
	// ------------------------------------------------------------------------
	std::vector<VolumeNode> newNodes;
	newNodes.reserve(oldNodeCount); // upper bound

	Bounds newBounds;

	std::function<void(uint32_t, uint32_t, const Bounds&, int)>
		buildSubtree = [&](uint32_t oldIdx, uint32_t newIdx, const Bounds& nodeBounds, int depth)
		{
			const VolumeNode& oldNode = oldNodes[oldIdx];
			VolumeNode& newNode = newNodes[newIdx];

			const uint32_t topo = oldNode.TopologyBits;
			const bool bHasChildren = (topo & HAS_CHILDREN_BIT) != 0;
			const bool bLeafSolid = (topo & OCCUPIED_BIT) != 0;

			// Copy brick info (if used)
			newNode.BrickBits = oldNode.BrickBits;

			// Decide if we treat this as a leaf in the new tree
			const bool bForceLeaf = (!bHasChildren || depth >= MAX_OCTREE_DEPTH);

			if (bForceLeaf)
			{
				if (bLeafSolid)
				{
					newNode.TopologyBits = OCCUPIED_BIT;
					newBounds.Encapsulate(nodeBounds);
				}
				else
				{
					newNode.TopologyBits = 0u;
				}
				return;
			}

			// Internal node in the old tree. Check children occupancy.
			const uint32_t oldChildBase = (topo & CHILD_INDEX_MASK);

			bool  bAnyChildNonEmpty = false;
			uint8_t childNonEmpty[8] = {};

			for (uint32_t i = 0; i < 8; ++i)
			{
				const uint32_t childOldIdx = oldChildBase + i;
				if (childOldIdx < oldNodeCount && subtreeNonEmpty[childOldIdx])
				{
					bAnyChildNonEmpty = true;
					childNonEmpty[i] = 1;
				}
			}

			// Case: no child subtree has occupancy.
			//  - If this node itself is solid, make it a leaf.
			//  - Otherwise, it's effectively empty.
			if (!bAnyChildNonEmpty)
			{
				if (bLeafSolid)
				{
					newNode.TopologyBits = OCCUPIED_BIT;
					newBounds.Encapsulate(nodeBounds);
				}
				else
				{
					newNode.TopologyBits = 0u;
				}
				return;
			}

			// There is at least one non-empty child subtree.
			// Create 8 child roots contiguously.
			const uint32_t childBaseNew = static_cast<uint32_t>(newNodes.size());
			newNode.TopologyBits = HAS_CHILDREN_BIT | (childBaseNew & CHILD_INDEX_MASK);

			// Pre-allocate 8 child root nodes (placeholders).
			for (uint32_t i = 0; i < 8; ++i)
			{
				newNodes.emplace_back(VolumeNode{ 0u, 0u });
			}

			// Compute child bounds once
			const FVector3 parentCenter = nodeBounds.Center();
			const FVector3 parentExtents = nodeBounds.Extents();
			const FVector3 childExtents = parentExtents * 0.5f;

			for (uint32_t i = 0; i < 8; ++i)
			{
				const uint32_t childNewIdx = childBaseNew + i;

				// Child center by octant
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

				const uint32_t childOldIdx = oldChildBase + i;

				// If this child subtree is completely empty, leave the placeholder as
				// an empty node (TopologyBits = 0). We don't build deeper nodes.
				if (childOldIdx >= oldNodeCount || !subtreeNonEmpty[childOldIdx])
				{
					newNodes[childNewIdx].TopologyBits = 0u;
					newNodes[childNewIdx].BrickBits = 0u;
					continue;
				}

				// Otherwise, recursively rebuild this child subtree.
				buildSubtree(childOldIdx, childNewIdx, childBounds, depth + 1);
			}
		};

	// Create root in newNodes and rebuild from old root.
	newNodes.emplace_back(VolumeNode{ 0u, 0u }); // root at index 0
	buildSubtree(0u, 0u, GetOctreeBounds(), 0);

	m_Nodes.swap(newNodes);
	m_PseudoOccupiedBounds = newBounds;
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

	FVector3 halfExtents(0.5f * width, 0.5f * height, 0.5f * depth);
	FVector3 center(0.0f, 0.0f, 0.0f);

	volume.ApplyUnion([center, halfExtents](const FVector3& p)
		{
			return SDF::Box(p, center, halfExtents);
		});

	return volume;
}

Volume Volume::CreateSphere(float radius, float cellSize)
{
	Volume volume(cellSize);

	volume.ApplyUnion([radius](const FVector3& p)
		{
			return SDF::Sphere(p, radius);
		});

	return volume;
}

Volume Volume::CreateCylinder(float radius, float height, float cellSize)
{
	Volume volume(cellSize);

	volume.ApplyUnion([radius, height](const FVector3& p)
		{
			return SDF::Cylinder(p, radius, height);
		});

	return volume;
}

Volume Volume::CreatePlane(float width, float height, float cellSize)
{
	Volume volume(cellSize);

	volume.ApplyUnion([width, height, cellSize](const FVector3& p)
		{
			return SDF::ThinBoxPlane(p, width, height, cellSize);
		});

	return volume;
}

Volume Volume::CreateCone(float radius, float height, float cellSize)
{
	Volume volume(cellSize);

	volume.ApplyUnion([radius, height](const FVector3& p)
		{
			return SDF::Cone(p, radius, height);
		});

	return volume;
}
