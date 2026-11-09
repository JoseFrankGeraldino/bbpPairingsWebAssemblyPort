/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The reading rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#ifndef TRFREADER_H
#define TRFREADER_H

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "decimal.h"

namespace tiebreak
{
  /**
   * The input file is not a tournament the tool can compute tie-breaks for.
   */
  class FileFormatException final : public std::runtime_error
  {
  public:
    explicit FileFormatException(const std::string &message)
      : std::runtime_error(message)
    { }
  };

  /**
   * One entry of a score system: either points or the letter of another
   * result whose points it scores ("F" scores as "W").
   */
  struct ScoreEntry
  {
    bool isLetter{ };
    std::string letter{ };
    Decimal points{ };
  };

  typedef std::map<std::string, ScoreEntry> ScoreSystem;

  /** One side of a game: the player and, when recorded, the result letter. */
  struct GameSide
  {
    int cid{ };
    std::optional<std::string> result{ };
  };

  struct Game
  {
    int round{ };
    GameSide white{ };
    std::optional<GameSide> black{ };
    bool played{ };
    bool rated{ };
  };

  /** A record 299 adjustment of a player's points. */
  struct Adjustment
  {
    int round{ };
    Decimal points{ };
    bool pairing{ };
  };

  struct Competitor
  {
    int cid{ };
    int rank{ };
    std::optional<int> rating{ };
    bool present{ true };
    std::vector<Adjustment> adjust{ };
  };

  struct Tournament
  {
    std::string tournamentType{ "Tournament" };
    int numRounds{ };
    int currentRound{ };
    std::vector<std::string> rankOrder{ "PTS" };
    std::optional<std::string> startDate{ };
    std::vector<Competitor> competitors{ };
    ScoreSystem gameScore{ };
    std::vector<Game> gameList{ };
  };

  /**
   * Read a TRF16 or TRF-2026 file of an individual tournament the way
   * Gacrux's trf2json reads it.
   *
   * @throws FileFormatException if the file is malformed, inconsistent or
   *   describes something the tool does not support (team events).
   */
  Tournament readTrf(const std::string &contents);

  /**
   * Walk the score system from a result letter to its points, scoring an
   * unplayed loss as a zero.
   *
   * @throws FileFormatException if a letter has no points.
   */
  Decimal resolveScore(
    const ScoreSystem &scoreSystem,
    std::string result,
    bool played);

  /**
   * The result the other side of a game has when only one side's result is
   * recorded (chessjson.reverse), or nothing for an unknown letter.
   */
  std::optional<std::string> reverseResult(
    const std::optional<std::string> &result);
}

#endif
