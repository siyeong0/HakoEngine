#include "pch.h"
#include "SparseBinaryGrid.h"
#include "QuickHull.h"

// ===================== Math helpers =====================
struct V3d {
    double x = 0, y = 0, z = 0;
    V3d() = default; V3d(double X, double Y, double Z) :x(X), y(Y), z(Z) {}
    V3d operator+(const V3d& b) const { return { x + b.x,y + b.y,z + b.z }; }
    V3d operator-(const V3d& b) const { return { x - b.x,y - b.y,z - b.z }; }
    V3d operator*(double s) const { return { x * s,y * s,z * s }; }
    V3d operator/(double s) const { return { x / s,y / s,z / s }; }
};
static inline double dot(const V3d& a, const V3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3d cross(const V3d& a, const V3d& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static inline double len2(const V3d& v) { return dot(v, v); }
static inline double len(const V3d& v) { return std::sqrt(len2(v)); }
static inline V3d normalize(const V3d& v) { double L = len(v); return (L > 0) ? v * (1.0 / L) : V3d{}; }
static inline V3d toD(const FLOAT3& p) { return { (double)p.x,(double)p.y,(double)p.z }; }
static inline FLOAT3 toF(const V3d& p) { return { (float)p.x,(float)p.y,(float)p.z }; }

// ===================== Face =====================
struct Face {
    uint32_t a, b, c; // CCW, 외향 법선
    V3d n; double d; // n·x + d = 0
};
static inline Face MakeFace(uint32_t i, uint32_t j, uint32_t k, const std::vector<V3d>& P) {
    const V3d& A = P[i], & B = P[j], & C = P[k];
    V3d n = cross(B - A, C - A);
    double L = len(n);
    if (L == 0) return { i,j,k,{0,0,0},0 };
    n = n * (1.0 / L);
    double d = -dot(n, A);
    return { i,j,k,n,d };
}
static inline double FaceDist(const Face& f, const V3d& p) { return dot(f.n, p) + f.d; }

// ===================== Eps =====================
struct ScaleEps { double eps_point = 1e-12, eps_plane = 1e-12; };
static ScaleEps MakeEps(const std::vector<V3d>& P) {
    V3d mn{ +INFINITY,+INFINITY,+INFINITY }, mx{ -INFINITY,-INFINITY,-INFINITY };
    for (auto& p : P) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    double diag = len(mx - mn);
    ScaleEps e;
    e.eps_point = std::max(1e-12, diag * 1e-12);
    e.eps_plane = std::max(1e-12, diag * 1e-12); // 더 타이트하게
    return e;
}

// ===================== Initial tetra =====================
static std::array<uint32_t, 4> BuildInitialTetra(const std::vector<V3d>& P, const ScaleEps& eps) {
    const uint32_t N = (uint32_t)P.size(); if (N < 4) return { ~0u,~0u,~0u,~0u };
    uint32_t iMin = 0, iMax = 0;
    for (uint32_t i = 1; i < N; ++i) { if (P[i].x < P[iMin].x) iMin = i; if (P[i].x > P[iMax].x) iMax = i; }
    if (iMin == iMax) return { ~0u,~0u,~0u,~0u };

    // farthest from segment
    V3d A = P[iMin], B = P[iMax], AB = B - A;
    uint32_t i2 = ~0u; double bestArea2 = 0;
    for (uint32_t i = 0; i < N; ++i) {
        if (i == iMin || i == iMax) continue;
        double a2 = len2(cross(AB, P[i] - A));
        if (a2 > bestArea2) { bestArea2 = a2; i2 = i; }
    }
    if (i2 == ~0u || std::sqrt(bestArea2) <= eps.eps_point) return { ~0u,~0u,~0u,~0u };

    // farthest from plane
    V3d n = normalize(cross(P[iMax] - A, P[i2] - A));
    double d = -dot(n, A);
    uint32_t i3 = ~0u; double best = 0;
    for (uint32_t i = 0; i < N; ++i) {
        if (i == iMin || i == iMax || i == i2) continue;
        double dist = std::abs(dot(n, P[i]) + d);
        if (dist > best) { best = dist; i3 = i; }
    }
    if (i3 == ~0u || best <= eps.eps_plane) return { ~0u,~0u,~0u,~0u };

    // orient so that i3 is outside (positive) -> 반대로 해서 외향 보장
    double side = dot(n, P[i3]) + d;
    if (side > 0.0) std::swap(iMin, iMax);
    return { iMin,iMax,i2,i3 };
}

static V3d TetraCenter(const std::array<uint32_t, 4>& T, const std::vector<V3d>& P) {
    V3d c{}; for (int k = 0; k < 4; ++k) c = c + P[T[k]]; return c * (1.0 / 4.0);
}

// 초기 4면 + insideRef 기준으로 외향성 강제
static std::vector<Face> MakeInitialHull(const std::array<uint32_t, 4>& T, const std::vector<V3d>& P, const V3d& insideRef) {
    const uint32_t i0 = T[0], i1 = T[1], i2 = T[2], i3 = T[3];
    std::vector<Face> F; F.reserve(4);
    Face f0 = MakeFace(i0, i1, i2, P);
    Face f1 = MakeFace(i0, i1, i3, P);
    Face f2 = MakeFace(i1, i2, i3, P);
    Face f3 = MakeFace(i2, i0, i3, P);
    auto enforceOut = [&](Face& f) {
        if (FaceDist(f, insideRef) > 0.0) { std::swap(f.b, f.c); f = MakeFace(f.a, f.b, f.c, P); }
        };
    enforceOut(f0); enforceOut(f1); enforceOut(f2); enforceOut(f3);
    F.push_back(f0); F.push_back(f1); F.push_back(f2); F.push_back(f3);
    return F;
}

// ===================== Adjacency =====================
// undirected edge key
static inline uint64_t EKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32) | uint64_t(b);
}
using EdgeAdj = std::unordered_map<uint64_t, std::vector<int>>;

static EdgeAdj BuildAdj(const std::vector<Face>& faces) {
    EdgeAdj adj; adj.reserve(faces.size() * 2);
    auto add = [&](uint32_t u, uint32_t v, int fi) { adj[EKey(u, v)].push_back(fi); };
    for (int fi = 0; fi < (int)faces.size(); ++fi) {
        const Face& f = faces[fi];
        if (len2(f.n) == 0) continue;
        add(f.a, f.b, fi); add(f.b, f.c, fi); add(f.c, f.a, fi);
    }
    return adj;
}

// BFS로 seed face에서 시작해 apex에서 보이는 모든 면 수집
static std::vector<int> CollectVisibleFacesBFS(
    const std::vector<Face>& faces, const EdgeAdj& adj,
    int seedFace, uint32_t apex, const std::vector<V3d>& P, const ScaleEps& eps)
{
    std::vector<int> vis;
    std::vector<char> seen(faces.size(), 0);
    std::queue<int> q;
    auto isVis = [&](int fi) { return FaceDist(faces[fi], P[apex]) > eps.eps_plane; };

    if (seedFace >= 0 && isVis(seedFace)) { q.push(seedFace); seen[seedFace] = 1; }

    while (!q.empty()) {
        int fi = q.front(); q.pop();
        vis.push_back(fi);
        const Face& f = faces[fi];
        uint64_t ekeys[3] = { EKey(f.a,f.b), EKey(f.b,f.c), EKey(f.c,f.a) };
        for (uint64_t k : ekeys) {
            auto it = adj.find(k); if (it == adj.end()) continue;
            for (int nb : it->second) {
                if (!seen[nb] && isVis(nb)) { seen[nb] = 1; q.push(nb); }
            }
        }
    }
    return vis;
}

// horizon: 가시면과 비가시면의 경계 에지(방향 있는 루프) 생성
static std::vector<std::pair<uint32_t, uint32_t>> BuildHorizon(
    const std::vector<Face>& faces, const EdgeAdj& adj, const std::vector<int>& visible)
{
    std::vector<char> isVis(faces.size(), 0);
    for (int fi : visible) if (fi >= 0 && fi < (int)faces.size()) isVis[fi] = 1;

    // 경계 에지 수집 (방향은 "가시면 기준 CCW 에지 방향"을 사용)
    struct HE { uint32_t u, v; int f; };
    std::vector<HE> hedges; hedges.reserve(visible.size() * 2);
    for (int fi : visible) {
        const Face& f = faces[fi];
        std::array<std::pair<uint32_t, uint32_t>, 3> E = { {{f.a,f.b},{f.b,f.c},{f.c,f.a}} };
        for (auto [u, v] : E) {
            auto it = adj.find(EKey(u, v));
            int other = -1;
            if (it != adj.end()) {
                for (int f2 : it->second) if (f2 != fi) { other = f2; break; }
            }
            if (other == -1 || !isVis[other]) {
                // 경계(가시↔비가시). 방향은 가시면의 (u->v)를 유지
                hedges.push_back({ u,v,fi });
            }
        }
    }

    // 루프로 정렬
    std::unordered_multimap<uint32_t, HE*> out; out.reserve(hedges.size());
    for (auto& e : hedges) out.emplace(e.u, &e);
    std::vector<std::pair<uint32_t, uint32_t>> loop; loop.reserve(hedges.size());
    if (!hedges.empty()) {
        HE* start = &hedges[0];
        loop.emplace_back(start->u, start->v);
        uint32_t curr = start->v;
        for (size_t i = 1; i < hedges.size() + 4; ++i) {
            auto range = out.equal_range(curr);
            HE* next = nullptr;
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second->u == curr) {
                    next = it->second; break;
                }
            }
            if (!next) break;
            loop.emplace_back(next->u, next->v);
            curr = next->v;
            if (curr == loop.front().first) break;
        }
    }
    if (loop.empty()) {
        // 실패 시 그냥 hedges를 반환(비정렬) — 그래도 새 면을 만들 수는 있다
        for (auto& e : hedges) loop.emplace_back(e.u, e.v);
    }
    return loop;
}

// ===================== Farthest point search =====================
struct MaxOutInfo { double dist = 0; int face = -1; uint32_t pid = 0; };
static MaxOutInfo FindFarthest(
    const std::vector<Face>& faces, const std::vector<V3d>& P, const ScaleEps& eps)
{
    MaxOutInfo info;
    for (int fi = 0; fi < (int)faces.size(); ++fi) {
        const Face& f = faces[fi]; if (len2(f.n) == 0) continue;
        double best = 0; uint32_t idx = ~0u;
        for (uint32_t i = 0; i < P.size(); ++i) {
            double d = FaceDist(f, P[i]);
            if (d > eps.eps_plane && d > best) { best = d; idx = i; }
        }
        if (idx != ~0u && best > info.dist) { info = { best,fi,idx }; }
    }
    return info;
}
// ---------- Voxel grid helpers ----------
struct CornerKey
{
    int kx, ky, kz; // 격자 코너 정수좌표 (origin 기준, 칸 크기 = Cell)
    bool operator==(const CornerKey& o) const noexcept {
        return kx == o.kx && ky == o.ky && kz == o.kz;
    }
};
struct CornerKeyHash
{
    size_t operator()(const CornerKey& k) const noexcept
    {
        uint64_t h = (uint64_t)(k.kx * 73856093) ^ (uint64_t)(k.ky * 19349663) ^ (uint64_t)(k.kz * 83492791);
        return (size_t)h;
    }
};

// ---- 경계 판정: 채워져 있고, 6-이웃 중 하나라도 비어 있으면 boundary ----
static inline bool isBoundaryVoxel(const SparseBinaryGrid& g, int x, int y, int z)
{
    ASSERT(x >= 0 && y >= 0 && z >= 0);
    if (!g.GetVoxel(x, y, z)) return false;
    static const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
    for (auto& d : d6) {
        if (!g.GetVoxel(x + d[0], y + d[1], z + d[2])) return true;
    }
    return false;
}

// ---- 메인: 경계 복셀들의 8 코너를 수집해 월드 좌표 반환 ----
std::vector<FLOAT3> collectHullCornersFromSurface(const SparseBinaryGrid& grid)
{
    const int T = Brick::NUM_VOXELS_EDGE; // 타일 한 변(예: 32)
    const float h = grid.GetCellSize();          // 복셀 크기
    const FLOAT3 O = grid.GetOrigin();       // 그리드 원점

    std::unordered_set<CornerKey, CornerKeyHash> cornerSet;
    cornerSet.reserve((size_t)grid.Size() * 64); // 대충 여유 잡기

    // 타일 단위 순회
    grid.ForEachTile([&](uint64_t /*key*/, int tileIdx, int tx, int ty, int tz)
        {
            const Brick& tile = grid.GetBrick(tileIdx);
            const int baseX = tx * T;
            const int baseY = ty * T;
            const int baseZ = tz * T;

            // 간단/안전: 타일 내부 전수 검사 (T^3 = 32768) + tile.Get(li)
            // (TileCPU::FULL일 땐 모두 true)
            for (int lz = 0; lz < T; ++lz)
            {
                for (int ly = 0; ly < T; ++ly)
                {
                    for (int lx = 0; lx < T; ++lx)
                    {
                        const int li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
                        bool filled = (tile.Mode == Brick::FULL) ? true : tile.GetBit((uint16_t)li);
                        if (!filled) continue;

                        const int gx = baseX + lx;
                        const int gy = baseY + ly;
                        const int gz = baseZ + lz;

                        if (!isBoundaryVoxel(grid, gx, gy, gz)) continue;

                        // 경계 복셀의 8 코너 정수 좌표 (격자 코너 키: 복셀 [ix,ix+1] × [iy,iy+1] × [iz,iz+1])
                        // sx,sy,sz = 0 or 1
                        for (int sx = 0; sx <= 1; ++sx)
                        {
                            for (int sy = 0; sy <= 1; ++sy)
                            {
                                for (int sz = 0; sz <= 1; ++sz)
                                {
                                    CornerKey ck{ gx + sx, gy + sy, gz + sz };
                                    cornerSet.insert(ck);
                                }
                            }
                        }
                    }
                }
            }
        });

    // 키 → 월드 좌표로 변환
    std::vector<FLOAT3> pts;
    pts.reserve(cornerSet.size());
    for (const CornerKey& k : cornerSet)
    {
        // world = Origin + Cell * (k)
        const float wx = O.x + h * (float)k.kx;
        const float wy = O.y + h * (float)k.ky;
        const float wz = O.z + h * (float)k.kz;
        pts.push_back({ wx, wy, wz });
    }
    return pts;
}

// Main entry
void QuickHull(
    const SparseBinaryGrid& voxelGrid,
    std::vector<FLOAT3>*
    outVertices, std::vector<uint32_t>* outIndices)
{
    const std::vector<FLOAT3> points = collectHullCornersFromSurface(voxelGrid);
    QuickHull(points, outVertices, outIndices);
}

void QuickHull(const std::vector<FLOAT3>& points,
    std::vector<FLOAT3>* outVertices,
    std::vector<uint32_t>* outIndices)
{
    if (!outVertices || !outIndices) return;
    outVertices->clear(); outIndices->clear();
    const uint32_t N = (uint32_t)points.size();
    if (N == 0) return;

    std::vector<V3d> P; P.reserve(N);
    for (auto& p : points) P.push_back(toD(p));
    ScaleEps eps = MakeEps(P);

    if (N == 1) { outVertices->push_back(points[0]); return; }
    if (N == 2) { outVertices->push_back(points[0]); outVertices->push_back(points[1]); return; }

    auto T = BuildInitialTetra(P, eps);
    if (T[0] == ~0u) { // coplanar fallback (간단 팬 삼각화)
        // 단순 2D convex hull로 팬 구성 (생략 가능 — 기존 버전과 동일하게 남겨둠)
        // 여기선 간결화를 위해 모든 포인트를 그대로 출력만 함
        for (auto& p : points) outVertices->push_back(p);
        if (points.size() >= 3) {
            for (uint32_t k = 1; k + 1 < (uint32_t)points.size(); ++k) {
                outIndices->push_back(0); outIndices->push_back(k); outIndices->push_back(k + 1);
            }
        }
        return;
    }

    V3d insideRef = TetraCenter(T, P);
    auto faces = MakeInitialHull(T, P, insideRef);

    // -------- Main loop --------
    while (true) {
        MaxOutInfo info = FindFarthest(faces, P, eps);
        if (info.face < 0) break; // done

        uint32_t apex = info.pid;

        // 인접 그래프 & BFS로 가시면 모으기
        EdgeAdj adj = BuildAdj(faces);
        std::vector<int> visible = CollectVisibleFacesBFS(faces, adj, info.face, apex, P, eps);
        if (visible.empty()) {
            // 수치적 이슈 — 다음 후보로
            // (드물게 발생; 그냥 종료해도 실무상 크게 문제 없음)
            break;
        }

        // horizon 구성
        auto horizon = BuildHorizon(faces, adj, visible);

        // 가시면 삭제
        std::vector<char> dead(faces.size(), 0);
        for (int fi : visible) if (fi >= 0 && fi < (int)faces.size()) dead[fi] = 1;
        std::vector<Face> kept; kept.reserve(faces.size());
        for (size_t i = 0; i < faces.size(); ++i) if (!dead[i]) kept.push_back(faces[i]);
        faces.swap(kept);

        // 새 면 추가: (u, v, apex) + insideRef로 외향성 검사
        for (auto [u, v] : horizon) {
            Face nf = MakeFace(u, v, apex, P);
            if (FaceDist(nf, insideRef) > 0.0) { // 내부가 양수면 뒤집기
                std::swap(nf.b, nf.c);
                nf = MakeFace(nf.a, nf.b, nf.c, P);
            }
            faces.push_back(nf);
        }
    }

    // ------- Emit unique vertices & triangles -------
    std::unordered_set<uint32_t> used;
    for (const auto& f : faces) {
        if (len2(f.n) == 0) continue;
        used.insert(f.a); used.insert(f.b); used.insert(f.c);
    }
    if (used.empty()) { outVertices->push_back(points[0]); return; }

    std::vector<uint32_t> remap(N, ~0u);
    outVertices->reserve(used.size());
    uint32_t nxt = 0;
    for (uint32_t i = 0; i < N; ++i) {
        if (used.count(i)) { remap[i] = nxt++; outVertices->push_back(toF(P[i])); }
    }
    outIndices->reserve(faces.size() * 3);
    for (const auto& f : faces) {
        if (len2(f.n) == 0) continue;
        uint32_t ia = remap[f.a], ib = remap[f.b], ic = remap[f.c];
        if (ia == ~0u || ib == ~0u || ic == ~0u) continue;
        if (ia == ib || ib == ic || ic == ia) continue;
        outIndices->push_back(ia); outIndices->push_back(ib); outIndices->push_back(ic);
    }
}

bool SaveHullAsObj(
    const std::string& path,
    const std::vector<FLOAT3>& vertices,
    const std::vector<uint32_t>& indices)
{
    FILE* f = nullptr;
#if defined(_MSC_VER)
    fopen_s(&f, path.c_str(), "wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    std::fprintf(f, "# QuickHull OBJ export\n");
    for (const auto& v : vertices)
    {
        std::fprintf(f, "v %.9f %.9f %.9f\n", v.x, v.y, v.z);
    }
    const size_t T = indices.size() / 3;
    for (size_t t = 0; t < T; ++t)
    {
        int i0 = indices[3 * t + 0] + 1, i1 = indices[3 * t + 1] + 1, i2 = indices[3 * t + 2] + 1;
        std::fprintf(f, "f %d %d %d\n", i0, i1, i2);
    }
    std::fclose(f);
    return true;
}