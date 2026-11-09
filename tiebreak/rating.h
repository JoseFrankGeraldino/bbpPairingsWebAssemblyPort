/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#ifndef RATING_H
#define RATING_H

#include <optional>
#include <vector>

#include "decimal.h"

namespace tiebreak
{
  namespace rating
  {
    /**
     * The expected score of a player against an opponent (FIDE B.02 table
     * 8.1b), from the player's side.
     */
    Decimal expectedScore(int ratingPlayer, int ratingOpponent);

    /** The sum of the expected scores against all the opponents. */
    Decimal sumExpectedScore(
      int ratingPlayer,
      const std::vector<int> &ratingsOpponents);

    /** ARO, C.07 10.1: the rounded average, or nothing without opponents. */
    std::optional<int> averageRatingOfOpponents(
      const std::vector<int> &ratingsOpponents);

    /** TPR, C.07 10.2: ARO plus dp of the percentage score. */
    std::optional<int> tournamentPerformanceRating(
      const Decimal &score,
      const std::vector<int> &ratingsOpponents);

    /**
     * PTP, C.07 10.3: the lowest rating whose expected score reaches the
     * score.
     */
    std::optional<int> perfectTournamentPerformance(
      const Decimal &score,
      const std::vector<int> &ratingsOpponents);
  }
}

#endif
