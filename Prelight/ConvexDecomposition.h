#pragma once
#include "pch.h"

class SparseBinaryGrid;

void ExtractConnectedComponents6(
	const SparseBinaryGrid& solid,
	std::vector<SparseBinaryGrid>* outComponents);

double ComputeConcavity(
	const SparseBinaryGrid& orig,
	const SparseBinaryGrid& hull);