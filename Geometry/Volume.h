#pragma once
#include "Generic/Color.h"
#include <array>
#include <unordered_set>
#include <unordered_map>

using MaterialId = uint8_t;

namespace Internals
{
	constexpr MaterialId MinMaterial = std::numeric_limits<MaterialId>::min();
	constexpr MaterialId MaxMaterial = std::numeric_limits<MaterialId>::max();
	constexpr uint32_t MaterialCount = static_cast<uint32_t>(MaxMaterial) + 1;
	constexpr uint64_t VolumeSideLength = UINT64_C(1) << 32;

	bool isMaterialNode(uint32_t nodeIndex);

	typedef std::array<uint32_t, 8> Node;

	class NodeStore
	{
	public:
		NodeStore() { mData = new Node[size()]; }
		~NodeStore() { delete[] mData; }

		// Non-copyable (deleted)
		NodeStore(const NodeStore&) = delete;
		NodeStore& operator=(const NodeStore&) = delete;

		const Node& operator[](uint32_t index) const { return mData[index]; }

		void setNode(uint32_t index, const Internals::Node& node);
		void setNodeChild(uint32_t nodeIndex, uint32_t childId, uint32_t newChildIndex);
		Node* data() const { return mData; }
		uint32_t size() const { return 0x3FFFFFF; }

	private:
		Node* mData = nullptr;
	};

	class NodeDAG
	{
	public:
		NodeDAG();

		// Non-copyable (deleted)
		NodeDAG(const NodeDAG&) = delete;
		NodeDAG& operator=(const NodeDAG&) = delete;

		const Node& operator[](uint32_t index) const { return mNodes[index]; }

		uint32_t bakedNodesBegin() const { return MaterialCount; }
		uint32_t bakedNodesEnd() const { return mBakedNodesEnd; }
		uint32_t editNodesBegin() const { return mEditNodesBegin; }
		uint32_t editNodesEnd() const { return mNodes.size(); }

		bool isBakedNode(uint32_t index) const { return index >= bakedNodesBegin() && index < bakedNodesEnd(); }
		bool isEditNode(uint32_t index) const { return index >= editNodesBegin() && index < editNodesEnd(); }

		NodeStore& nodes() { return mNodes; }
		const NodeStore& nodes() const { return mNodes; }

		uint32_t countNodes(uint32_t startNodeIndex) const;
		void countNodes(uint32_t startNodeIndex, std::unordered_set<uint32_t>& usedIndices) const;

		void read(std::ifstream& file);
		void write(std::ofstream& file);


		bool isPrunable(const Node& node) const;

		uint32_t insert(const Node& node);
		uint32_t updateNodeChild(uint32_t nodeIndex, uint32_t childId, uint32_t newChildNodeIndex, bool forceCopy);

		void merge(uint32_t index);
		uint32_t mergeNode(uint32_t nodeIndex, std::unordered_map<Internals::Node, uint32_t>& map, uint32_t& nextSpace);

	private:
		NodeStore mNodes;
		uint32_t mBakedNodesEnd = MaterialCount;
		uint32_t mEditNodesBegin = 0;
	};
}

class Brush
{
public:
	virtual bool contains(const FLOAT3& point) const = 0;
	virtual Bounds bounds() const = 0;

	FLOAT3 mCentre;
};

class SphereBrush : public Brush
{
public:
	SphereBrush(float x, float y, float z, float radius)
		:SphereBrush(FLOAT3(x, y, z), radius) {
	}
	SphereBrush(const FLOAT3& centre, float radius)
		:mRadiusSquared(radius* radius)
	{
		mCentre = centre;
		FLOAT3 radiusAsVec = FLOAT3(radius, radius, radius);
		mBounds = Bounds(centre - radiusAsVec, centre + radiusAsVec);
	}

	bool contains(const FLOAT3& point) const
	{
		// WARNING - Dubious precision here - these values can be huge!
		float distX = point.x - mCentre.x;
		float distY = point.y - mCentre.y;
		float distZ = point.z - mCentre.z;

		int64_t distSq = (distX * distX + distY * distY + distZ * distZ);

		return distSq < mRadiusSquared;
	}

	Bounds bounds() const
	{
		return mBounds;
	}

public:

	float mRadiusSquared;
	Bounds mBounds;
};

class Volume;
namespace Internals
{
	// The Volume class provides a high-level interface to the voxel data, but for some purposes (e.g. rendering) it's
	// faster to work on the raw data. This should be done with care as it requires understanding the internal format.
	// Such code can access the internals only through this 'Internals' namespace, which serves as a warning that they
	// probably shouldn't be doing it.

	/// Provides access to the raw node data.
	NodeDAG& getNodes(Volume& volume);
	const NodeDAG& getNodes(const Volume& volume);
	const uint32_t getRootNodeIndex(const Volume& volume);
}

class Volume
{
public:

	Volume();
	Volume(const std::string& filename);

	// Non-copyable (deleted)
	Volume(const Volume&) = delete;
	Volume& operator=(const Volume&) = delete;

	// FIXME - Default move constructor doesn't work, I think because there
	// is no move constructor on NodeStore (which manages a raw pointer) and
	// so it falls back on the copy constructor. 
	// But moveable (defaulted)
	//Volume(Volume&& vol) = default;
	//Volume& operator=(Volume&& vol) = default;

	void fill(MaterialId matId);

	uint32_t rootNodeIndex() const;
	void setRootNodeIndex(uint32_t newRootNodeIndex);

	void setTrackEdits(bool trackEdits);
	bool undo();
	bool redo();

	void setVoxelRecursive(int32_t x, int32_t y, int32_t z, MaterialId matId);
	uint32_t setVoxelRecursive(uint32_t ux, uint32_t uy, uint32_t uz, MaterialId matId, uint32_t nodeIndex, int nodeHeight);

	template <typename ArrayType>
	void setVoxel(const ArrayType& position, MaterialId matId);
	void setVoxel(int32_t x, int32_t y, int32_t z, MaterialId matId);

	void fillBrush(const Brush& brush, MaterialId matId);
	uint32_t fillBrush(const Brush& brush, MaterialId matId, uint32_t nodeIndex, int nodeHeight, int32_t nodeLowerX, int32_t nodeLowerY, int32_t nodeLowerZ);

	void addVolume(const Volume& rhsVolume);
	uint32_t addVolume(const Volume& rhsVolume, uint32_t rhsNodeIndex, uint32_t nodeIndex, int nodeHeight, int32_t nodeLowerX, int32_t nodeLowerY, int32_t nodeLowerZ);

	template <typename ArrayType>
	MaterialId voxel(const ArrayType& position) const;
	MaterialId voxel(int32_t x, int32_t y, int32_t z) const;

	const Color& getColor(MaterialId matId) const { return mMaterialColors[matId]; }

	void bake();

	uint32_t countNodes() const { return mDAG.countNodes(rootNodeIndex()); };

	bool load(const std::string& filename);
	void save(const std::string& filename);

	const IBounds& getOccupiedBounds() const { return mOccupiedBounds; }

private:
	void computeOccupiedBounds();

	friend Internals::NodeDAG& Internals::getNodes(Volume& volume);
	friend const Internals::NodeDAG& Internals::getNodes(const Volume& volume);
	//friend u32& Internals::getRootNodeIndex(Volume& volume);
	friend const uint32_t Internals::getRootNodeIndex(const Volume& volume);

private:
	Internals::NodeDAG mDAG;
	IBounds mOccupiedBounds;
	std::array<Color, Internals::MaterialCount> mMaterialColors;

	// Note: The undo system is linear. If we apply operation A, undo it, and then apply operation B
	// then A is lost. But it we wanted to we could still track it, because the appropriate edit still
	// exists (it is jst unreferenced). But having such an undo history tree will probably be confusing
	// for the user, moving forwards and backwards is probably enough.
	bool mTrackEdits = false;
	std::vector<uint32_t> mRootNodeIndices;
	uint32_t mCurrentRoot = 0;
};

// Implementation of templatised accessors
template <typename ArrayType> MaterialId Volume::voxel(const ArrayType& position) const
{
	return voxel(position[0], position[1], position[2]);
}

template <typename ArrayType> void Volume::setVoxel(const ArrayType& position, MaterialId matId)
{
	setVoxel(position[0], position[1], position[2], matId);
}

namespace Internals
{
	//-----------------------------------------------------------------------------
	// Block read - if your platform needs to do endian-swapping or can only
	// handle aligned reads, do the conversion here

	inline uint32_t getblock32(const uint32_t* p, int i)
	{
		return p[i];
	}

	inline uint64_t getblock64(const uint64_t* p, int i)
	{
		return p[i];
	}

	//-----------------------------------------------------------------------------
	// Finalization mix - force all bits of a hash block to avalanche

	inline uint32_t fmix32(uint32_t h)
	{
		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;

		return h;
	}

	//----------

	inline uint64_t fmix64(uint64_t k)
	{
		k ^= k >> 33;
		k *= 0xff51afd7ed558ccd;
		k ^= k >> 33;
		k *= 0xc4ceb9fe1a85ec53;
		k ^= k >> 33;

		return k;
	}

	inline void MurmurHash3_x86_32(const void* key, int len, uint32_t seed, void* out)
	{
		const uint8_t* data = (const uint8_t*)key;
		const int nblocks = len / 4;

		uint32_t h1 = seed;

		const uint32_t c1 = 0xcc9e2d51;
		const uint32_t c2 = 0x1b873593;

		//----------
		// body

		const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);

		for (int i = -nblocks; i; i++)
		{
			uint32_t k1 = getblock32(blocks, i);

			k1 *= c1;
			k1 = _rotl(k1, 15);
			k1 *= c2;

			h1 ^= k1;
			h1 = _rotl(h1, 13);
			h1 = h1 * 5 + 0xe6546b64;
		}

		//----------
		// tail

		const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);

		uint32_t k1 = 0;

		switch (len & 3)
		{
		case 3: k1 ^= tail[2] << 16;
		case 2: k1 ^= tail[1] << 8;
		case 1: k1 ^= tail[0];
			k1 *= c1; k1 = _rotl(k1, 15); k1 *= c2; h1 ^= k1;
		};

		//----------
		// finalization

		h1 ^= len;

		h1 = fmix32(h1);

		*(uint32_t*)out = h1;
	}

	inline uint32_t murmurHash3(const void* key, int len, uint32_t seed)
	{
		uint32_t result;
		MurmurHash3_x86_32(key, len, seed, &result);
		return result;
	}
}

namespace std
{
	template<>
	struct hash<Internals::Node>
	{
		uint32_t seed = 0;

		std::size_t operator()(const Internals::Node& node) const noexcept
		{
			return Internals::murmurHash3(&(node[0]), sizeof(uint32_t) * 8, seed);
		}
	};
}