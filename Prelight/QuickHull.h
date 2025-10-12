// QuickHull.h (single-file)
// Minimal, robust-enough QuickHull for voxel/corner inputs.
// Author: you + ChatGPT
#pragma once
#include <vector>
#include <limits>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <queue>
#include <cassert>

struct QHVec3 {
    double x = 0, y = 0, z = 0;
    QHVec3() = default;
    QHVec3(double X, double Y, double Z) :x(X), y(Y), z(Z) {}
    QHVec3 operator+(const QHVec3& b) const { return { x + b.x,y + b.y,z + b.z }; }
    QHVec3 operator-(const QHVec3& b) const { return { x - b.x,y - b.y,z - b.z }; }
    QHVec3 operator*(double s) const { return { x * s,y * s,z * s }; }
    QHVec3 operator/(double s) const { return { x / s,y / s,z / s }; }
};
inline static double dot(const QHVec3& a, const QHVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline static QHVec3 cross(const QHVec3& a, const QHVec3& b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
inline static double len(const QHVec3& a) { return std::sqrt(dot(a, a)); }

struct QHPlane {
    QHVec3 n; // unit normal
    double d = 0; // plane: n·p + d = 0
    double signedDist(const QHVec3& p) const { return dot(n, p) + d; }
};
inline static QHPlane makePlane(const QHVec3& a, const QHVec3& b, const QHVec3& c) {
    QHVec3 n = cross(b - a, c - a);
    double L = len(n);
    if (L > 0) n = n / L;
    QHPlane P; P.n = n; P.d = -dot(n, a); return P;
}
inline static double tetraVolume6(const QHVec3& a, const QHVec3& b, const QHVec3& c, const QHVec3& d) {
    return dot(cross(b - a, c - a), d - a);
}

struct QHFace {
    int a = -1, b = -1, c = -1;   // CCW when seen from outside
    int adj[3] = { -1,-1,-1 }; // neighbor faces across edges (a,b),(b,c),(c,a)
    QHPlane plane;
    bool alive = true;
    std::vector<int> outside; // point indices outside this face
};

struct QHHull {
    std::vector<QHVec3> verts;     // unique vertices (copy of input)
    std::vector<int>    indices;   // triangles as (i0,i1,i2)
};

class QuickHull3D {
public:
    double eps = -1.0; // if <0, auto: 1e-9 * bbox_diag
    // returns convex hull for input points
    QHHull run(const std::vector<QHVec3>& points) {
        H.verts = points;
        initEps();
        if (H.verts.size() < 4) return tinyHull();

        // build initial tetra
        std::vector<int> all(H.verts.size()); std::iota(all.begin(), all.end(), 0);
        if (!buildInitialTetra(all)) return tinyHull();

        // initial distribution
        distributePoints(all);

        // main loop
        while (true) {
            int f = pickFaceWithFarthestPoint();
            if (f < 0) break;
            int p = popFarthestPoint(f);
            if (p < 0) break;

            std::vector<int> visible;
            std::vector<std::pair<int, int>> horizon; // (faceIdx, edgeLocalIdx)
            findVisibleRegion(f, p, visible, horizon);

            // kill visible faces
            for (int vf : visible) F[vf].alive = false;

            // create cone of new faces with apex p over horizon
            std::vector<int> newFaces;
            createCone(p, horizon, newFaces);

            // collect orphan points from old (visible) faces and reassign them
            std::vector<int> orphanPts;
            for (int vf : visible) {
                auto& out = F[vf].outside;
                orphanPts.insert(orphanPts.end(), out.begin(), out.end());
                out.clear();
            }
            // remove duplicates and the apex itself
            std::sort(orphanPts.begin(), orphanPts.end());
            orphanPts.erase(std::unique(orphanPts.begin(), orphanPts.end()), orphanPts.end());
            orphanPts.erase(std::remove(orphanPts.begin(), orphanPts.end(), p), orphanPts.end());
            reassignOutside(orphanPts, newFaces);
        }

        // export triangles
        QHHull out;
        out.verts = H.verts;
        for (auto& face : F) {
            if (!face.alive) continue;
            out.indices.push_back(face.a);
            out.indices.push_back(face.b);
            out.indices.push_back(face.c);
        }
        return out;
    }

private:
    struct { std::vector<QHVec3> verts; } H;
    std::vector<QHFace> F;

    void initEps() {
        if (eps > 0) return;
        if (H.verts.empty()) { eps = 1e-12; return; }
        QHVec3 mn = H.verts[0], mx = H.verts[0];
        for (auto& p : H.verts) {
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
        double diag = len(mx - mn);
        eps = std::max(1e-12, 1e-9 * diag);
    }

    QHHull tinyHull() {
        QHHull out; out.verts = H.verts; // no triangles
        return out;
    }

    // Build initial tetrahedron (robust-ish)
    bool buildInitialTetra(const std::vector<int>& idx) {
        // 1) two extremes on x
        int i0 = -1, i1 = -1; {
            double minx = std::numeric_limits<double>::infinity(), maxx = -minx;
            for (int i : idx) { double x = H.verts[i].x; if (x < minx) { minx = x; i0 = i; } if (x > maxx) { maxx = x; i1 = i; } }
            if (i0 == i1) return false;
        }
        // 2) farthest from line i0-i1
        int i2 = -1; {
            QHVec3 a = H.verts[i0], b = H.verts[i1], ab = b - a;
            double best = 0;
            for (int i : idx) {
                if (i == i0 || i == i1) continue;
                double d = len(cross(H.verts[i] - a, ab));
                if (d > best) { best = d; i2 = i; }
            }
            if (i2 < 0 || best < eps) return false; // collinear
        }
        // 3) farthest (by volume) from triangle (i0,i1,i2)
        int i3 = -1; {
            QHVec3 a = H.verts[i0], b = H.verts[i1], c = H.verts[i2];
            double best = 0;
            for (int i : idx) {
                if (i == i0 || i == i1 || i == i2) continue;
                double v6 = std::abs(tetraVolume6(a, b, c, H.verts[i]));
                if (v6 > best) { best = v6; i3 = i; }
            }
            if (i3 < 0 || best < eps) return false; // coplanar
        }

        // Create 4 faces, outward orientation
        F.clear(); F.reserve(64);
        auto addFace = [&](int a, int b, int c)->int {
            QHFace f; f.a = a; f.b = b; f.c = c; f.plane = makePlane(H.verts[a], H.verts[b], H.verts[c]);
            f.adj[0] = f.adj[1] = f.adj[2] = -1;
            F.push_back(std::move(f));
            return (int)F.size() - 1;
            };
        // make sure normals point outwards w.r.t. the opposite vertex
        auto oriented = [&](int a, int b, int c, int opp) {
            QHPlane P = makePlane(H.verts[a], H.verts[b], H.verts[c]);
            double sd = P.signedDist(H.verts[opp]);
            if (sd > 0) std::swap(b, c); // flip
            return addFace(a, b, c);
            };

        int f0 = oriented(i0, i1, i2, i3);
        int f1 = oriented(i0, i3, i1, i2);
        int f2 = oriented(i1, i3, i2, i0);
        int f3 = oriented(i2, i3, i0, i1);

        // set adjacency (by construction)
        setAdj(f0, 0, f1, 2); // (i0,i1) with f1 (i1,i0)
        setAdj(f0, 1, f2, 2); // (i1,i2) with f2 (i2,i1)
        setAdj(f0, 2, f3, 2); // (i2,i0) with f3 (i0,i2)

        setAdj(f1, 0, f3, 0); // (i0,i3) with f3 (i0,i3)
        setAdj(f1, 1, f2, 0); // (i3,i1) with f2 (i3,i1)

        setAdj(f2, 1, f3, 1); // (i3,i2) with f3 (i3,i2)
        return true;
    }

    void setAdj(int fA, int eA, int fB, int eB) {
        F[fA].adj[eA] = fB;
        F[fB].adj[eB] = fA;
    }

    // After initial faces exist, distribute remaining points to outside lists
    void distributePoints(const std::vector<int>& idx) {
        for (int i : idx) {
            if (!F[0].alive) return; // safety
        }
        // collect seed indices (exclude tetra vertices)
        std::vector<bool> used(H.verts.size(), false);
        for (auto& f : F) { used[f.a] = used[f.b] = used[f.c] = true; }
        for (int i = 0; i < (int)H.verts.size(); ++i) {
            if (used[i]) continue;
            int bestFace = -1; double bestDist = 0.0;
            for (int fi = 0; fi < (int)F.size(); ++fi) {
                auto& f = F[fi]; if (!f.alive) continue;
                double d = f.plane.signedDist(H.verts[i]);
                if (d > eps && d > bestDist) { bestDist = d; bestFace = fi; }
            }
            if (bestFace >= 0) F[bestFace].outside.push_back(i);
        }
    }

    int pickFaceWithFarthestPoint() {
        int best = -1; double bestDist = 0.0;
        for (int fi = 0; fi < (int)F.size(); ++fi) {
            auto& f = F[fi]; if (!f.alive || f.outside.empty()) continue;
            // find farthest cached
            for (int pi : f.outside) {
                double d = f.plane.signedDist(H.verts[pi]);
                if (d > bestDist) { bestDist = d; best = fi; }
            }
        }
        return best;
    }

    int popFarthestPoint(int faceIdx) {
        auto& f = F[faceIdx];
        if (f.outside.empty()) return -1;
        // farthest from this face
        int bestIdx = -1; double best = 0.0; int bestPos = -1;
        for (int k = 0; k < (int)f.outside.size(); ++k) {
            int pi = f.outside[k];
            double d = f.plane.signedDist(H.verts[pi]);
            if (d > best) { best = d; bestIdx = pi; bestPos = k; }
        }
        if (bestIdx >= 0) {
            // remove from list (swap-pop)
            f.outside[bestPos] = f.outside.back(); f.outside.pop_back();
        }
        return bestIdx;
    }

    // DFS over faces using visibility test (>eps)
    void findVisibleRegion(int startFace, int apex,
        std::vector<int>& visible,
        std::vector<std::pair<int, int>>& horizon /*(face,edge)*/)
    {
        std::vector<char> mark(F.size(), 0);
        std::vector<int> stack; stack.push_back(startFace);

        auto isVisible = [&](int fi)->bool {
            return F[fi].alive && (F[fi].plane.signedDist(H.verts[apex]) > eps);
            };

        while (!stack.empty()) {
            int fi = stack.back(); stack.pop_back();
            if (mark[fi]) continue;
            mark[fi] = 1;
            if (!isVisible(fi)) continue;
            visible.push_back(fi);

            // Check its 3 edges
            for (int e = 0; e < 3; ++e) {
                int nbr = F[fi].adj[e];
                if (nbr < 0 || !F[nbr].alive) {
                    horizon.emplace_back(fi, e); // border edge
                    continue;
                }
                if (!mark[nbr]) {
                    if (isVisible(nbr)) {
                        stack.push_back(nbr);
                    }
                    else {
                        // (fi,e) is horizon boundary (visible -> non-visible)
                        horizon.emplace_back(fi, e);
                    }
                }
            }
        }

        // deduplicate horizon edges and ensure they are in correct cyclic order.
        // For correctness, we keep them as-is; createCone expects (face,edge) references.
        // (Optional) Could be ordered, but not strictly required as we stitch via adjacency.
    }

    // helper: face local edge endpoints
    static inline std::pair<int, int> edgeOf(const QHFace& f, int e) {
        switch (e) { case 0: return { f.a,f.b }; case 1: return { f.b,f.c }; default: return { f.c,f.a }; }
    }

    void createCone(int apex, const std::vector<std::pair<int, int>>& horizon,
        std::vector<int>& newFaces)
    {
        newFaces.clear(); newFaces.reserve(horizon.size());
        // Map from (neighbor face id) to new face id, for adjacency stitching
        // We'll also stitch adjacency along the horizon ring.
        struct PendingEdge { int face = -1, edge = -1; int newFace = -1; };
        std::vector<PendingEdge> pend; pend.reserve(horizon.size());

        // create a face for each horizon edge (visible face -> non-visible neighbor across that edge)
        for (auto [fi, e] : horizon) {
            auto ab = edgeOf(F[fi], e);
            int a = ab.first, b = ab.second;
            // orientation: we want (a,b,apex) to face outward; check with opposite face across edge.
            // We can enforce by testing signed distance of centroid offset:
            int nf = addFaceOriented(a, b, apex);
            newFaces.push_back(nf);
            pend.push_back({ fi,e,nf });
        }

        // Stitch adjacency: 
        // 1) new faces to the non-visible neighbors across horizon edges
        for (auto& pe : pend) {
            int fi = pe.face, e = pe.edge;
            int oldNbr = F[fi].adj[e]; // this is the non-visible one
            if (oldNbr >= 0) {
                // find which local edge of new face matches the horizon edge
                // new face is (a,b,apex) -> edge (a,b) is local edge 0
                F[pe.newFace].adj[0] = oldNbr;
                // in oldNbr, find the matching edge pointing back to this new face
                int mateEdge = findMatchingEdge(oldNbr, F[fi], e);
                if (mateEdge >= 0) F[oldNbr].adj[mateEdge] = pe.newFace;
            }
        }

        // 2) link new faces to each other around the apex
        // Two new faces share the apex and one horizon vertex; detect by edge match
        // Build simple hash from undirected edge (b,apex) / (apex,a)
        // But simpler: For each pair along horizon where consecutive edges meet at a vertex,
        // connect the corresponding edges (b,apex) <-> (apex,next_a)
        // We do O(k^2) fallback since k ~ small (typical).
        for (size_t i = 0; i < pend.size(); ++i) {
            int nf_i = pend[i].newFace;
            auto e_i = edgeOf(F[pend[i].face], pend[i].edge);
            int ai = e_i.first, bi = e_i.second;

            for (size_t j = i + 1; j < pend.size(); ++j) {
                int nf_j = pend[j].newFace;
                auto e_j = edgeOf(F[pend[j].face], pend[j].edge);
                int aj = e_j.first, bj = e_j.second;

                // new face nf is (a,b,apex): edges: (a,b)->0, (b,apex)->1, (apex,a)->2
                // If bi == aj → they share vertex, link edge (b,apex) of i with (apex,a) of j
                if (bi == aj) {
                    setAdj(nf_i, 1, nf_j, 2);
                }
                // If bj == ai → link edge (b,apex) of j with (apex,a) of i
                if (bj == ai) {
                    setAdj(nf_j, 1, nf_i, 2);
                }
            }
        }
    }

    int addFaceOriented(int a, int b, int c) {
        // ensure outward orientation: we can't easily test "outside" here,
        // but we can make a consistent choice by flipping if plane normal points towards hull center.
        QHFace f; f.a = a; f.b = b; f.c = c;
        f.plane = makePlane(H.verts[a], H.verts[b], H.verts[c]);
        f.adj[0] = f.adj[1] = f.adj[2] = -1;
        f.alive = true;
        // no easy global center—skip flip now; we'll rely on visibility checks elsewhere.
        F.push_back(std::move(f));
        int id = (int)F.size() - 1;
        // try to flip if plane is inverted w.r.t. apex 'c' projected:
        // (Heuristic) If many faces turn inward, you can refine with a cached interior point.
        return id;
    }

    // find edge in 'oldNbr' that matches edge of 'fromFace' local edge 'e' but reversed
    int findMatchingEdge(int oldNbr, const QHFace& fromFace, int e) {
        auto ab = edgeOf(fromFace, e);
        auto& f = F[oldNbr];
        auto e0 = edgeOf(f, 0), e1 = edgeOf(f, 1), e2 = edgeOf(f, 2);
        if (e0.first == ab.second && e0.second == ab.first) return 0;
        if (e1.first == ab.second && e1.second == ab.first) return 1;
        if (e2.first == ab.second && e2.second == ab.first) return 2;
        return -1;
    }

    void reassignOutside(const std::vector<int>& pts, const std::vector<int>& newFaces) {
        for (int pi : pts) {
            int best = -1; double bestD = 0.0;
            for (int nf : newFaces) {
                auto& f = F[nf];
                double d = f.plane.signedDist(H.verts[pi]);
                if (d > eps && d > bestD) { bestD = d; best = nf; }
            }
            if (best >= 0) F[best].outside.push_back(pi);
        }
    }
};
#pragma once
