#pragma once
#include "pch.h"

class SparseBinaryGrid;

void ExtractConnectedComponents6(
	const SparseBinaryGrid& solid,
	std::vector<SparseBinaryGrid>* outComponents);

double ComputeConcavity(
	const SparseBinaryGrid& orig,
	const SparseBinaryGrid& hull);


// TODO: 
//후보 강화 : 위 GenerateAxisSweepCandidates 외에 SDF 그라디언트 접선, 로컬 PCA, CH 기반 법선 등 추가하면 품질↑.
//정밀 평가 단계 : 최상위 1~3개의 후보에 한해 각 파트의 로컬 CH 를 생성(포인트 support - culling 후 QuickHull)하고 concavity를 정확히 재평가하면 선택 품질이 확 좋아져.
//브릭 단위 가속 : SplitGridByPlane는 현재 복셀 단위.평면과 브릭 AABB의 반공간 테스트로 브릭 전체를 한 번에 라우팅하면 훨씬 빨라진다.
//cutCount : 지금은 평면 - 복셀 중심 거리 기반 근사.필요하면 복셀 박스 - 평면 교차로 더 정확히 계산 가능.
std::optional<Plane> PickBestSplitPlane(
	const SparseBinaryGrid& solid,
	const SparseBinaryGrid& hull,
	double concavityThreshold,
	size_t minVoxelsPerPart);