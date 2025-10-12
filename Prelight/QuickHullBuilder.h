#pragma once

struct QHVec3;
struct QHFace;
struct QuickHull;

// ------------------------------------------------
// QuickHullBuilder
// ------------------------------------------------
// Incremental 3D QuickHull.
// - Epsilon: auto from bbox diag if < 0 (robustness).
// - Seed: non-degenerate tetra.
// - Grow: pick farthest point, find visible region, stitch cone, reassign.
// - Output: convex hull (triangles CCW outward).
class QuickHullBuilder
{
public:
	QuickHullBuilder();
	~QuickHullBuilder();

	QuickHull Run(const std::vector<FLOAT3>& points);

private:
	void initEpsilon(); // Initi epsilon based on bounding box diagonal
	bool buildInitialTetra(const std::vector<int>& idx, int& o0, int& o1, int& o2, int& o3);
	void setAdj(int fa, int ea, int fb, int eb);

	void distributePoints(const std::vector<int>& idx);

	int pickFaceWithFarthestPoint();
	int popFarthestPoint(int faceIndex);
	void findVisibleRegion(
		int startFace,
		int apex,
		std::vector<int>& visible,
		std::vector<std::pair<int, int>>& horizonFE);
	std::vector<std::pair<int, int>> orderHorizonRing(const std::vector<std::pair<int, int>>& edges);

	int addFace(int a, int b, int c);
	int addFaceOutward(int a, int b, int c, const QHVec3& interior);

	int findMatchingEdge(int oldNbr, int a, int b);

	void createConeOrdered(
		int apex,
		const std::vector<std::pair<int, int>>& horizonFE,
		const std::vector<std::pair<int, int>>& ringAB,
		std::vector<int>& newFaces,
		const QHVec3& interior);

	void reassignOutside(const std::vector<int>& pts, const std::vector<int>& newFaces);

private:
	// Geometric tolerance for sidedness/orientation (<=0 => auto).
	double m_HullEpsilon = -1.0;

	// Working vertices and faces(internal format).
	std::vector<QHVec3>* m_pVertices;
	std::vector<QHFace>* m_pFaces;
};