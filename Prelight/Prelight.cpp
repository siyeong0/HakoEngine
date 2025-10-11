#include "pch.h"
#include "Prelight.h"

bool ENGINECALL Prelight::Initialize()
{
	std::cout << "Prelight::Initialize called." << std::endl;
	return true;
}

void ENGINECALL Prelight::Cleanup()
{
	std::cout << "Prelight::Cleanup called." << std::endl;
}

// ================================================================
// Apove is the member method implementation of IPrelight interface.
// Underneath is the global facade implementation.
// ================================================================
// Global facade vs. member methods — why this split?
// • Member methods (above) are the *real backend implementation* of the IPrelight interface.
//   - They own/use internal state (resources, lifetime).
//   - Different backends (CPU reference, D3D12, CUDA, stub) implement these virtual methods.
//   - They live on an actual object instance (created by the app/DLL factory).
//
// • Global facade functions (below) are a thin, static-style API for callers.
//   - Callers use a single entry point: hfx::PrecomputeAtmos(...).
//   - The facade *forwards* to whichever IPrelight backend was registered via hfx::SetBackend().
//   - This keeps headers simple and hides global state: g_backend is file-local (no external symbol).
//   - It decouples the app from a specific implementation and preserves ABI: the app never calls
//     a DLL symbol directly, only the facade → easy to hot-swap backends.
//
// • Why separate them?
//   - Convenience: “static function” UX for the engine code, while still using a pluggable instance.
//   - Encapsulation: backend pointer is private to this TU; other TUs can’t see or mutate it.
//   - Flexibility: swap implementations (CPU/D3D12/CUDA) without changing call sites.
//   - Testability: register a mock IPrelight for unit tests.
//   - ODR/visibility safety: avoids inline/global variables leaking across translation units.
//
// • Lifecycle & usage:
//   - Call hfx::SetBackend(&impl) once during boot (before any facade call).
//   - Call hfx::ShutDown() during shutdown to clear the pointer.
//   - Facade asserts if no backend is set.
//
// • Threading note:
//   - Initialize/SetBackend/ShutDown should be done in a single-threaded init/shutdown phase,
//     or guarded externally if used from multiple threads.

#include "ComputeAtmos.h"

bool ENGINECALL Prelight::PrecomputeAtmos(const AtmosParams& in, AtmosResult* out) const
{
	ComputeAtmosCPU(in, out);
	return true;
}

#include "ConvexDecomposition.h"
#include <fstream>
bool ENGINECALL Prelight::DecomposeToConvex(const MeshData& meshData) const
{
	GpuFriendlySparseGridFB solidVoxelGrid;
	VoxelizeToSparse(meshData, 0.05f, &solidVoxelGrid);
	std::vector<VoxelComponent> components;
	ExtractConnectedComponents6(solidVoxelGrid, components);


    {
        // 메타
        const float cell = solidVoxelGrid.Cell;
        const FLOAT3 origin = solidVoxelGrid.Origin;

        std::ofstream ofs("C:\\Dev\\VoxVis\\Assets\\voxels.txt");
        ofs << "Components: " << components.size()
            << ", Cell Size: " << cell << "\n\n";

        for (size_t ci = 0; ci < components.size(); ++ci)
        {
            const VoxelComponent& comp = components[ci];

            // 컴포넌트 타일 집합(빠른 포함 판정)
            std::unordered_set<uint64_t> tileSet;
            tileSet.reserve(comp.tiles.size() * 2);
            for (const auto& t : comp.tiles)
                tileSet.insert(pack3x21(t.tx, t.ty, t.tz));

            // 컴포넌트 AABB(복셀 인덱스) → 월드 AABB(참고용 출력)
            const int ix0 = comp.minX, iy0 = comp.minY, iz0 = comp.minZ;
            const int ix1 = comp.maxX, iy1 = comp.maxY, iz1 = comp.maxZ;

            // 복셀은 [i, i+1) 공간을 차지하므로, 월드 최대는 (max+1)*cell로 잡는 게 자연스러움
            const FLOAT3 wmin{
                origin.x + ix0 * cell,
                origin.y + iy0 * cell,
                origin.z + iz0 * cell
            };
            const FLOAT3 wmax{
                origin.x + (ix1 + 1) * cell,
                origin.y + (iy1 + 1) * cell,
                origin.z + (iz1 + 1) * cell
            };

            ofs << "==== Component " << ci
                << " | voxels=" << comp.voxelCount
                << " | surface=" << comp.surfaceCount
                << " | AABB(idx)=[" << ix0 << ".." << ix1 << ", "
                << iy0 << ".." << iy1 << ", "
                << iz0 << ".." << iz1 << "]"
                << " | AABB(world)=Min(" << wmin.x << ", " << wmin.y << ", " << wmin.z
                << ") Max(" << wmax.x << ", " << wmax.y << ", " << wmax.z << ")"
                << "\n";

            // Z 슬라이스별로 출력 (컴포넌트 AABB만 순회)
            for (int z = iz0; z <= iz1; ++z)
            {
                ofs << "Layer z=" << z << " (local " << (z - iz0) << "):\n";
                for (int y = iy0; y <= iy1; ++y)
                {
                    for (int x = ix0; x <= ix1; ++x)
                    {
                        // 이 복셀이 속한 타일이 컴포넌트 타일 집합에 들어있는지 먼저 확인
                        const int tx = x >> 5, ty = y >> 5, tz = z >> 5;
                        const bool inThisComponent = (tileSet.find(pack3x21(tx, ty, tz)) != tileSet.end());

                        if (!inThisComponent) {
                            ofs << ' ' << ' ';   // 공백1 문자 + 구분 공백
                            continue;
                        }

                        // 같은 타일 소속이면 실제 복셀 존재 여부를 조회해서 출력
                        const bool on = solidVoxelGrid.GetVoxelIndex(x, y, z);
                        ofs << (on ? '#' : ' ') << ' ';
                    }
                    ofs << "\n";
                }
                ofs << "\n";
            }
            ofs << "\n"; // 컴포넌트 구분 빈 줄
        }
    }

	return true;
}