/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#ifndef TIEBREAK_H
#define TIEBREAK_H

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "trfreader.h"
#include "value.h"

namespace tiebreak
{
  /** The request names a tie-break or modifier the tool cannot compute. */
  class InvalidRequestException final : public std::runtime_error
  {
  public:
    explicit InvalidRequestException(const std::string &message)
      : std::runtime_error(message)
    { }
  };

  struct Options
  {
    /** Rank order descriptors; empty means the file's record 202/212. */
    std::vector<std::string> tiebreaks{ };
    /** Compare the computed ranks with the ranks in the file. */
    bool check{ };
    /** Treat the tournament as having pre-determined pairings (-p). */
    bool preDetermined{ };
    /** Treat the tournament as a Swiss tournament (-s). */
    bool swiss{ };
    /** Rating used for unrated players (-u). */
    std::optional<int> unrated{ };
    /** Standings after this round; negative means the last round (-n). */
    int currentRound{ -1 };
  };

  struct CompetitorResult
  {
    int cid{ };
    int rank{ };
    std::vector<Value> scores{ };
  };

  struct Result
  {
    /** The descriptors as given, one per column. */
    std::vector<std::string> tiebreaks{ };
    /** Competitors in pairing-number order. */
    std::vector<CompetitorResult> competitors{ };
    /** Every computed rank equals the rank in the file. */
    bool check{ };
  };

  /**
   * Compute the tie-breaks and the resulting ranking of an individual or a
   * team tournament, as Gacrux's tiebreakchecker does.
   *
   * @throws InvalidRequestException if a descriptor cannot be computed.
   * @throws FileFormatException if the results are inconsistent.
   */
  Result computeTiebreaks(const Tournament &tournament, Options options);
}

#endif
