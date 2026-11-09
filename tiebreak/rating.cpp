/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "decimal.h"
#include "fidetables.h"
#include "rating.h"

namespace tiebreak
{
  namespace rating
  {
    namespace
    {
      /**
       * Python's int(round(numerator / denominator + 0.000001)): a float
       * division, rounded half-even. The small addend is the reference
       * implementation's guard against a binary representation just below
       * one half.
       */
      int roundedQuotient(
        const std::int64_t numerator,
        const std::int64_t denominator)
      {
        return static_cast<int>(std::nearbyint(
          static_cast<double>(numerator) / static_cast<double>(denominator)
            + 0.000001));
      }
    }

    Decimal expectedScore(const int ratingPlayer, const int ratingOpponent)
    {
      int difference{ ratingOpponent - ratingPlayer };
      difference = std::clamp(difference, -800, 800);
      const bool negative{ difference < 0 };
      int percentage{
        DIFF_TO_PD[static_cast<std::size_t>(negative ? -difference : difference)]
      };
      if (!negative)
      {
        percentage = 100 - percentage;
      }
      return Decimal::fromInt(percentage) / Decimal::fromInt(100);
    }

    Decimal sumExpectedScore(
      const int ratingPlayer,
      const std::vector<int> &ratingsOpponents)
    {
      Decimal sum{ };
      for (const int opponent : ratingsOpponents)
      {
        sum = sum + expectedScore(ratingPlayer, opponent);
      }
      return sum;
    }

    std::optional<int> averageRatingOfOpponents(
      const std::vector<int> &ratingsOpponents)
    {
      if (ratingsOpponents.empty())
      {
        return std::nullopt;
      }
      std::int64_t sum{ };
      for (const int opponent : ratingsOpponents)
      {
        sum += opponent;
      }
      return roundedQuotient(
        sum,
        static_cast<std::int64_t>(ratingsOpponents.size()));
    }

    std::optional<int> tournamentPerformanceRating(
      const Decimal &score,
      const std::vector<int> &ratingsOpponents)
    {
      if (ratingsOpponents.empty())
      {
        return std::nullopt;
      }
      const std::int64_t score100{
        (score * Decimal::fromInt(100)).roundToInteger() };
      const int percentage{
        roundedQuotient(
          score100,
          static_cast<std::int64_t>(ratingsOpponents.size())) };
      return *averageRatingOfOpponents(ratingsOpponents)
        + SCORE_TO_DP.at(static_cast<std::size_t>(percentage));
    }

    std::optional<int> perfectTournamentPerformance(
      const Decimal &score,
      const std::vector<int> &ratingsOpponents)
    {
      if (ratingsOpponents.empty())
      {
        return std::nullopt;
      }
      if (score.isZero())
      {
        return *std::min_element(
            ratingsOpponents.begin(),
            ratingsOpponents.end())
          - 800;
      }
      // An impossible score would never be reached by the search below.
      if (Decimal::fromInt(static_cast<std::int64_t>(ratingsOpponents.size()))
        < score)
      {
        return *std::max_element(
            ratingsOpponents.begin(),
            ratingsOpponents.end())
          + 800;
      }
      const int start{ *tournamentPerformanceRating(score, ratingsOpponents) };
      int step{ 16 };
      int high{ };
      int low{ };
      if (!(sumExpectedScore(start, ratingsOpponents) < score))
      {
        high = start;
        while (!(sumExpectedScore(start - step, ratingsOpponents) < score))
        {
          high = start - step;
          step *= 2;
        }
        low = start - step;
      }
      else
      {
        low = start;
        while (sumExpectedScore(start + step, ratingsOpponents) < score)
        {
          low = start + step;
          step *= 2;
        }
        high = start + step;
      }
      step = high - low;
      while (high - low > 1)
      {
        step /= 2;
        if (!(sumExpectedScore(low + step, ratingsOpponents) < score))
        {
          high -= step;
        }
        else
        {
          low += step;
        }
      }
      return high;
    }
  }
}
