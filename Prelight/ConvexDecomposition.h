#pragma once
#include "pch.h"
#include "SparseBinaryGrid.h"

void ExtractConnectedComponents6(
	const SparseBinaryGrid& solid,
	std::vector<SparseBinaryGrid>* outComponents);


struct SplitWeights {
    double LambdaBalance = 0.35; // |ratio-0.5| 패널티
    double LambdaCut = 0.00; // 평면 근방 컷 패널티
    double WWeakness = 0.00; // 약점필드(없으면 0)
    double WNeck = 0.00; // 목 감소(없으면 0)
    double WStress = 0.00; // 응력 정렬(없으면 0)
    double WMaterial = 0.00; // 재료 선호(없으면 0)
};

struct SplitParams {
    double ConcavityThreshold = 0.25;
    size_t MinVoxelsPerPart = 256;
    int    SweepsPerAxis = 16;
    double BalanceTarget = 0.5;
    double EarlyRejectBalance = 0.40; // |ratio-0.5| > 이면 기각
    double CutBandGrid = 0.25; // 그리드단위 컷 밴드
};

struct SplitResult {
    Plane plane;
    SparseBinaryGrid A, B;
    double score;
    double ratio;   // |A|/(|A|+|B|)
    double cA, cB;  // 각 파트 concavity(갭 버전)
};

std::optional<SplitResult>
PickBestSplitPlaneEx(const SparseBinaryGrid& solid,
    const SparseBinaryGrid& hull,
    const SplitParams& params,
    const SplitWeights& weights);