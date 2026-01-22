#pragma once

#include "position.h"

void generate_pseudo_moves(const Position& pos, MoveList& list, bool captures_only = false);
void generate_legal_moves(Position& pos, MoveList& list, bool captures_only = false);
