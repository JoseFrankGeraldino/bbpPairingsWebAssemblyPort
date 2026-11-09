/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tables are FIDE Handbook B.02 tables 8.1a and 8.1b as
 * transcribed in Gacrux (TieBreakServer, Copyright (c) 2024 FIDE, MIT
 * licence, https://www.gacrux.no); see THIRD_PARTY_NOTICES.md.
 */

#ifndef FIDETABLES_H
#define FIDETABLES_H

#include <array>

namespace tiebreak
{
  /** Rating difference (0 to 800) to the expected score in percent. */
  extern const std::array<int, 801> DIFF_TO_PD;

  /** Percentage score (0 to 100) to the rating difference dp. */
  extern const std::array<int, 101> SCORE_TO_DP;
}

#endif
