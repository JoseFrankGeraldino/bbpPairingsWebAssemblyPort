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
    /**
     * A match score entry "W*": the game points of the letter times the
     * team size (the game points of an unplayed match).
     */
    bool timesTeamSize{ };
  };

  typedef std::map<std::string, ScoreEntry> ScoreSystem;

  /**
   * One side of a game or match: the player or team and, when recorded, the
   * result letter. A record 299 may also set the game points of a team's
   * side of a match.
   */
  struct GameSide
  {
    int cid{ };
    std::optional<std::string> result{ };
    std::optional<Decimal> gpoints{ };
  };

  /**
   * A game of the game list or a match of the match list (a team event).
   * A match lists the ids of its board games in board order.
   */
  struct Game
  {
    int id{ };
    int round{ };
    GameSide white{ };
    std::optional<GameSide> black{ };
    bool played{ };
    bool rated{ };
    int board{ };
    std::vector<int> games{ };
  };

  /**
   * A record 299 adjustment of a competitor's points: match and game points
   * for a team, the points (in gpoints) for a player.
   */
  struct Adjustment
  {
    int round{ };
    Decimal mpoints{ };
    Decimal gpoints{ };
    bool pairing{ };
  };

  /** A player, or in a team event a team with its players (cplayers). */
  struct Competitor
  {
    int cid{ };
    int rank{ };
    std::optional<int> rating{ };
    bool present{ true };
    std::vector<Adjustment> adjust{ };
    std::vector<int> cplayers{ };
  };

  struct Tournament
  {
    std::string tournamentType{ "Tournament" };
    int numRounds{ };
    int currentRound{ };
    std::vector<std::string> rankOrder{ "PTS" };
    std::optional<std::string> startDate{ };
    /** The players, or in a team event the teams. */
    std::vector<Competitor> competitors{ };
    ScoreSystem gameScore{ };
    std::vector<Game> gameList{ };
    bool teamTournament{ };
    int teamSize{ };
    /** The primary score a team event's record 192 declares. */
    std::optional<std::string> primary{ };
    ScoreSystem matchScore{ };
    std::vector<Game> matchList{ };
  };

  /**
   * Read a TRF16 or TRF-2026 file of an individual or team tournament the
   * way Gacrux's trf2json reads it.
   *
   * @throws FileFormatException if the file is malformed, inconsistent or
   *   describes something the tool does not support.
   */
  Tournament readTrf(const std::string &contents);

  /**
   * scoresystem.get_score: the points of a score system key, where a key
   * marked "*" is the game points of a letter times the team size.
   *
   * @throws FileFormatException if a key has no points.
   */
  Decimal solveScore(
    const Tournament &tournament,
    bool match,
    const std::string &key);

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
