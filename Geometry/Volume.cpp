#include "pch.h"
#include "Volume.h"

/***************************************************************************************************
* Cubiquity - A micro-voxel engine for games and other interactive applications                    *
*                                                                                                  *
* Written in 2019 by David Williams                                                                *
*                                                                                                  *
* To the extent possible under law, the author(s) have dedicated all copyright and related and     *
* neighboring rights to this software to the public domain worldwide. This software is distributed *
* without any warranty.                                                                            *
*                                                                                                  *
* You should have received a copy of the CC0 Public Domain Dedication along with this software.    *
* If not, see http://creativecommons.org/publicdomain/zero/1.0/.                                   *
***************************************************************************************************/

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>

using namespace Internals;

bool Internals::isMaterialNode(uint32_t nodeIndex) { return nodeIndex < MaterialCount; }

// See https://stackoverflow.com/a/57796299
constexpr Node makeNode(uint32_t value)
{
	Node node{};
	for (auto& x : node) { x = value; }
	return node;
}

void NodeStore::setNode(uint32_t index, const Node& newNode)
{
	assert(!isMaterialNode(index) && "Error - Cannot modify material nodes");

	for (const uint32_t& childIndex : newNode)
	{
		assert(childIndex != index && "Error - Child points at parent");
	}

	mData[index] = newNode;
}

void NodeStore::setNodeChild(uint32_t nodeIndex, uint32_t childId, uint32_t newChildIndex)
{
	assert(!isMaterialNode(nodeIndex));
	assert(newChildIndex != nodeIndex && "Error - Node points at self");

	mData[nodeIndex][childId] = newChildIndex;
}

NodeDAG::NodeDAG()
{
	mEditNodesBegin = mNodes.size();
}

uint32_t NodeDAG::countNodes(uint32_t startNodeIndex) const
{
	std::unordered_set<uint32_t> usedIndices;
	countNodes(startNodeIndex, usedIndices);
	return usedIndices.size();
}

// Counts the number of nodes at distinct locations (node indices).
// A tree which is not fully merged will contain idetical nodes at different
// locations in memory, and these will be counted seperately.
void NodeDAG::countNodes(uint32_t startNodeIndex, std::unordered_set<uint32_t>& usedIndices) const
{
	// It may have been more efficient to have done this test before calling
	// into this function, but the implementation is simpler this way around.
	if (isMaterialNode(startNodeIndex)) { return; }

	usedIndices.insert(startNodeIndex);
	for (const uint32_t& childNodeIndex : mNodes[startNodeIndex])
	{
		countNodes(childNodeIndex, usedIndices);
	}
}

void NodeDAG::read(std::ifstream& file)
{
	uint32_t nodeCount;
	file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
	for (uint32_t ct = 0; ct < nodeCount; ct++)
	{
		Node node;
		file.read(reinterpret_cast<char*>(&node), sizeof(node));
		mNodes.setNode(bakedNodesBegin() + ct, node);
	}
	mBakedNodesEnd = bakedNodesBegin() + nodeCount;
}

void NodeDAG::write(std::ofstream& file)
{
	uint32_t nodeCount = bakedNodesEnd() - bakedNodesBegin();
	file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
	for (uint32_t ct = 0; ct < nodeCount; ct++)
	{
		file.write(reinterpret_cast<const char*>(&mNodes[bakedNodesBegin() + ct]), sizeof(Node));
	}

}

bool NodeDAG::isPrunable(const Node& node) const
{
	if (!isMaterialNode(node[0])) { return false; }
	for (uint32_t i = 1; i < 8; i++)
	{
		if (node[i] != node[0]) { return false; }
	}

	// All children represent the same solid material, so this node can be pruned.
	return true;
}

void NodeDAG::merge(uint32_t index)
{
	std::unordered_map<Node, uint32_t> map;
	uint32_t mergedEnd = mEditNodesBegin;
	uint32_t nextSpace = mergedEnd - 1;

	if (isMaterialNode(index))
	{
		mBakedNodesEnd = bakedNodesBegin();
	}
	else
	{
		uint32_t mergedRoot = mergeNode(index, map, nextSpace);

		uint32_t actualNodeCount = mergedEnd - mergedRoot;

		mBakedNodesEnd = bakedNodesBegin() + actualNodeCount;

		// FIXME - This offset value seems to get large. Is the logic backwards
		// but we ar wrapping around the array so it happens to work?
		uint32_t offset = bakedNodesBegin() - mergedRoot;
		for (uint32_t nodeIndex = bakedNodesBegin(); nodeIndex < bakedNodesEnd(); nodeIndex++)
		{
			Node node = mNodes[nodeIndex - offset];
			for (uint32_t& childIndex : node)
			{
				if (childIndex > MaxMaterial)
				{
					childIndex += offset;
				}
			}
			mNodes.setNode(nodeIndex, node);
		}
	}
}

uint32_t NodeDAG::mergeNode(uint32_t nodeIndex, std::unordered_map<Node, uint32_t>& map, uint32_t& nextSpace)
{
	assert(!isMaterialNode(nodeIndex));
	const Node& oldNode = mNodes[nodeIndex];
	Node newNode;
	for (int i = 0; i < 8; i++)
	{
		uint32_t oldChildIndex = oldNode[i];
		if (!isMaterialNode(oldChildIndex))
		{
			newNode[i] = mergeNode(oldChildIndex, map, nextSpace);
		}
		else
		{
			newNode[i] = oldNode[i];
		}
	}

	uint32_t newNodeIndex = 0;
	auto iter = map.find(newNode);
	if (iter == map.end())
	{
		//newNodeIndex = insertNode(newNode)
		//newNodeIndex = mDAG.mInitialNodes.insert(newNode);

		mNodes.setNode(nextSpace, newNode);
		newNodeIndex = nextSpace;
		nextSpace--;

		map.insert({ newNode, newNodeIndex });
	}
	else
	{
		newNodeIndex = iter->second;
	}
	return newNodeIndex;
	//return nodes.insert(newNode);
}

uint32_t NodeDAG::insert(const Node& node)
{
	if (mEditNodesBegin > bakedNodesEnd())
	{
		mEditNodesBegin--;
		uint32_t index = mEditNodesBegin;
		mNodes.setNode(index, node);
		return index;
	}

	assert(false && "Out of space for unshared edits!");
	std::cout << ("Out of space for unshared edits!") << std::endl;
	exit(1);
	return 0; // Indicates error (we don't use this function to allocate the zeroth node).
}

// Update the child of a node. If a copy needs to be made then this is returned,
// otherwise the return value is empty to indicate that the update was done in-place.
uint32_t NodeDAG::updateNodeChild(uint32_t nodeIndex, uint32_t childId, uint32_t newChildNodeIndex, bool forceCopy)
{
	assert(newChildNodeIndex != mNodes[nodeIndex][childId]); // Watch for self-assignment (wasteful)
	assert(newChildNodeIndex != nodeIndex); // Don't let child point to parent.

	// Edit nodes can be modified in-place as they are unshared, unless the users
	// requests they are copied (e.g. for the purpose of maintaining an undo history).
	const bool modifyInPlace = isEditNode(nodeIndex) && (!forceCopy);

	if (modifyInPlace)
	{
		// Modify the existing node in-place by updating only the relevant child.
		mNodes.setNodeChild(nodeIndex, childId, newChildNodeIndex);

		// The modification may have made the node prunable. If so it must have taken on the value
		// of the new child so return that, otherwise return the updated node that we were given.
		return isPrunable(mNodes[nodeIndex]) ? newChildNodeIndex : nodeIndex;
	}
	else
	{
		const bool nodeIsMaterial = isMaterialNode(nodeIndex);

		// Make a copy of the existing node and then update the child.
		Node newNode = nodeIsMaterial ? makeNode(nodeIndex) : mNodes[nodeIndex];
		newNode[childId] = newChildNodeIndex;

		// If the copy becomes prunable as a result of the modification
		// then we can skip inserting it, which saves time and space.
		return isPrunable(newNode) ? newChildNodeIndex : insert(newNode);
	}

}

////////////////////////////////////////////////////////////////////////////////
// Public member functions
////////////////////////////////////////////////////////////////////////////////

Volume::Volume()
{
	mRootNodeIndices.resize(1);
	mCurrentRoot = 0;
}

Volume::Volume(const std::string& filename)
{
	mRootNodeIndices.resize(1);
	mCurrentRoot = 0;

	load(filename);
}

void Volume::fill(MaterialId matId)
{
	setRootNodeIndex(matId);
}

uint32_t Volume::rootNodeIndex() const
{
	return mRootNodeIndices[mCurrentRoot];
}

void Volume::setRootNodeIndex(uint32_t newRootNodeIndex)
{
	if (mTrackEdits)
	{
		// When tracking edits a root node should not be set to itself. However, this
		// might happen when *not* tracking edits because we are then modifying in-place.
		assert(newRootNodeIndex != mRootNodeIndices[mCurrentRoot]);

		mCurrentRoot++;
		if (mCurrentRoot >= mRootNodeIndices.size())
		{
			mRootNodeIndices.resize(mCurrentRoot + 1);
		}
	}

	mRootNodeIndices[mCurrentRoot] = newRootNodeIndex;
}

void Volume::setTrackEdits(bool trackEdits)
{
	mTrackEdits = trackEdits;
	if (!mTrackEdits)
	{
		mRootNodeIndices[0] = mRootNodeIndices[mCurrentRoot];
		mRootNodeIndices.resize(1);
		mCurrentRoot = 0;
	}
}

bool Volume::undo()
{
	if (mCurrentRoot > 0)
	{
		mCurrentRoot--;
		return true;
	}

	return false; // Nothing to undo
}

bool Volume::redo()
{
	if (mCurrentRoot < mRootNodeIndices.size() - 1)
	{
		mCurrentRoot++;
		return true;
	}

	return false; // Nothing to redo
}

void Volume::bake()
{
	mDAG.merge(rootNodeIndex());
	setRootNodeIndex(mDAG.bakedNodesBegin());
}

void Volume::setVoxelRecursive(int32_t x, int32_t y, int32_t z, MaterialId matId)
{
	// Do we need this mapping to unsiged space? Or could we eliminate it in both voxel()/
	//setVoxel() and get the same behaviour? Or is that confusing, e.g. with raycasting?
	uint32_t ux = static_cast<uint32_t>(x) ^ (1UL << 31);
	uint32_t uy = static_cast<uint32_t>(y) ^ (1UL << 31);
	uint32_t uz = static_cast<uint32_t>(z) ^ (1UL << 31);

	const int rootHeight = std::log2(VolumeSideLength);
	uint32_t newRootNodeIndex = setVoxelRecursive(ux, uy, uz, matId, rootNodeIndex(), rootHeight);
	setRootNodeIndex(newRootNodeIndex);
}

uint32_t Volume::setVoxelRecursive(uint32_t ux, uint32_t uy, uint32_t uz, MaterialId matId, uint32_t nodeIndex, int nodeHeight)
{
	assert(nodeHeight > 0);
	uint32_t childHeight = nodeHeight - 1;

	// We could possibly remove these variable bitshifts, but I'm not sure it's worth the effort.
	uint32_t childX = (ux >> childHeight) & 0x01;
	uint32_t childY = (uy >> childHeight) & 0x01;
	uint32_t childZ = (uz >> childHeight) & 0x01;
	uint32_t childId = childZ << 2 | childY << 1 | childX;

	const bool nodeIsMaterial = isMaterialNode(nodeIndex);

	// If current node is a material then just propergate it. Otherwise get the true child.
	uint32_t childNodeIndex = nodeIsMaterial ? nodeIndex : mDAG[nodeIndex][childId];

	// If the child node is set to the desired material then the voxel is 
	// already set. Return invalid node indicating nothing to update.
	if (childNodeIndex == matId) { return nodeIndex; }

	// Recusively process the child node until we reach a just-above-leaf node (height of 1).
	if (nodeHeight > 1)
	{
		uint32_t newChildNodeIndex = setVoxelRecursive(ux, uy, uz, matId, childNodeIndex, nodeHeight - 1);

		// If the child hasn't changed then we don't need to update the current node.
		if (newChildNodeIndex == childNodeIndex) { return nodeIndex; }

		return mDAG.updateNodeChild(nodeIndex, childId, newChildNodeIndex, mTrackEdits);
	}
	else
	{
		return mDAG.updateNodeChild(nodeIndex, childId, matId, mTrackEdits);
	}
}

void Volume::setVoxel(int32_t x, int32_t y, int32_t z, MaterialId matId)
{
	//return setVoxelRecursive(x, y, z, matId);

	struct NodeState
	{
		uint32_t mIndex;
		bool mProcessedNode;
	};

	// Note that the first two elements of this stack never actually get used.
	// Leaf and almost-leaf nodes(heights 0 and 1) never get put on the stack.
	// We accept this wasted space, rather than subtracting two on every access.
	const int maxStackDepth = 33;
	NodeState nodeStateStack[maxStackDepth];

	const int rootHeight = std::log2(VolumeSideLength);
	int nodeHeight = rootHeight;
	nodeStateStack[nodeHeight].mIndex = rootNodeIndex();
	nodeStateStack[nodeHeight].mProcessedNode = false;

	while (true)
	{
		// This loop does not go right down to leaf nodes, it stops one level above. But for any
		// given node it manipulates it's children, which means leaf nodes can get modified.
		assert(nodeHeight >= 1);

		NodeState& nodeState = nodeStateStack[nodeHeight];
		//const Node* node = &(mDAG[nodeState.mIndex]);

		// Find which subtree we are in.
		uint32_t childHeight = nodeHeight - 1;
		int tx = (x ^ (1UL << 31)); // Could precalculte these.
		int ty = (y ^ (1UL << 31));
		int tz = (z ^ (1UL << 31));
		uint32_t childX = (tx >> childHeight) & 0x01;
		uint32_t childY = (ty >> childHeight) & 0x01;
		uint32_t childZ = (tz >> childHeight) & 0x01;
		uint32_t childId = childZ << 2 | childY << 1 | childX;

		if (nodeState.mProcessedNode == false) // Executed the first time we see a node - i.e. as we move *down* the tree.
		{
			const bool nodeIsMaterial = isMaterialNode(nodeState.mIndex);

			// If current node is a material then just propergate it. Otherwise get the true child.
			uint32_t childNodeIndex = nodeIsMaterial ? nodeState.mIndex : mDAG[nodeState.mIndex][childId];

			// If the child node is set to the desired material then the voxel is already set. 
			if (childNodeIndex == matId) { return; }

			if (nodeHeight >= 2)
			{
				NodeState& childNodeState = nodeStateStack[nodeHeight - 1];
				childNodeState.mIndex = childNodeIndex;
				childNodeState.mProcessedNode = false;

				nodeHeight -= 1;
			}

			nodeState.mProcessedNode = true;
		}
		else // Executed the second time we see a node - i.e. as we move *up* the tree.
		{
			if (nodeHeight > 1)
			{
				// If the child has changed then we need to update the current node.
				const NodeState& childNodeState = nodeStateStack[nodeHeight - 1];
				if (mDAG[nodeState.mIndex][childId] != childNodeState.mIndex)
				{
					nodeState.mIndex = mDAG.updateNodeChild(nodeState.mIndex, childId, childNodeState.mIndex, mTrackEdits);
				}
			}
			else
			{
				nodeState.mIndex = mDAG.updateNodeChild(nodeState.mIndex, childId, matId, mTrackEdits);
			}

			// Move up the tree to process parent node next, until we reach the root.
			nodeHeight += 1;
			if (nodeHeight > rootHeight)
			{
				break;
			}
		}
	}

	setRootNodeIndex(nodeStateStack[rootHeight].mIndex);
}

void Volume::fillBrush(const Brush& brush, MaterialId matId)
{
	const int rootHeight = std::log2(VolumeSideLength);
	int nodeHeight = rootHeight;
	uint32_t newIndex = matId;

	constexpr int32_t rootLowerBound = std::numeric_limits<int32_t>::min();

	uint32_t newRootNodeIndex = fillBrush(brush, matId, rootNodeIndex(), nodeHeight, rootLowerBound, rootLowerBound, rootLowerBound);
	setRootNodeIndex(newRootNodeIndex);
}

uint32_t Volume::fillBrush(const Brush& brush, MaterialId matId, uint32_t nodeIndex, int nodeHeight, int32_t nodeLowerX, int32_t nodeLowerY, int32_t nodeLowerZ)
{
	uint32_t childHeight = nodeHeight - 1;
	//int tx = (x ^ (1UL << 31)); // Could precalculte these.
	//int ty = (y ^ (1UL << 31));
	//int tz = (z ^ (1UL << 31));

	for (uint32_t childZ = 0; childZ <= 1; childZ++)
	{
		for (uint32_t childY = 0; childY <= 1; childY++)
		{
			for (uint32_t childX = 0; childX <= 1; childX++)
			{
				//u32 childX = (tx >> childHeight) & 0x01;
				//u32 childY = (ty >> childHeight) & 0x01;
				//u32 childZ = (tz >> childHeight) & 0x01;
				uint32_t childId = childZ << 2 | childY << 1 | childX;

				uint32_t childSideLength = 1 << (childHeight);
				int32_t childLowerX = nodeLowerX + (childSideLength * childX);
				int32_t childLowerY = nodeLowerY + (childSideLength * childY);
				int32_t childLowerZ = nodeLowerZ + (childSideLength * childZ);

				int32_t childUpperX = childLowerX + (childSideLength - 1);
				int32_t childUpperY = childLowerY + (childSideLength - 1);
				int32_t childUpperZ = childLowerZ + (childSideLength - 1);

				Bounds childBounds(FVector3({ static_cast<float>(childLowerX), static_cast<float>(childLowerY), static_cast<float>(childLowerZ) }), FVector3({ static_cast<float>(childUpperX),static_cast<float>(childUpperY), static_cast<float>(childUpperZ) }));

				if (!Bounds::Overlaps(brush.bounds(), childBounds))
				{
					continue;
				}

				bool allCornersInsideBrush = true;
				if (!brush.contains(FLOAT3({ static_cast<float>(childLowerX), static_cast<float>(childLowerY), static_cast<float>(childLowerZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childLowerX), static_cast<float>(childLowerY), static_cast<float>(childUpperZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childLowerX), static_cast<float>(childUpperY), static_cast<float>(childLowerZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childLowerX), static_cast<float>(childUpperY), static_cast<float>(childUpperZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childUpperX), static_cast<float>(childLowerY), static_cast<float>(childLowerZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childUpperX), static_cast<float>(childLowerY), static_cast<float>(childUpperZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childUpperX), static_cast<float>(childUpperY), static_cast<float>(childLowerZ) }))) { allCornersInsideBrush = false; }
				if (!brush.contains(FLOAT3({ static_cast<float>(childUpperX), static_cast<float>(childUpperY), static_cast<float>(childUpperZ) }))) { allCornersInsideBrush = false; }

				const bool nodeIsMaterial = isMaterialNode(nodeIndex);

				// If current node is a material then just propergate it. Otherwise get the true child.
				uint32_t childNodeIndex = nodeIsMaterial ? nodeIndex : mDAG[nodeIndex][childId];

				// If the child node is set to the desired material then the voxel is already set.
				if (childNodeIndex == matId) { continue; }

				// Process children
				uint32_t newChildNodeIndex = childNodeIndex;
				//if (nodeHeight >= 2)
				if (nodeHeight >= 2 && !allCornersInsideBrush)
				{
					newChildNodeIndex = fillBrush(brush, matId, childNodeIndex, nodeHeight - 1, childLowerX, childLowerY, childLowerZ);
				}
				else
				{
					if (allCornersInsideBrush)
					{
						newChildNodeIndex = matId;
					}
				}

				// If the child has changed then we need to update the current node.
				if (childNodeIndex != newChildNodeIndex)
				{
					nodeIndex = mDAG.updateNodeChild(nodeIndex, childId, newChildNodeIndex, mTrackEdits);
				}
			}
		}
	}

	return nodeIndex;
}

void Volume::addVolume(const Volume& rhsVolume)
{
	const int rootHeight = std::log2(VolumeSideLength);
	int nodeHeight = rootHeight;

	constexpr int32_t rootLowerBound = std::numeric_limits<int32_t>::min();

	uint32_t newRootNodeIndex = addVolume(rhsVolume, rhsVolume.rootNodeIndex(), rootNodeIndex(), nodeHeight, rootLowerBound, rootLowerBound, rootLowerBound);
	setRootNodeIndex(newRootNodeIndex);
}

// Fixme - This function can prbably be more efficient. Firstly through better early out when whole nodes are full/empty, and also
// if seperate volumes shared a common memory space it would be easier to copy node directly across (maybe in compressd space?).
uint32_t Volume::addVolume(const Volume& rhsVolume, uint32_t rhsNodeIndex, uint32_t nodeIndex, int nodeHeight, int32_t nodeLowerX, int32_t nodeLowerY, int32_t nodeLowerZ)
{
	uint32_t childHeight = nodeHeight - 1;
	//int tx = (x ^ (1UL << 31)); // Could precalculte these.
	//int ty = (y ^ (1UL << 31));
	//int tz = (z ^ (1UL << 31));

	for (uint32_t childZ = 0; childZ <= 1; childZ++)
	{
		for (uint32_t childY = 0; childY <= 1; childY++)
		{
			for (uint32_t childX = 0; childX <= 1; childX++)
			{
				//u32 childX = (tx >> childHeight) & 0x01;
				//u32 childY = (ty >> childHeight) & 0x01;
				//u32 childZ = (tz >> childHeight) & 0x01;
				uint32_t childId = childZ << 2 | childY << 1 | childX;

				uint32_t childSideLength = 1 << (childHeight);
				int32_t childLowerX = nodeLowerX + (childSideLength * childX);
				int32_t childLowerY = nodeLowerY + (childSideLength * childY);
				int32_t childLowerZ = nodeLowerZ + (childSideLength * childZ);

				int32_t childUpperX = childLowerX + (childSideLength - 1);
				int32_t childUpperY = childLowerY + (childSideLength - 1);
				int32_t childUpperZ = childLowerZ + (childSideLength - 1);

				Bounds childBounds(FVector3({ static_cast<float>(childLowerX), static_cast<float>(childLowerY), static_cast<float>(childLowerZ) }), FVector3({ static_cast<float>(childUpperX), static_cast<float>(childUpperY), static_cast<float>(childUpperZ) }));

				/*if (!overlaps(brush.bounds(), childBounds))
				{
					continue;
				}*/

				/*bool allCornersInsideBrush = true;
				if (!brush.contains(vec3f(childLowerX, childLowerY, childLowerZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childLowerX, childLowerY, childUpperZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childLowerX, childUpperY, childLowerZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childLowerX, childUpperY, childUpperZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childUpperX, childLowerY, childLowerZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childUpperX, childLowerY, childUpperZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childUpperX, childUpperY, childLowerZ))) { allCornersInsideBrush = false; }
				if (!brush.contains(vec3f(childUpperX, childUpperY, childUpperZ))) { allCornersInsideBrush = false; }*/

				const bool nodeIsMaterial = isMaterialNode(nodeIndex);
				const bool rhsNodeIsMaterial = isMaterialNode(rhsNodeIndex);

				// If current node is a material then just propergate it. Otherwise get the true child.
				uint32_t childNodeIndex = nodeIsMaterial ? nodeIndex : mDAG[nodeIndex][childId];
				uint32_t rhsChildNodeIndex = rhsNodeIsMaterial ? rhsNodeIndex : rhsVolume.mDAG[rhsNodeIndex][childId];

				// If the child node is set to the desired material then the voxel is already set.
				if (isMaterialNode(childNodeIndex) && isMaterialNode(rhsChildNodeIndex) && (childNodeIndex == rhsChildNodeIndex)) { continue; }

				// Process children
				uint32_t newChildNodeIndex = childNodeIndex;

				// We have to treat at least one material as empty space, otherwise we are just copying 
				// every voxel from source to destination, which just result in a copy of the source. 
				// Hard-code it to zero for now, but should think about how else it might be controlled.
				MaterialId emptyMaterial = 0;
				if (rhsChildNodeIndex != emptyMaterial)
				{
					if (isMaterialNode(rhsChildNodeIndex))
					{
						newChildNodeIndex = rhsChildNodeIndex;
					}
					else
					{
						newChildNodeIndex = addVolume(rhsVolume, rhsChildNodeIndex, childNodeIndex, nodeHeight - 1, childLowerX, childLowerY, childLowerZ);;
					}
				}

				// If the child has changed then we need to update the current node.
				if (childNodeIndex != newChildNodeIndex)
				{
					nodeIndex = mDAG.updateNodeChild(nodeIndex, childId, newChildNodeIndex, mTrackEdits);
				}
			}
		}
	}

	return nodeIndex;
}

MaterialId Volume::voxel(int32_t x, int32_t y, int32_t z) const
{
	uint32_t nodeIndex = rootNodeIndex();
	uint32_t height = std::log2(VolumeSideLength);

	// FIXME - think whether we need the line below - I think we do for empty/solid volumes?
	//if (mDAG.isMaterialNode(mRootNodeIndex)) { return static_cast<MaterialId>(mRootNodeIndex); }

	while (height >= 1)
	{
		// If we reach a full node then the requested voxel is occupied.
		if (isMaterialNode(nodeIndex)) { return static_cast<MaterialId>(nodeIndex); }

		// Otherwise find which subtree we are in.
		// Optimization - Note that the code below requires shifting by a variable amount which can be slow.
		// Alternatively I think we can simply shift x, y, and z by one bit per iteration, but this requires us
		// to reverse the order of the bits at the start of this function. It would be a one-time cost for a
		// faster loop, and testing is needed (on a real, large volume) to determine whether it is beneficial.
		uint32_t childHeight = height - 1;
		int tx = (x ^ (1UL << 31)); // Could precalculte these.
		int ty = (y ^ (1UL << 31));
		int tz = (z ^ (1UL << 31));
		uint32_t childX = (tx >> childHeight) & 0x01;
		uint32_t childY = (ty >> childHeight) & 0x01;
		uint32_t childZ = (tz >> childHeight) & 0x01;
		uint32_t childId = childZ << 2 | childY << 1 | childX;

		// Prepare for next iteration.
		nodeIndex = mDAG[nodeIndex][childId];
		height--;
	}

	// We have reached a height of zero so the node must be a material node.
	assert(height == 0 && isMaterialNode(nodeIndex));
	return static_cast<MaterialId>(nodeIndex);
}

////////////////////////////////////////////////////////////////////////////////
// Private member functions
////////////////////////////////////////////////////////////////////////////////

bool Volume::load(const std::string& filename)
{
	std::ifstream file(filename, std::ios::binary);

	// FIXME - What should we do for error handling in this function? Return codes or exceptions?
	if (!file.is_open())
	{
		return false;
	}

	uint32_t rootNodeIndex;
	file.read(reinterpret_cast<char*>(&rootNodeIndex), sizeof(rootNodeIndex));
	//assert(rootNodeIndex == mDAG.arrayBegin());
	//setRootNodeIndex(RefCountedNodeIndex(rootNodeIndex, &mDAG));

	//mDAG.read(file);

	// This fill is not required but can be useful for debugging
	//const Node InvalidNode = makeNode(0xffffffff);
	//std::fill(mDAG.mNodes.begin(), mDAG.mNodes.end(), InvalidNode);

	mDAG.read(file);

	if (rootNodeIndex >= MaterialCount)
	{
		rootNodeIndex += mDAG.bakedNodesBegin() - MaterialCount;
	}

	setRootNodeIndex(rootNodeIndex);
	computeOccupiedBounds();

	return true;
}

void Volume::save(const std::string& filename)
{
	bake();

	uint32_t root = rootNodeIndex();
	const Node& data = mDAG[mDAG.bakedNodesBegin()];

	std::ofstream file;
	file = std::ofstream(filename, std::ios::out | std::ios::binary);
	file.write(reinterpret_cast<const char*>(&root), sizeof(root));
	mDAG.write(file);
	file.close();
}

void Volume::computeOccupiedBounds()
{
	struct Box { int32_t minx, miny, minz, maxx, maxy, maxz; };

	// DAG 접근
	const auto& dag = Internals::getNodes(*this);
	const uint32_t root = Internals::getRootNodeIndex(*this);

	// 루트 박스: [-2^31, 2^31-1] (포함 경계)
	constexpr int kRootHeight = 32;
	const Box rootBox{
		std::numeric_limits<int32_t>::min(),
		std::numeric_limits<int32_t>::min(),
		std::numeric_limits<int32_t>::min(),
		std::numeric_limits<int32_t>::max(),
		std::numeric_limits<int32_t>::max(),
		std::numeric_limits<int32_t>::max()
	};

	auto childBounds = [](const Box& p, int cid, int childHeight) -> Box
		{
			const int64_t side = (int64_t)1 << childHeight; // 자식 한 변 길이(포함 경계라 max = min + side - 1)

			const int cx = cid & 1;
			const int cy = (cid >> 1) & 1;
			const int cz = (cid >> 2) & 1;

			const int64_t minx = (int64_t)p.minx + side * cx;
			const int64_t miny = (int64_t)p.miny + side * cy;
			const int64_t minz = (int64_t)p.minz + side * cz;

			Box b{};
			b.minx = (int32_t)std::clamp<int64_t>(minx, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
			b.miny = (int32_t)std::clamp<int64_t>(miny, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
			b.minz = (int32_t)std::clamp<int64_t>(minz, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());

			const int64_t maxx = minx + (side - 1);
			const int64_t maxy = miny + (side - 1);
			const int64_t maxz = minz + (side - 1);
			b.maxx = (int32_t)std::clamp<int64_t>(maxx, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
			b.maxy = (int32_t)std::clamp<int64_t>(maxy, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
			b.maxz = (int32_t)std::clamp<int64_t>(maxz, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
			return b;
		};

	// 누적 AABB (IBounds로 관리)
	IBounds acc; // 초기값: Min=+INF, Max=-INF (비어있는 상태)
	bool any = false;

	struct Item { uint32_t n; int h; Box b; };
	std::vector<Item> st;
	st.push_back({ root, kRootHeight, rootBox });

	while (!st.empty())
	{
		auto [n, h, box] = st.back(); st.pop_back();

		if (n < Internals::MaterialCount)
		{
			const uint8_t matId = static_cast<uint8_t>(n);
			if (matId != 0) // 빈 머티리얼(0) 제외
			{
				any = true;
				acc.Encapsulate(IVector3{ box.minx, box.miny, box.minz });
				acc.Encapsulate(IVector3{ box.maxx, box.maxy, box.maxz });
			}
			continue;
		}

		// 내부노드 → 8자식 방문
		const Internals::Node& kids = dag[n];
		const int childH = h - 1;
		for (int cid = 0; cid < 8; ++cid)
		{
			const uint32_t childIndex = kids[cid];
			if (childIndex == 0) continue; // 빈 자식은 스킵해도 무방(최적화)

			Box cb = childBounds(box, cid, childH);
			st.push_back({ childIndex, childH, cb });
		}
	}

	if (!any)
	{
		// 비어있음: IBounds의 기본(empty) 상태 그대로 반환
		mOccupiedBounds = IBounds();
	}
	
	mOccupiedBounds = acc;
}

namespace Internals
{
	/// This is an advanced function which should only be used if you
	/// understand the internal memory layout of Cubiquity's volume data.
	NodeDAG& getNodes(Volume& volume)
	{
		return volume.mDAG;
	}

	const NodeDAG& getNodes(const Volume& volume)
	{
		return volume.mDAG;
	}

	const uint32_t getRootNodeIndex(const Volume& volume)
	{
		return volume.rootNodeIndex();
	}
}
