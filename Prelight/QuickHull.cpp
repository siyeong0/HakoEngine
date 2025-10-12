#include "pch.h"
#include "QuickHull.h"

// ===== 내부 수학 유틸 =====
struct QHVec3 {
    double x = 0, y = 0, z = 0; QHVec3() = default; QHVec3(double X, double Y, double Z) :x(X), y(Y), z(Z) {}
    QHVec3 operator+(const QHVec3& b) const { return { x + b.x,y + b.y,z + b.z }; }
    QHVec3 operator-(const QHVec3& b) const { return { x - b.x,y - b.y,z - b.z }; }
    QHVec3 operator*(double s) const { return { x * s,y * s,z * s }; }
};
inline static double dot(const QHVec3& a, const QHVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline static QHVec3 cross(const QHVec3& a, const QHVec3& b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
inline static double len(const QHVec3& a) { return std::sqrt(dot(a, a)); }
struct QHPlane { QHVec3 n; double d = 0; double sd(const QHVec3& p)const { return dot(n, p) + d; } };
inline static QHPlane makePlane(const QHVec3& a, const QHVec3& b, const QHVec3& c) {
    QHVec3 n = cross(b - a, c - a); double L = len(n); if (L > 0) n = n * (1.0 / L);
    QHPlane P; P.n = n; P.d = -dot(n, a); return P;
}
inline static double tetVol6(const QHVec3& a, const QHVec3& b, const QHVec3& c, const QHVec3& d) {
    return dot(cross(b - a, c - a), d - a);
}

// ===== 내부 구조 =====
struct QHFace {
    int a = -1, b = -1, c = -1;         // CCW (바깥에서 볼 때)
    int adj[3] = { -1,-1,-1 };    // (a,b),(b,c),(c,a)
    QHPlane plane;
    bool alive = true;
    std::vector<int> outside;
};
static inline std::pair<int, int> edgeOf(const QHFace& f, int e) {
    switch (e) { case 0: return { f.a,f.b }; case 1: return { f.b,f.c }; default: return { f.c,f.a }; }
}
static inline int nextEdge(int e) { return (e + 1) % 3; }
static inline int prevEdge(int e) { return (e + 2) % 3; }

class QuickHullBuilder {
public:
    double eps = -1.0;

    QuickHull Run(const std::vector<FLOAT3>& ptsIn) {
        std::vector<QHVec3> pts(ptsIn.size());
        for (size_t i = 0; i < ptsIn.size(); ++i) pts[i] = QHVec3(ptsIn[i].x, ptsIn[i].y, ptsIn[i].z);
        return Run(pts);
    }

    QuickHull Run(const std::vector<QHVec3>& points) {
        H.verts = points;
        initEps();
        if (H.verts.size() < 4) return {};

        // 초기 테트라
        std::vector<int> all(H.verts.size()); std::iota(all.begin(), all.end(), 0);
        int i0, i1, i2, i3;
        if (!buildInitialTetra(all, i0, i1, i2, i3)) return {};

        // 내부 기준점(초기 테트라 중심) — 면 방향 판정에 사용
        interior = (H.verts[i0] + H.verts[i1] + H.verts[i2] + H.verts[i3]) * 0.25;

        distributePoints(all);

        // 메인 루프
        while (true) {
            int f = pickFaceWithFarthestPoint(); if (f < 0) break;
            int p = popFarthestPoint(f); if (p < 0) break;

            std::vector<int> visible;
            std::vector<std::pair<int, int>> horizonFE; // (face, edge)
            findVisibleRegion(f, p, visible, horizonFE);

            // 지평선 에지를 “방향 포함”해서 추출 (보이는 face의 CCW 에지 방향으로 a->b)
            std::vector<std::pair<int, int>> horizonAB; horizonAB.reserve(horizonFE.size());
            for (auto [fi, e] : horizonFE) {
                auto ab = edgeOf(F[fi], e);
                horizonAB.push_back(ab); // 이미 보이는 페이스의 CCW 순서
            }

            // 지평선을 “연속 링”으로 정렬
            std::vector<std::pair<int, int>> ring = orderHorizonRing(horizonAB);
            if (ring.empty()) continue; // 안전

            // 가시 페이스 kill
            for (int vf : visible) F[vf].alive = false;

            // 원뿔 생성 (정렬된 링 사용) + 인접 연결
            std::vector<int> newFaces;
            createConeOrdered(p, horizonFE, ring, newFaces);

            // orphan 포인트 재분배
            std::vector<int> orphan;
            for (int vf : visible) { auto& out = F[vf].outside; orphan.insert(orphan.end(), out.begin(), out.end()); out.clear(); }
            std::sort(orphan.begin(), orphan.end()); orphan.erase(std::unique(orphan.begin(), orphan.end()), orphan.end());
            orphan.erase(std::remove(orphan.begin(), orphan.end(), p), orphan.end());
            reassignOutside(orphan, newFaces);
        }

        // 내보내기
        QuickHull out;
        out.Vertices.reserve(H.verts.size());
        for (auto& v : H.verts) out.Vertices.emplace_back((FLOAT)v.x, (FLOAT)v.y, (FLOAT)v.z);
        for (auto& face : F) { if (!face.alive) continue; out.Indices.push_back(face.a); out.Indices.push_back(face.b); out.Indices.push_back(face.c); }
        return out;
    }

private:
    struct { std::vector<QHVec3> verts; } H;
    std::vector<QHFace> F;
    QHVec3 interior{ 0,0,0 };

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

    bool buildInitialTetra(const std::vector<int>& idx, int& o0, int& o1, int& o2, int& o3) {
        int i0 = -1, i1 = -1; {
            double mn = +std::numeric_limits<double>::infinity(), mx = -mn;
            for (int i : idx) { double x = H.verts[i].x; if (x < mn) { mn = x; i0 = i; } if (x > mx) { mx = x; i1 = i; } }
            if (i0 == i1) return false;
        }
        int i2 = -1; {
            QHVec3 a = H.verts[i0], b = H.verts[i1], ab = b - a; double best = 0;
            for (int i : idx) {
                if (i == i0 || i == i1) continue;
                double d = len(cross(H.verts[i] - a, ab));
                if (d > best) { best = d; i2 = i; }
            }
            if (i2 < 0 || best < eps) return false;
        }
        int i3 = -1; {
            QHVec3 a = H.verts[i0], b = H.verts[i1], c = H.verts[i2]; double best = 0;
            for (int i : idx) {
                if (i == i0 || i == i1 || i == i2) continue;
                double v6 = std::abs(tetVol6(a, b, c, H.verts[i]));
                if (v6 > best) { best = v6; i3 = i; }
            }
            if (i3 < 0 || best < eps) return false;
        }
        F.clear(); F.reserve(64);
        auto addFace = [&](int a, int b, int c)->int {
            QHFace f; f.a = a; f.b = b; f.c = c; f.plane = makePlane(H.verts[a], H.verts[b], H.verts[c]); F.push_back(std::move(f));
            return (int)F.size() - 1;
            };
        auto oriented = [&](int a, int b, int c, int opp) {
            QHPlane P = makePlane(H.verts[a], H.verts[b], H.verts[c]);
            if (P.sd(H.verts[opp]) > 0) std::swap(b, c); // 바깥을 향하도록
            return addFace(a, b, c);
            };
        int f0 = oriented(i0, i1, i2, i3);
        int f1 = oriented(i0, i3, i1, i2);
        int f2 = oriented(i1, i3, i2, i0);
        int f3 = oriented(i2, i3, i0, i1);

        setAdj(f0, 0, f1, 2);
        setAdj(f0, 1, f2, 2);
        setAdj(f0, 2, f3, 2);
        setAdj(f1, 0, f3, 0);
        setAdj(f1, 1, f2, 0);
        setAdj(f2, 1, f3, 1);

        o0 = i0; o1 = i1; o2 = i2; o3 = i3;
        return true;
    }
    void setAdj(int fa, int ea, int fb, int eb) { F[fa].adj[ea] = fb; F[fb].adj[eb] = fa; }

    void distributePoints(const std::vector<int>& idx) {
        std::vector<char> used(H.verts.size(), 0);
        for (auto& f : F) { used[f.a] = used[f.b] = used[f.c] = 1; }
        for (int i = 0; i < (int)H.verts.size(); ++i) {
            if (used[i]) continue;
            int best = -1; double bd = 0;
            for (int fi = 0; fi < (int)F.size(); ++fi) {
                auto& f = F[fi]; if (!f.alive) continue;
                double d = f.plane.sd(H.verts[i]);
                if (d > eps && d > bd) { bd = d; best = fi; }
            }
            if (best >= 0) F[best].outside.push_back(i);
        }
    }

    int pickFaceWithFarthestPoint() {
        int best = -1; double bd = 0;
        for (int fi = 0; fi < (int)F.size(); ++fi) {
            auto& f = F[fi]; if (!f.alive || f.outside.empty()) continue;
            for (int pi : f.outside) {
                double d = f.plane.sd(H.verts[pi]);
                if (d > bd) { bd = d; best = fi; }
            }
        }
        return best;
    }
    int popFarthestPoint(int fi) {
        auto& f = F[fi]; if (f.outside.empty()) return -1;
        int bi = -1, bp = -1; double bd = 0;
        for (int k = 0; k < (int)f.outside.size(); ++k) {
            int pi = f.outside[k]; double d = f.plane.sd(H.verts[pi]);
            if (d > bd) { bd = d; bi = pi; bp = k; }
        }
        if (bp >= 0) { f.outside[bp] = f.outside.back(); f.outside.pop_back(); }
        return bi;
    }

    void findVisibleRegion(int startFace, int apex,
        std::vector<int>& visible,
        std::vector<std::pair<int, int>>& horizonFE)
    {
        std::vector<char> mark(F.size(), 0);
        std::vector<int> st; st.push_back(startFace);

        auto isVis = [&](int fi)->bool { return F[fi].alive && (F[fi].plane.sd(H.verts[apex]) > eps); };

        while (!st.empty()) {
            int fi = st.back(); st.pop_back();
            if (mark[fi]) continue; mark[fi] = 1;
            if (!isVis(fi)) continue;
            visible.push_back(fi);

            for (int e = 0; e < 3; ++e) {
                int nb = F[fi].adj[e];
                if (nb < 0 || !F[nb].alive) { horizonFE.emplace_back(fi, e); continue; }
                if (!mark[nb]) {
                    if (isVis(nb)) st.push_back(nb);
                    else horizonFE.emplace_back(fi, e);
                }
            }
        }
    }

    // 지평선 에지들을 (a->b) 체인으로 정렬
    std::vector<std::pair<int, int>> orderHorizonRing(const std::vector<std::pair<int, int>>& edges) {
        if (edges.empty()) return {};
        // a->b 매핑으로 체인 구성
        std::unordered_multimap<int, int> nextMap; nextMap.reserve(edges.size() * 2);
        std::unordered_multimap<int, int> prevMap;
        for (auto& e : edges) { nextMap.emplace(e.first, e.second); prevMap.emplace(e.second, e.first); }

        // 시작 에지 선택: 임의의 첫 에지
        std::vector<std::pair<int, int>> ring; ring.reserve(edges.size());
        int a = edges[0].first, b = edges[0].second;
        ring.emplace_back(a, b);
        // 앞으로
        while ((int)ring.size() < (int)edges.size()) {
            auto range = nextMap.equal_range(b);
            bool found = false;
            for (auto it = range.first; it != range.second; ++it) {
                int nb = it->second;
                // 사용된 에지인지 체크
                bool used = false;
                for (auto& r : ring) { if (r.first == b && r.second == nb) { used = true; break; } }
                if (!used) { ring.emplace_back(b, nb); b = nb; found = true; break; }
            }
            if (!found) break; // 단일 루프로 충분 — 일반적으로 한 개의 링
        }
        return ring;
    }

    int addFace(int a, int b, int c) {
        QHFace f; f.a = a; f.b = b; f.c = c; f.plane = makePlane(H.verts[a], H.verts[b], H.verts[c]); F.push_back(std::move(f));
        return (int)F.size() - 1;
    }
    int addFaceOutward(int a, int b, int c) {
        int id = addFace(a, b, c);
        // 내부점에 대해 음수(= 내부)여야 함. 양수면 뒤집는다.
        if (F[id].plane.sd(interior) > 0) {
            std::swap(F[id].b, F[id].c);
            F[id].plane = makePlane(H.verts[F[id].a], H.verts[F[id].b], H.verts[F[id].c]);
        }
        return id;
    }

    int findMatchingEdge(int oldNbr, int a, int b) {
        auto& f = F[oldNbr];
        auto e0 = edgeOf(f, 0), e1 = edgeOf(f, 1), e2 = edgeOf(f, 2);
        if (e0.first == b && e0.second == a) return 0;
        if (e1.first == b && e1.second == a) return 1;
        if (e2.first == b && e2.second == a) return 2;
        return -1;
    }

    void createConeOrdered(
        int apex,
        const std::vector<std::pair<int, int>>& horizonFE,
        const std::vector<std::pair<int, int>>& ringAB,
        std::vector<int>& newFaces)
    {
        // (a,b) → new face id 매핑
        std::unordered_map<long long, int> newByEdge; newByEdge.reserve(ringAB.size() * 2);
        auto keyAB = [](int a, int b)->long long { return ((long long)a << 32) ^ (long long)b; };

        newFaces.clear(); newFaces.reserve(ringAB.size());

        // 1) 링을 따라 새 면 생성: (a,b,apex) (방향은 addFaceOutward에서 보정)
        for (auto& ab : ringAB) {
            int a = ab.first, b = ab.second;
            int nf = addFaceOutward(a, b, apex);
            newFaces.push_back(nf);
            newByEdge.emplace(keyAB(a, b), nf);
        }

        // 2) 각 새 면 edge0=(a,b) 를 과거 “비가시” 이웃(face)와 연결
        // horizonFE에는 (보이는face, edge) 저장. 그 edge의 이웃이 비가시 페이스.
        for (auto [fi, e] : horizonFE) {
            auto ab = edgeOf(F[fi], e); // 보이는 face의 CCW 에지방향
            int oldNbr = F[fi].adj[e];  // 비가시 이웃
            auto it = newByEdge.find(keyAB(ab.first, ab.second));
            if (it == newByEdge.end()) continue; // 정렬 링에 없으면 skip (이상 케이스)
            int nf = it->second;

            if (oldNbr >= 0) {
                F[nf].adj[0] = oldNbr; // (a,b) 경계
                int mate = findMatchingEdge(oldNbr, ab.first, ab.second);
                if (mate >= 0) F[oldNbr].adj[mate] = nf;
            }
        }

        // 3) 새 면들끼리 apex를 공유하는 옆변들 연결
        // 링이 (a0,b0),(a1,b1),... 이고 b_i == a_{i+1}.  
        // 새면 i: (a_i,b_i,apex) → edge1=(b_i,apex), edge2=(apex,a_i)
        const int M = (int)ringAB.size();
        for (int i = 0; i < M; ++i) {
            int j = (i + 1) % M;
            int fi = newByEdge[keyAB(ringAB[i].first, ringAB[i].second)];
            int fj = newByEdge[keyAB(ringAB[j].first, ringAB[j].second)];
            // (b_i,apex) of fi  <->  (apex,a_{i+1}) of fj
            setAdj(fi, 1, fj, 2);
        }
    }

    void reassignOutside(const std::vector<int>& pts, const std::vector<int>& newFaces) {
        for (int pi : pts) {
            int best = -1; double bd = 0;
            for (int nf : newFaces) {
                double d = F[nf].plane.sd(H.verts[pi]);
                if (d > eps && d > bd) { bd = d; best = nf; }
            }
            if (best >= 0) F[best].outside.push_back(pi);
        }
    }
};

// ===== Public API =====
QuickHull QuickHull::Create(const std::vector<FLOAT3>& points) {
    QuickHullBuilder qh;
    return qh.Run(points);
}

bool QuickHull::SaveAsOBJ(const std::string& path) const {
    FILE* f = nullptr;
#if defined(_MSC_VER)
    fopen_s(&f, path.c_str(), "wb");
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    std::fprintf(f, "# QuickHull OBJ export\n");
    for (const auto& v : Vertices) std::fprintf(f, "v %.9f %.9f %.9f\n", v.x, v.y, v.z);
    const size_t T = Indices.size() / 3;
    for (size_t t = 0; t < T; ++t) {
        int i0 = Indices[3 * t + 0] + 1, i1 = Indices[3 * t + 1] + 1, i2 = Indices[3 * t + 2] + 1;
        std::fprintf(f, "f %d %d %d\n", i0, i1, i2);
    }
    std::fclose(f);
    return true;
}
