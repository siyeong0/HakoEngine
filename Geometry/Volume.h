#pragma once
#include <functional>

struct VolumeNode
{
	uint32_t TopologyBits; // highest 2 bits are flags, lower 30 bits are child pointer
	uint32_t BrickBits;
};
static_assert(sizeof(VolumeNode) == 8, "VolumeNode size must be 8 bytes");

class Volume
{
public:
	Volume();
	Volume(float cellSize);
	~Volume();

	const std::vector<VolumeNode>& GetNodes() const { return m_Nodes; }
	float GetCellSize() const { return m_CellSize; }
	const Bounds& GetBounds() const { return m_Bounds; }
	Bounds GetOctreeBounds() const;

	void ApplyUnion(const std::function<float(const FVector3& pos)>& sdf);
	void ApplyDifference(const std::function<float(const FVector3& pos)>& sdf);

	static Volume CreateUnitCube(float cellSize = DEFAULT_CELL_SIZE);
	static Volume CreateBox(float width, float height, float depth, float cellSize = DEFAULT_CELL_SIZE);
	static Volume CreateSphere(float radius, float cellSize = DEFAULT_CELL_SIZE);
	static Volume CreatePlane(float width, float height, float cellSize = DEFAULT_CELL_SIZE);
	static Volume CreateCylinder(float radius, float height,float cellSize = DEFAULT_CELL_SIZE);
	static Volume CreateCone(float radius, float height, float cellSize = DEFAULT_CELL_SIZE);

public:
	static constexpr float DEFAULT_CELL_SIZE = 0.1f;
	static constexpr int MAX_OCTREE_DEPTH = 10; // maximum depth of the octree
	static constexpr int BRICK_SIZE = 8; // 8x8x8 voxels per brick

private:
	std::vector<VolumeNode> m_Nodes;

	float m_CellSize; // size of one voxel cell
	Bounds m_Bounds; // overall volume bounds

	static constexpr size_t DEFAULT_NODE_CAPACITY = 4096;
};