/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md. Function names follow gacrux/tiebreak.py so that
 * the two can be read side by side.
 */

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "decimal.h"
#include "rating.h"
#include "tiebreak.h"
#include "trfreader.h"
#include "value.h"

namespace tiebreak
{
  namespace
  {
    /**
     * Which value of a round a tie-break reads: "points" (individual),
     * "mpoints" and "gpoints" (team), or the board-weighted "tpoints" of the
     * direct encounter board criteria.
     */
    enum PointType
    {
      POINTS,
      MPOINTS,
      GPOINTS,
      TPOINTS
    };

    /** One board of a team's match (tiebreak.prepare_teamgames). */
    struct BoardGame
    {
      Decimal points{ };
      char color{ 'w' };
      bool vur{ };
      bool played{ };
      int opponent{ };
      int board{ };
    };

    /** One round of a competitor (tiebreak.py "rsts"). */
    struct RoundResult
    {
      /** rst["points"] of a player, rst["mpoints"] of a team. */
      Decimal points{ };
      std::optional<Decimal> gpoints{ };
      std::optional<Decimal> tpoints{ };
      Decimal rpoints{ 0, -1 };
      std::string res{ "Z" };
      char color{ 'w' };
      bool played{ };
      bool vur{ true };
      int opponent{ };
      std::optional<int> opprating{ };
      std::vector<int> gameIds{ };
      std::vector<BoardGame> games{ };
    };

    /** The values under one prefix of tiebreak.py "tbval". */
    struct PrefixValues
    {
      std::map<std::string, Value> vals{ };
      std::map<int, Value> pointsRound{ };
      Value pointsTop{ };
      int lmp{ };
      int lna{ };
      /** Board points, in the order the boards were first scored. */
      std::vector<std::pair<int, Decimal>> bp{ };
      Value ownscore{ };
    };

    struct Competitor
    {
      int cid{ };
      int orgrank{ };
      int rank{ 1 };
      std::optional<int> rating{ };
      std::vector<Adjustment> adjust{ };
      std::map<int, RoundResult> rsts{ };
      std::vector<Value> tiebreakScore{ };
      std::map<std::string, PrefixValues> tbval{ };
      Decimal deval{ };
      Decimal demax{ };
      int denum{ };
      bool moreloops{ };
      Decimal tbrval{ };
      Decimal bbeval{ };
    };

    struct Modifiers
    {
      bool rev{ };
      int ver{ };
      int cutlow{ };
      int cuthigh{ };
      Decimal plim{ 500, -1 };
      Decimal nlim{ 0, -1 };
      std::optional<int> unrated{ };
      bool urd{ };
      bool predetermined{ };
      bool foremode{ };
      bool swiss{ };
      bool vun{ };
      bool exchange{ };
    };

    enum Function
    {
      FUNCTION_NUL,
      FUNCTION_PTS,
      FUNCTION_BUILTIN,
      FUNCTION_MPVGP,
      FUNCTION_DE,
      FUNCTION_PS,
      FUNCTION_KS,
      FUNCTION_BH_SB,
      FUNCTION_ESB,
      FUNCTION_AOB,
      FUNCTION_RTNG,
      FUNCTION_PERFORMANCE,
      FUNCTION_APRO,
      FUNCTION_APPO,
      FUNCTION_BC,
      FUNCTION_TBR,
      FUNCTION_SSSC,
      FUNCTION_UNSUPPORTED
    };

    struct TiebreakDefinition
    {
      const char *key;
      const char *name;
      Function function;
      bool rev;
      /** Computable for an individual tournament (where Gacrux can). */
      bool individual;
    };

    /** tiebreak.py "tiebreaklist". */
    constexpr TiebreakDefinition TIEBREAK_LIST[]{
      { "NUL", "NUL", FUNCTION_NUL, false, true },
      { "PTS", "PTS", FUNCTION_PTS, true, true },
      { "MPTS", "PTS", FUNCTION_PTS, true, false },
      { "GPTS", "PTS", FUNCTION_PTS, true, false },
      { "SNO", "SNO", FUNCTION_BUILTIN, false, true },
      { "TPN", "SNO", FUNCTION_BUILTIN, false, true },
      { "RANK", "RANK", FUNCTION_BUILTIN, false, true },
      { "WIN", "WIN", FUNCTION_BUILTIN, true, true },
      { "WON", "WON", FUNCTION_BUILTIN, true, true },
      { "BPG", "BPG", FUNCTION_BUILTIN, true, true },
      { "BWG", "BWG", FUNCTION_BUILTIN, true, true },
      { "GE", "GE", FUNCTION_BUILTIN, true, true },
      { "REP", "REP", FUNCTION_BUILTIN, true, true },
      { "RIP", "RIP", FUNCTION_BUILTIN, true, true },
      { "VUR", "VUR", FUNCTION_BUILTIN, true, true },
      { "NUM", "NUM", FUNCTION_BUILTIN, true, true },
      { "COD", "COD", FUNCTION_BUILTIN, true, true },
      { "RTG", "RTG", FUNCTION_BUILTIN, true, true },
      { "MPVGP", "MPVGP", FUNCTION_MPVGP, true, true },
      { "DE", "DE", FUNCTION_DE, false, true },
      { "EDE", "EDE", FUNCTION_DE, false, true },
      { "EDEC", "EDEC", FUNCTION_DE, false, false },
      { "EDET", "EDET", FUNCTION_DE, false, true },
      { "EDEB", "EDEB", FUNCTION_DE, false, true },
      { "EDEBT", "EDEBT", FUNCTION_DE, false, false },
      { "EDEBB", "EDEBB", FUNCTION_DE, false, false },
      { "PS", "PS", FUNCTION_PS, true, true },
      { "KS", "KS", FUNCTION_KS, true, true },
      { "BH", "BH", FUNCTION_BH_SB, true, true },
      { "FB", "FB", FUNCTION_BH_SB, true, true },
      { "SB", "SB", FUNCTION_BH_SB, true, true },
      { "ABH", "ABH", FUNCTION_BH_SB, true, true },
      { "AFB", "AFB", FUNCTION_BH_SB, true, true },
      { "AOB", "AOB", FUNCTION_AOB, true, true },
      { "RTNG", "RTNG", FUNCTION_RTNG, true, true },
      { "ARO", "ARO", FUNCTION_PERFORMANCE, true, true },
      { "TPR", "TPR", FUNCTION_PERFORMANCE, true, true },
      { "PTP", "PTP", FUNCTION_PERFORMANCE, true, true },
      { "APRO", "APRO", FUNCTION_APRO, true, true },
      { "APPO", "APPO", FUNCTION_APPO, true, true },
      { "ESB", "ESB", FUNCTION_ESB, true, true },
      { "EMMSB", "EMMSB", FUNCTION_ESB, true, false },
      { "EMGSB", "EMGSB", FUNCTION_ESB, true, false },
      { "EGMSB", "EGMSB", FUNCTION_ESB, true, false },
      { "EGGSB", "EGGSB", FUNCTION_ESB, true, false },
      { "BC", "BC", FUNCTION_BC, false, false },
      { "TBR", "TBR", FUNCTION_TBR, false, false },
      { "BBE", "BBE", FUNCTION_TBR, false, false },
      { "SSSC", "SSSC", FUNCTION_SSSC, true, false },
      // Known to the reference implementation but not computed here:
      // helpers that are not FIDE tie-breaks.
      { "CSQ", "CSQ", FUNCTION_UNSUPPORTED, true, false },
      { "RSES", "RSES", FUNCTION_UNSUPPORTED, true, false },
      { "STD", "STD", FUNCTION_UNSUPPORTED, true, false },
      { "ACC", "ACC", FUNCTION_UNSUPPORTED, true, false },
      { "FLT", "FLT", FUNCTION_UNSUPPORTED, true, false },
      { "FLTD", "FLTD", FUNCTION_UNSUPPORTED, true, false },
      { "FLTFT", "FLTFT", FUNCTION_UNSUPPORTED, true, false },
      { "RFP", "RFP", FUNCTION_UNSUPPORTED, true, false },
      { "TOP", "TOP", FUNCTION_UNSUPPORTED, true, false },
      { "RND", "RND", FUNCTION_UNSUPPORTED, false, false } };

    /** The tie-break of a name, or nothing for a name the list lacks. */
    const TiebreakDefinition *findTiebreak(const std::string &name)
    {
      for (const TiebreakDefinition &definition : TIEBREAK_LIST)
      {
        if (name == definition.key)
        {
          return &definition;
        }
      }
      return nullptr;
    }

    struct Spec
    {
      std::string name{ };
      /** "points", "mpoints", "gpoints" or a pair such as "mgpoints". */
      std::string pointtype{ };
      Modifiers modifiers{ };
      // The direct encounter loop state (tiebreak.py tb["ede"]).
      std::string functions{ };
      int loopcount{ };
      std::vector<bool> edechanges{ };
      std::size_t swap{ };
      int changes{ };
    };

    /** tiebreak.get_scoreinfo. */
    struct ScoreInfo
    {
      bool match;
      PointType points;
      const ScoreSystem *scoretype;
      std::string prefix;
    };

    std::string lowerCase(std::string text)
    {
      for (char &character : text)
      {
        character = static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
      }
      return text;
    }

    std::string upperCase(std::string text)
    {
      for (char &character : text)
      {
        character = static_cast<char>(
          std::toupper(static_cast<unsigned char>(character)));
      }
      return text;
    }

    /** Python's str.isdigit for ASCII text: non-empty and all digits. */
    bool isDigits(const std::string &text)
    {
      return !text.empty()
        && std::all_of(text.begin(), text.end(), [](const char character)
          {
            return character >= '0' && character <= '9';
          });
    }

    /**
     * Python's int() of a modifier argument.
     *
     * @throws InvalidRequestException if it is not an integer.
     */
    int modifierInt(const std::string &text, const std::string &descriptor)
    {
      std::string digits{ text };
      bool negative{ };
      if (!digits.empty() && (digits[0] == '+' || digits[0] == '-'))
      {
        negative = digits[0] == '-';
        digits.erase(0, 1);
      }
      if (!isDigits(digits) || digits.size() > 9)
      {
        throw InvalidRequestException(
          "Invalid tie-break descriptor: " + descriptor);
      }
      const int value{ std::stoi(digits) };
      return negative ? -value : value;
    }

    std::vector<std::string> split(const std::string &text, const char by)
    {
      std::vector<std::string> fields{ };
      std::size_t begin{ };
      while (true)
      {
        const std::size_t end{ text.find(by, begin) };
        if (end == std::string::npos)
        {
          fields.push_back(text.substr(begin));
          return fields;
        }
        fields.push_back(text.substr(begin, end - begin));
        begin = end + 1;
      }
    }

    /** math.floor of a Decimal. */
    std::int64_t floorDecimal(const Decimal &value)
    {
      std::int64_t result{ value.coefficient };
      if (value.exponent >= 0)
      {
        for (int digit{ }; digit < value.exponent; ++digit)
        {
          result *= 10;
        }
        return result;
      }
      std::int64_t divisor{ 1 };
      for (int digit{ }; digit < -value.exponent; ++digit)
      {
        divisor *= 10;
      }
      result = value.coefficient / divisor;
      if (value.coefficient % divisor != 0 && value.coefficient < 0)
      {
        --result;
      }
      return result;
    }

    /** A score system entry that is a number (scoretype["W"]). */
    Decimal scoreValue(const ScoreSystem &scoreSystem, const std::string &key)
    {
      const auto found = scoreSystem.find(key);
      if (found == scoreSystem.end())
      {
        throw std::out_of_range("KeyError: score " + key);
      }
      if (found->second.isLetter)
      {
        throw std::logic_error("TypeError: score " + key + " is a letter");
      }
      return found->second.points;
    }

    /**
     * gamescore[res] followed through its letters, an unplayed loss not
     * turned into a zero (tiebreak.compute_score).
     */
    Decimal walkScore(const ScoreSystem &scoreSystem, std::string key)
    {
      for (std::size_t step{ }; step <= scoreSystem.size(); ++step)
      {
        const auto found = scoreSystem.find(key);
        if (found == scoreSystem.end())
        {
          throw std::out_of_range("KeyError: score " + key);
        }
        if (!found->second.isLetter)
        {
          return found->second.points;
        }
        key = found->second.letter;
      }
      throw std::logic_error("RecursionError: score " + key);
    }

    std::optional<Decimal> pointsOf(
      const RoundResult &rst,
      const PointType type)
    {
      switch (type)
      {
      case GPOINTS:
        return rst.gpoints;
      case TPOINTS:
        return rst.tpoints;
      case POINTS:
      case MPOINTS:
      default:
        return rst.points;
      }
    }

    /** rst[points], which the reference reads without a default. */
    Decimal requirePoints(const RoundResult &rst, const PointType type)
    {
      const std::optional<Decimal> points{ pointsOf(rst, type) };
      if (!points)
      {
        throw std::out_of_range("KeyError: round points");
      }
      return *points;
    }

    class Engine
    {
    public:
      Engine(const Tournament &tournament, const Options &options);

      Result computeTiebreaks(Options options);

    private:
      const Tournament &tournament;
      std::vector<Competitor> cmps{ };
      std::map<int, std::size_t> index{ };
      std::vector<std::size_t> rankorder{ };
      int rounds{ };
      int currentround{ };
      bool rr{ };
      bool isteam{ };
      int teamsize{ };
      int maxboard{ };
      std::optional<int> unrated{ };
      int rulesversion{ 2 };
      std::optional<std::string> primaryscore{ };
      ScoreSystem matchscore{ };
      ScoreSystem gamescore{ };
      ScoreSystem ratingScore{ };
      std::map<int, int> cteam{ };
      std::map<int, const Game *> cgames{ };

      Competitor &byCid(const int cid)
      {
        const auto found = index.find(cid);
        if (found == index.end())
        {
          throw std::out_of_range(
            "KeyError: competitor " + std::to_string(cid));
        }
        return cmps[found->second];
      }

      static PrefixValues &values(Competitor &cmp, const std::string &prefix)
      {
        const auto found = cmp.tbval.find(prefix);
        if (found == cmp.tbval.end())
        {
          throw std::out_of_range("KeyError: " + prefix);
        }
        return found->second;
      }

      static Value &val(
        Competitor &cmp,
        const std::string &prefix,
        const std::string &name)
      {
        PrefixValues &prefixValues{ values(cmp, prefix) };
        const auto found = prefixValues.vals.find(name);
        if (found == prefixValues.vals.end())
        {
          throw std::out_of_range("KeyError: " + prefix + name);
        }
        return found->second;
      }

      int teamOf(const int player) const
      {
        const auto found = cteam.find(player);
        if (found == cteam.end())
        {
          throw std::out_of_range(
            "KeyError: player " + std::to_string(player) + " has no team");
        }
        return found->second;
      }

      Decimal zero(bool match) const;
      ScoreInfo getScoreinfo(const Spec &tb, bool primary) const;
      void setPrimaryscore(const std::string &scorename);
      Decimal getScore(
        const ScoreSystem &scoreSystem,
        const Game &game,
        bool white) const;
      bool isVur(const Game &game, bool white) const;
      void prepareCompetitors();
      void prepareResult(const Game &game);
      void prepareTeamgames(const Game &match);
      void computeScore(
        bool match,
        PointType pointtype,
        const std::string &prefix);
      void findTmversion();
      Spec parseTiebreak(const std::string &text);
      void computeSingleTiebreak(Spec &tb);

      std::string computePts(const Spec &tb);
      std::string reversePointtype(Spec &tb);
      std::string computeRecursiveIfTied(Spec &tb, bool topBottom);
      bool singleRunExtDirectEncounter(
        Spec &tb,
        const std::vector<std::size_t> &subro,
        int loopcount);
      int basicDirectEncounter(
        Spec &tb,
        std::vector<std::size_t> subro,
        PointType points,
        const ScoreSystem &scoretype);
      bool singleRunTopBottomBoardResult(
        const Spec &tb,
        const std::vector<std::size_t> &ro,
        int loopcount);
      std::string computeProgressiveScore(const Spec &tb);
      std::string computeKoya(const Spec &tb);
      std::string computeBuchholzSonnebornBerger(const Spec &tb);
      std::string computeExtSonnebornBerger(Spec &tb);
      std::string computeRtng(const Spec &tb);
      std::string computeRatingPerformance(const Spec &tb);
      std::string computeAverage(
        const Spec &tb,
        const std::string &name,
        int exponent);
      std::string computeBoardcount(const Spec &tb);
      std::string computeScoreStrengthCombination(const Spec &tb);
    };

    Engine::Engine(const Tournament &tournament_, const Options &options)
      : tournament(tournament_)
    {
      std::optional<bool> isRr{ };
      if (options.preDetermined)
      {
        isRr = true;
      }
      if (options.swiss)
      {
        isRr = false;
      }
      isteam = tournament.teamTournament;
      teamsize = tournament.teamSize;
      rounds = std::max(tournament.numRounds, tournament.currentRound);
      currentround = options.currentRound >= 0 ? options.currentRound : rounds;
      if (isteam && tournament.primary)
      {
        setPrimaryscore(*tournament.primary);
      }
      // Every result letter resolves to a number for the rating values.
      ratingScore["W"] = { false, "", Decimal(10, -1), false };
      ratingScore["D"] = { false, "", Decimal(5, -1), false };
      ratingScore["Z"] = { false, "", Decimal(0, -1), false };
      for (const char *const letter : { "L", "A", "U", "F", "H", "P" })
      {
        ratingScore[letter] = { true, "Z", Decimal{ }, false };
      }
      gamescore = tournament.gameScore;
      if (isteam)
      {
        matchscore = tournament.matchScore;
        // chessjson.build_tournament_teamcompetitors
        cteam[0] = 0;
        for (const tiebreak::Competitor &team : tournament.competitors)
        {
          for (const int player : team.cplayers)
          {
            cteam[player] = team.cid;
          }
        }
        for (const Game &game : tournament.gameList)
        {
          cgames[game.id] = &game;
        }
        prepareCompetitors();
        computeScore(true, MPOINTS, "mpoints_");
        computeScore(false, GPOINTS, "gpoints_");
      }
      else
      {
        matchscore = gamescore;
        prepareCompetitors();
        computeScore(false, POINTS, "points_");
      }
      for (std::size_t position{ }; position < cmps.size(); ++position)
      {
        rankorder.push_back(position);
      }

      const std::string type{ upperCase(tournament.tournamentType) };
      const int numcomp{ static_cast<int>(cmps.size()) };
      if (isRr)
      {
        rr = *isRr;
      }
      else if (type.find("SWISS") != std::string::npos)
      {
        rr = false;
      }
      else if (type.find("RR") != std::string::npos
        || type.find("ROBIN") != std::string::npos
        || type.find("BERGER") != std::string::npos)
      {
        rr = true;
      }
      else
      {
        rr = numcomp == rounds + 1 || numcomp == rounds
          || numcomp * 2 == rounds + 1 || numcomp * 2 == rounds;
      }
      unrated = options.unrated;
    }

    Decimal Engine::zero(const bool match) const
    {
      return scoreValue(match ? matchscore : gamescore, "Z");
    }

    /**
     * tiebreak.get_scoreinfo: the primary (first letter of the point type)
     * or the secondary score (the second letter, or the other team score).
     */
    ScoreInfo Engine::getScoreinfo(const Spec &tb, bool primary) const
    {
      if (tb.modifiers.exchange)
      {
        primary = !primary;
      }
      const std::string &pointtype{ tb.pointtype };
      char key{ pointtype.at(primary ? 0 : 1) };
      if (!primary && key != 'g' && key != 'm')
      {
        key = pointtype.at(0);
        if (key == 'g')
        {
          key = 'm';
        }
        else if (key == 'm')
        {
          key = 'g';
        }
      }
      if (key == 'g')
      {
        return { false, GPOINTS, &gamescore, "gpoints_" };
      }
      if (key == 'm')
      {
        return { true, MPOINTS, &matchscore, "mpoints_" };
      }
      return { false, POINTS, &gamescore, "points_" };
    }

    void Engine::setPrimaryscore(const std::string &scorename)
    {
      if (primaryscore)
      {
        return;
      }
      const std::string name{ lowerCase(scorename) };
      if (name == "mp" || name == "match")
      {
        primaryscore = "mpoints";
      }
      else if (name == "gp" || name == "game")
      {
        primaryscore = "gpoints";
      }
      else
      {
        primaryscore = "points";
      }
    }

    /** tiebreak.get_score: one side's points under a score system. */
    Decimal Engine::getScore(
      const ScoreSystem &scoreSystem,
      const Game &game,
      const bool white) const
    {
      const GameSide &side{ white ? game.white : *game.black };
      std::optional<std::string> res{ side.result };
      if (!res && side.cid > 0)
      {
        if (white)
        {
          res = reverseResult(
            game.black ? game.black->result : std::nullopt);
        }
        else
        {
          res = reverseResult(game.white.result);
        }
      }
      if (!res)
      {
        return Decimal(0, -1);
      }
      return resolveScore(scoreSystem, *res, game.played);
    }

    /** tiebreak.is_vur: an unplayed round that is not a win. */
    bool Engine::isVur(const Game &game, const bool white) const
    {
      if (game.played)
      {
        return false;
      }
      const GameSide &side{ white ? game.white : *game.black };
      std::optional<std::string> res{ side.result };
      if (!res && side.cid > 0)
      {
        res = reverseResult(
          white
            ? (game.black ? game.black->result : std::nullopt)
            : game.white.result);
      }
      return !res || *res != "W";
    }

    /**
     * tiebreak.prepare_competitors: every round starts as an unplayed zero,
     * then the games (or the matches and their board games) fill it in.
     */
    void Engine::prepareCompetitors()
    {
      for (const tiebreak::Competitor &competitor : tournament.competitors)
      {
        Competitor cmp{ };
        cmp.cid = competitor.cid;
        cmp.orgrank = competitor.rank;
        cmp.rating = competitor.rating;
        cmp.adjust = competitor.adjust;
        for (int round{ 1 }; round <= currentround; ++round)
        {
          RoundResult result{ };
          result.points = zero(isteam);
          cmp.rsts[round] = result;
        }
        index[cmp.cid] = cmps.size();
        cmps.push_back(cmp);
      }
      for (const Game &game :
        isteam ? tournament.matchList : tournament.gameList)
      {
        prepareResult(game);
        if (isteam)
        {
          prepareTeamgames(game);
        }
      }
    }

    void Engine::prepareResult(const Game &game)
    {
      const ScoreSystem &scoresystem{ isteam ? matchscore : gamescore };
      const int round{ game.round };
      const int white{ game.white.cid };
      const int black{ game.black ? game.black->cid : 0 };
      RoundResult whiteResult{ };
      whiteResult.points = getScore(scoresystem, game, true);
      whiteResult.rpoints = getScore(ratingScore, game, true);
      whiteResult.vur = isVur(game, true);
      whiteResult.res = game.white.result.value_or("Z");
      whiteResult.color = 'w';
      whiteResult.played = game.played;
      whiteResult.opponent = black;
      whiteResult.gameIds = game.games;
      std::optional<int> whiteRating{ };
      std::optional<int> blackRating{ };
      RoundResult blackResult{ };
      if (black > 0)
      {
        if (!game.black->result)
        {
          throw FileFormatException(
            "No result for black in round " + std::to_string(round)
              + ", white=" + std::to_string(white) + ", black="
              + std::to_string(black));
        }
        blackResult.points = getScore(scoresystem, game, false);
        blackResult.rpoints = getScore(ratingScore, game, false);
        blackResult.vur = isVur(game, false);
        blackResult.res = *game.black->result;
        blackResult.color = 'b';
        blackResult.played = game.played;
        blackResult.opponent = white;
        blackResult.gameIds = game.games;
        if (game.played)
        {
          if (index.count(white))
          {
            whiteRating = byCid(white).rating;
          }
          if (index.count(black))
          {
            blackRating = byCid(black).rating;
          }
        }
      }
      if (white > 0)
      {
        whiteResult.opprating = blackRating;
        byCid(white).rsts[round] = whiteResult;
      }
      if (black > 0)
      {
        blackResult.opprating = whiteRating;
        byCid(black).rsts[round] = blackResult;
      }
    }

    /**
     * tiebreak.prepare_teamgames: a team's game points of a match and its
     * boards; an unplayed match scores the game points the match score
     * system gives it.
     */
    void Engine::prepareTeamgames(const Game &match)
    {
      const int round{ match.round };
      const int matchBlack{ match.black ? match.black->cid : 0 };
      for (const bool whiteSide : { true, false })
      {
        const std::optional<GameSide> side{
          whiteSide ? std::optional<GameSide>(match.white) : match.black };
        const int competitor{ side ? side->cid : 0 };
        if (competitor <= 0)
        {
          continue;
        }
        RoundResult &tmatch{ byCid(competitor).rsts[round] };
        if (!tmatch.gameIds.empty() && matchBlack > 0)
        {
          Decimal gpoints{ 0, 0 };
          std::vector<BoardGame> games{ };
          for (const int id : tmatch.gameIds)
          {
            const auto found = cgames.find(id);
            if (found == cgames.end())
            {
              throw std::out_of_range("KeyError: game " + std::to_string(id));
            }
            const Game &game{ *found->second };
            const int white{ game.white.cid };
            const int black{ game.black ? game.black->cid : 0 };
            const int board{ game.board };
            maxboard = std::max(maxboard, board);
            const bool wVur{ isVur(game, true) };
            if (teamOf(white) == competitor && board > 0)
            {
              const Decimal points{ getScore(gamescore, game, true) };
              gpoints = gpoints + points;
              games.push_back({ points, 'w', wVur, game.played, black, board });
            }
            if (black > 0 && board > 0 && teamOf(black) == competitor)
            {
              const Decimal points{ getScore(gamescore, game, false) };
              const bool bVur{ isVur(game, false) };
              gpoints = gpoints + points;
              games.push_back({ points, 'b', bVur, game.played, white, board });
            }
          }
          tmatch.gpoints = gpoints;
          if (side->gpoints)
          {
            tmatch.gpoints = side->gpoints;
          }
          tmatch.games = games;
        }
        else
        {
          if (tmatch.opponent == 0 && tmatch.played)
          {
            tmatch.gpoints = solveScore(tournament, true, "PG");
          }
          else
          {
            static const std::map<std::string, std::string> trans{
              { "W", "F" }, { "D", "H" }, { "L", "Z" } };
            const auto found = trans.find(tmatch.res);
            const std::string res{
              found != trans.end() ? found->second : tmatch.res };
            tmatch.gpoints = solveScore(tournament, true, res + "G");
          }
          tmatch.games.clear();
        }
      }
    }

    /**
     * tiebreak.compute_score: the totals and counts of one score (points,
     * match points or game points) for every competitor. A team's game
     * points count board by board.
     */
    void Engine::computeScore(
      const bool match,
      const PointType pointtype,
      const std::string &prefix)
    {
      const ScoreSystem &scoretype{ match ? matchscore : gamescore };
      const Value win{ Value::fromDecimal(scoreValue(scoretype, "W")) };
      const bool boards{ isteam && !match };
      for (Competitor &cmp : cmps)
      {
        PrefixValues &tbscore{ cmp.tbval[prefix] };
        std::map<std::string, Value> &vals{ tbscore.vals };
        vals["nul"] = Value::fromInt(0);
        vals["sno"] = Value::fromInt(cmp.cid);
        vals["rank"] = Value::fromInt(cmp.orgrank);
        vals["rtg"] =
          cmp.rating ? Value::fromInt(*cmp.rating) : Value::none();
        Value points{ Value::fromDecimal(zero(match)) };
        int wins{ };
        int won{ };
        int bpg{ };
        int bwg{ };
        int ge{ };
        int rip{ };
        int vur{ };
        int cod{ };
        int num{ };
        for (int round{ 1 }; round <= currentround; ++round)
        {
          const auto found = cmp.rsts.find(round);
          if (found == cmp.rsts.end())
          {
            continue;
          }
          const RoundResult &rst{ found->second };
          const std::optional<Decimal> roundPoints{ pointsOf(rst, pointtype) };
          const Value value{
            roundPoints ? Value::fromDecimal(*roundPoints) : Value::fromInt(0) };
          tbscore.pointsRound[round] = value;
          points = points + value;
          std::vector<BoardGame> gamelist{ };
          if (!boards)
          {
            gamelist.push_back(
              { Decimal{ }, rst.color, rst.vur, rst.played, rst.opponent, 0 });
          }
          else if (rst.played && rst.opponent > 0)
          {
            gamelist = rst.games;
          }
          else
          {
            const Decimal boardPoints{ walkScore(gamescore, rst.res) };
            for (int board{ }; board < teamsize; ++board)
            {
              gamelist.push_back(
                { boardPoints, 'w', rst.vur, rst.played, 0, board + 1 });
            }
          }
          for (const BoardGame &game : gamelist)
          {
            Value gamePoints{ value };
            if (boards)
            {
              Decimal boardPoints{ game.points };
              if (game.played && game.opponent <= 0)
              {
                // A pairing-allocated bye scores a win on every board.
                boardPoints = scoreValue(gamescore, "W");
              }
              gamePoints = Value::fromDecimal(boardPoints);
              const auto board = std::find_if(
                tbscore.bp.begin(),
                tbscore.bp.end(),
                [&game](const std::pair<int, Decimal> &entry)
                {
                  return entry.first == game.board;
                });
              if (board == tbscore.bp.end())
              {
                tbscore.bp.emplace_back(game.board, boardPoints);
              }
              else
              {
                board->second = board->second + boardPoints;
              }
            }
            const bool isWin{ gamePoints.equals(win) };
            wins += isWin;
            won += isWin && game.played && game.opponent > 0;
            bpg += game.color == 'b' && game.played;
            bwg += game.color == 'b' && game.played && isWin;
            ge += game.played || isWin;
            rip += game.played || game.opponent > 0;
            vur += game.vur;
            // Last round not absent, and last round paired or played (C.07
            // 16.2): Fore Buchholz and the unplayed-round rules read them.
            if (round > tbscore.lna && !game.vur)
            {
              tbscore.lna = round;
            }
            if (round > tbscore.lmp && game.opponent > 0)
            {
              tbscore.lmp = round;
            }
          }
          if (rst.played && rst.opponent > 0)
          {
            cod += rst.color == 'w' ? 1 : -1;
          }
          if (rst.played)
          {
            num += rst.opponent > 0;
            if (round > tbscore.lmp)
            {
              tbscore.lmp = round;
            }
          }
        }
        Value adj{ Value::fromInt(0) };
        Value top{ Value::fromInt(0) };
        for (const Adjustment &adjustment : cmp.adjust)
        {
          const Decimal amount{
            pointtype == MPOINTS ? adjustment.mpoints : adjustment.gpoints };
          if (adjustment.pairing && adjustment.round <= currentround)
          {
            adj = adj + Value::fromDecimal(amount);
          }
          if (!adjustment.pairing)
          {
            top = top + Value::fromDecimal(amount);
          }
        }
        tbscore.pointsTop = top;
        vals["points"] = points + adj;
        vals["win"] = Value::fromInt(wins);
        vals["won"] = Value::fromInt(won);
        vals["bpg"] = Value::fromInt(bpg);
        vals["bwg"] = Value::fromInt(bwg);
        vals["ge"] = Value::fromInt(ge);
        vals["rep"] = Value::fromInt(ge);
        vals["rip"] = Value::fromInt(rip);
        vals["vur"] = Value::fromInt(vur);
        vals["cod"] = Value::fromInt(cod);
        vals["num"] = Value::fromInt(num);
      }
    }

    /**
     * tiebreak.find_tmversion: tournaments that started before 2026-03-01
     * are scored under the 2024 rules; an undated one under the newest.
     */
    void Engine::findTmversion()
    {
      rulesversion = 2;
      if (!tournament.startDate)
      {
        return;
      }
      const std::string date{ tournament.startDate->substr(0, 10) };
      const std::vector<std::string> parts{ split(date, '-') };
      if (parts.size() != 3 || parts[0].size() != 4 || !isDigits(parts[0])
        || parts[1].empty() || parts[1].size() > 2 || !isDigits(parts[1])
        || parts[2].empty() || parts[2].size() > 2 || !isDigits(parts[2]))
      {
        return;
      }
      const int year{ std::stoi(parts[0]) };
      const int month{ std::stoi(parts[1]) };
      const int day{ std::stoi(parts[2]) };
      const bool leap{ (year % 4 == 0 && year % 100 != 0) || year % 400 == 0 };
      const int daysInMonth[]{
        31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
      if (year < 1 || month < 1 || month > 12 || day < 1
        || day > daysInMonth[month - 1])
      {
        return;
      }
      if (year < 2026 || (year == 2026 && month < 3))
      {
        rulesversion = 1;
      }
    }

    /**
     * tiebreak.parse_tiebreak.
     *
     * @throws InvalidRequestException for a descriptor the tool cannot
     *   compute.
     */
    Spec Engine::parseTiebreak(const std::string &text)
    {
      static const std::map<std::string, std::string> pointtrans{
        { "MP", "mpoints" }, { "GP", "gpoints" }, { "MM", "mmpoints" },
        { "MG", "mgpoints" }, { "GM", "gmpoints" }, { "GG", "ggpoints" } };
      std::string txt{ upperCase(text) };
      std::replace(txt.begin(), txt.end(), '!', '/');
      std::replace(txt.begin(), txt.end(), '#', '/');
      const std::vector<std::string> comp{ split(txt, '/') };
      const std::vector<std::string> nameparts{ split(comp[0], ':') };
      std::vector<std::string> nameyear{ split(nameparts[0], '@') };
      nameyear.push_back("24");
      modifierInt(nameyear[1], text);
      Spec tb{ };
      tb.name = nameyear[0];
      tb.modifiers.ver = rulesversion;
      const TiebreakDefinition *const definition{ findTiebreak(tb.name) };
      const bool known{ definition != nullptr };
      // A point system suffix (":MP") names team scores, which an
      // individual tournament does not have.
      if (!isteam
        && (nameparts.size() >= 2
          || (known && !definition->individual)))
      {
        throw InvalidRequestException(
          "Tie-break not supported for individual tournaments: " + text);
      }
      if (known && definition->function == FUNCTION_UNSUPPORTED)
      {
        throw InvalidRequestException("Tie-break not supported: " + text);
      }
      if (primaryscore)
      {
        tb.pointtype = *primaryscore;
      }
      else
      {
        tb.pointtype = isteam ? "mpoints" : "points";
      }
      if (tb.name == "MPTS")
      {
        tb.pointtype = "mpoints";
      }
      if (tb.name == "GPTS")
      {
        tb.pointtype = "gpoints";
      }
      if (nameparts.size() == 2)
      {
        const auto found = pointtrans.find(nameparts[1]);
        if (found == pointtrans.end())
        {
          throw InvalidRequestException(
            "Invalid tie-break descriptor: " + text);
        }
        tb.pointtype = found->second;
      }
      if (!primaryscore
        && (tb.name == "PTS" || tb.name == "MPTS" || tb.name == "GPTS"))
      {
        primaryscore = tb.pointtype;
      }
      for (std::size_t part{ 1 }; part < comp.size(); ++part)
      {
        const std::string &mf{ comp[part] };
        const std::string argument{ mf.size() > 1 ? mf.substr(1) : "" };
        for (const char flag : mf)
        {
          if (flag == 'C' && isDigits(argument))
          {
            tb.modifiers.cutlow = modifierInt(argument, text);
          }
          else if (flag == 'M' && isDigits(argument))
          {
            tb.modifiers.cutlow = modifierInt(argument, text);
            tb.modifiers.cuthigh = modifierInt(argument, text);
          }
          else if (flag == 'L')
          {
            if (mf.size() < 2)
            {
              throw InvalidRequestException(
                "Invalid tie-break descriptor: " + text);
            }
            const Decimal scale{
              mf.find('.') != std::string::npos ? Decimal(10, -1)
                : Decimal(5, -1) };
            std::string numbers{ mf };
            numbers.erase(
              std::remove(numbers.begin(), numbers.end(), '.'),
              numbers.end());
            const std::string signedPart{
              numbers.size() > 2 ? numbers.substr(2) : "" };
            if (isDigits(argument))
            {
              tb.modifiers.plim = Decimal::parse(argument);
            }
            else if (mf[1] == '+' && isDigits(signedPart))
            {
              tb.modifiers.nlim = Decimal::parse(mf.substr(2)) * scale;
            }
            else if (mf[1] == '-' && isDigits(signedPart))
            {
              tb.modifiers.nlim = -(Decimal::parse(mf.substr(2)) * scale);
            }
          }
          else if (flag == 'U')
          {
            tb.modifiers.unrated = modifierInt(argument, text);
          }
          else if (flag == 'D')
          {
            tb.modifiers.urd = true;
          }
          else if (flag == 'P')
          {
            tb.modifiers.predetermined = true;
          }
          else if (flag == 'F')
          {
            tb.modifiers.foremode = true;
          }
          else if (flag == 'R')
          {
            tb.modifiers.rev = true;
          }
          else if (flag == 'S')
          {
            tb.modifiers.swiss = true;
          }
          else if (flag == 'X')
          {
            tb.modifiers.exchange = true;
          }
          else if (flag == 'K' && isDigits(argument))
          {
            // The SSSC factor; it shares its field with the Koya limit.
            tb.modifiers.nlim = Decimal::parse(argument);
          }
          else if (flag == 'V')
          {
            int ver{ argument.empty() ? 2 : modifierInt(argument, text) };
            if (ver > 2000)
            {
              ver = ver == 2022 ? 0 : ver == 2024 ? 1 : 2;
            }
            else
            {
              ver = ver == 0 || ver == 1 ? ver : 2;
            }
            tb.modifiers.ver = ver;
          }
          else if (flag == 'N')
          {
            tb.modifiers.vun = true;
          }
        }
      }
      if (rr && !tb.modifiers.swiss)
      {
        // A round robin treats unplayed games as played by default.
        tb.modifiers.predetermined = true;
      }
      return tb;
    }

    void Engine::computeSingleTiebreak(Spec &tb)
    {
      const TiebreakDefinition *const found{ findTiebreak(tb.name) };
      const TiebreakDefinition definition{
        found ? *found : *findTiebreak("NUL") };
      tb.modifiers.rev = tb.modifiers.rev != definition.rev;
      std::string key{ };
      switch (definition.function)
      {
      case FUNCTION_NUL:
        key = "nul";
        break;
      case FUNCTION_PTS:
        key = computePts(tb);
        break;
      case FUNCTION_BUILTIN:
        key = lowerCase(definition.name);
        break;
      case FUNCTION_MPVGP:
        key = reversePointtype(tb);
        break;
      case FUNCTION_DE:
        if (tb.name == "EDEC" || tb.name == "EDEBT" || tb.name == "EDEBB")
        {
          computeBoardcount(tb);
        }
        key = computeRecursiveIfTied(tb, false);
        break;
      case FUNCTION_PS:
        key = computeProgressiveScore(tb);
        break;
      case FUNCTION_KS:
        key = computeKoya(tb);
        break;
      case FUNCTION_BH_SB:
        key = computeBuchholzSonnebornBerger(tb);
        break;
      case FUNCTION_ESB:
        key = computeExtSonnebornBerger(tb);
        break;
      case FUNCTION_AOB:
      {
        // Two decimals hold the average for 8 rounds, three for 26 and so
        // on (tiebreak.compute_average_of_buchholz).
        const int exponent{
          currentround < 9 ? -2 : currentround < 27 ? -3
            : currentround < 73 ? -4 : -5 };
        computeBuchholzSonnebornBerger(tb);
        key = computeAverage(tb, "bh", exponent);
        break;
      }
      case FUNCTION_RTNG:
        key = computeRtng(tb);
        break;
      case FUNCTION_PERFORMANCE:
        key = computeRatingPerformance(tb);
        break;
      case FUNCTION_APRO:
        computeRatingPerformance(tb);
        key = computeAverage(tb, "tpr", 0);
        break;
      case FUNCTION_APPO:
        computeRatingPerformance(tb);
        key = computeAverage(tb, "ptp", 0);
        break;
      case FUNCTION_BC:
        key = computeBoardcount(tb);
        break;
      case FUNCTION_TBR:
        key = computeRecursiveIfTied(tb, true);
        break;
      case FUNCTION_SSSC:
        key = computeScoreStrengthCombination(tb);
        break;
      default:
        throw InvalidRequestException("Tie-break not supported: " + tb.name);
      }
      // tiebreak.addval
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      for (Competitor &cmp : cmps)
      {
        cmp.tiebreakScore.push_back(val(cmp, prefix, key));
      }
      const std::size_t position{ cmps.front().tiebreakScore.size() - 1 };
      const bool rev{ !tb.modifiers.rev };
      std::sort(
        rankorder.begin(),
        rankorder.end(),
        [this, position, rev](const std::size_t left, const std::size_t right)
        {
          const Competitor &a{ cmps[left] };
          const Competitor &b{ cmps[right] };
          if (a.rank != b.rank)
          {
            return a.rank < b.rank;
          }
          const Value &aValue{ a.tiebreakScore[position] };
          const Value &bValue{ b.tiebreakScore[position] };
          const bool aNone{ aValue.isNone() != rev };
          const bool bNone{ bValue.isNone() != rev };
          if (aNone != bNone)
          {
            return aNone < bNone;
          }
          const int comparison{
            aValue.isNone() || bValue.isNone() ? 0
              : rev ? aValue.compare(bValue) : bValue.compare(aValue) };
          if (comparison != 0)
          {
            return comparison < 0;
          }
          return a.cid < b.cid;
        });
      int rank{ 1 };
      Value previous{ cmps[rankorder[0]].tiebreakScore[position] };
      for (std::size_t i{ 1 }; i < rankorder.size(); ++i)
      {
        ++rank;
        Competitor &cmp{ cmps[rankorder[i]] };
        const Value &score{ cmp.tiebreakScore[position] };
        if (cmp.rank == rank || !score.equals(previous))
        {
          cmp.rank = rank;
          previous = score;
        }
        else
        {
          cmp.rank = cmps[rankorder[i - 1]].rank;
        }
      }
    }

    /**
     * tiebreak.compute_pts: points plus the adjustments outside pairing,
     * computed once per score (the reference looks at competitor 1).
     */
    std::string Engine::computePts(const Spec &tb)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      if (values(byCid(1), prefix).vals.count("pts"))
      {
        return "pts";
      }
      for (Competitor &cmp : cmps)
      {
        PrefixValues &tbscore{ values(cmp, prefix) };
        tbscore.vals["pts"] = tbscore.vals.at("points") + tbscore.pointsTop;
      }
      return "pts";
    }

    /**
     * tiebreak.reverse_pointtype (MPvGP, C.07 13.1): the score that is not
     * the primary one.
     */
    std::string Engine::reversePointtype(Spec &tb)
    {
      static const std::map<std::string, std::string> trans{
        { "mpoints", "gpoints" }, { "gpoints", "mpoints" },
        { "mmpoints", "ggpoints" }, { "mgpoints", "gmpoints" },
        { "gmpoints", "mgpoints" }, { "ggpoints", "mmgpoints" } };
      if (!primaryscore)
      {
        // The reference fails without a primary score.
        throw InvalidRequestException(
          "Tie-break " + tb.name + " needs a primary score (PTS) before it");
      }
      const auto found = trans.find(*primaryscore);
      tb.pointtype = found != trans.end() ? found->second : *primaryscore;
      return "points";
    }

    /**
     * tiebreak.compute_recursive_if_tied, for the direct encounter family
     * (DE, EDE and its board variants) and for TBR and BBE: run the
     * criterion on every group of tied competitors again and again until no
     * group changes.
     */
    std::string Engine::computeRecursiveIfTied(Spec &tb, const bool topBottom)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      const std::string name{ lowerCase(tb.name) };
      std::vector<std::size_t> ro{ rankorder };
      for (const std::size_t player : ro)
      {
        values(cmps[player], prefix).vals[name] =
          Value::fromInt(cmps[player].rank);
        cmps[player].moreloops = true;
      }
      const auto singlerun = [this, &tb, topBottom](
        const std::vector<std::size_t> &subro,
        const int loopcount)
      {
        return topBottom ? singleRunTopBottomBoardResult(tb, subro, loopcount)
          : singleRunExtDirectEncounter(tb, subro, loopcount);
      };
      const auto rankValue = [this, &prefix, &name](const std::size_t player)
        -> Value &
      {
        return val(cmps[player], prefix, name);
      };
      int loopcount{ };
      bool moretodo{ singlerun(ro, loopcount) };
      while (moretodo)
      {
        moretodo = false;
        ++loopcount;
        std::size_t start{ };
        while (start < ro.size())
        {
          const Value currentrank{ rankValue(ro[start]) };
          std::size_t stop{ start + 1 };
          while (stop < ro.size() && rankValue(ro[stop]).equals(currentrank))
          {
            ++stop;
          }
          if (cmps[ro[start]].moreloops)
          {
            if (stop - start == 1)
            {
              cmps[ro[start]].moreloops = false;
            }
            else
            {
              const std::vector<std::size_t> subro(
                ro.begin() + static_cast<std::ptrdiff_t>(start),
                ro.begin() + static_cast<std::ptrdiff_t>(stop));
              const bool moreloops{ singlerun(subro, loopcount) };
              for (const std::size_t player : subro)
              {
                cmps[player].moreloops = moreloops;
              }
              moretodo = moretodo || moreloops;
            }
          }
          start = stop;
        }
        const bool moreloops{ singlerun({ }, loopcount) };
        moretodo = moretodo || moreloops;
        std::sort(
          ro.begin(),
          ro.end(),
          [this, &rankValue](const std::size_t left, const std::size_t right)
          {
            const Competitor &a{ cmps[left] };
            const Competitor &b{ cmps[right] };
            if (a.rank != b.rank)
            {
              return a.rank < b.rank;
            }
            const int comparison{ rankValue(left).compare(rankValue(right)) };
            if (comparison != 0)
            {
              return comparison < 0;
            }
            return a.cid < b.cid;
          });
      }
      // Within each group of equal rank, number the order from 1, or 0 for
      // all when it separates nobody.
      std::size_t start{ };
      while (start < ro.size())
      {
        const int currentrank{ cmps[ro[start]].rank };
        std::size_t stop{ start };
        while (stop < ro.size() && cmps[ro[stop]].rank == currentrank)
        {
          ++stop;
        }
        std::int64_t offset{ rankValue(ro[start]).integer };
        if (offset != rankValue(ro[stop - 1]).integer)
        {
          --offset;
        }
        for (std::size_t p{ start }; p < stop; ++p)
        {
          Value &value{ rankValue(ro[p]) };
          value = Value::fromInt(value.integer - offset);
        }
        start = stop;
      }
      return name;
    }

    /**
     * tiebreak.compute_singlerun_ext_direct_encounter: DE runs the primary
     * score ("P"); EDE alternates primary and secondary score ("PS"), and
     * its variants add board count ("C"), top board ("T") and bottom board
     * elimination ("B") criteria for two tied teams.
     */
    bool Engine::singleRunExtDirectEncounter(
      Spec &tb,
      const std::vector<std::size_t> &subro,
      const int loopcount)
    {
      if (loopcount == 0)
      {
        const std::string tops(
          static_cast<std::size_t>(std::max(teamsize, 0)), 'T');
        const std::string bottoms(
          static_cast<std::size_t>(std::max(teamsize - 1, 0)), 'B');
        const std::map<std::string, std::string> functype{
          { "DE", "P" }, { "EDE", "PS" }, { "EDEC", "PSC" },
          { "EDET", "PS" + tops }, { "EDEB", "PS" + bottoms },
          { "EDEBT", "PSC" + tops }, { "EDEBB", "PSC" + bottoms } };
        tb.functions = functype.at(tb.name);
        tb.loopcount = 0;
        tb.edechanges.assign(tb.functions.size(), true);
        tb.swap = 0;
        return true;
      }
      if (tb.loopcount != loopcount)
      {
        tb.loopcount = loopcount;
        tb.changes = 0;
      }
      if (subro.empty())
      {
        const int changes{ tb.changes };
        tb.edechanges[tb.swap] = changes > 0;
        const bool moretodo{
          std::any_of(
            tb.edechanges.begin(),
            tb.edechanges.end(),
            [](const bool changed)
            {
              return changed;
            }) };
        if (changes == 0)
        {
          tb.swap = (tb.swap + 1) % tb.functions.size();
          for (Competitor &cmp : cmps)
          {
            cmp.moreloops = true;
          }
        }
        else
        {
          tb.edechanges.assign(tb.edechanges.size(), true);
        }
        return moretodo && loopcount < 30;
      }
      int changes{ };
      const char func{ tb.functions[tb.swap] };
      if (func == 'P' || func == 'S')
      {
        const ScoreInfo info{ getScoreinfo(tb, func == 'P') };
        changes += basicDirectEncounter(tb, subro, info.points, *info.scoretype);
      }
      else if (subro.size() == 2)
      {
        // 13.3.2: exactly two teams still tied in both scores.
        const ScoreInfo info{ getScoreinfo(tb, false) };
        const int pos{ static_cast<int>(std::count(
          tb.functions.begin(),
          tb.functions.begin() + static_cast<std::ptrdiff_t>(tb.swap),
          func)) };
        const int size{ teamsize };
        const auto weight = [func, pos, size](const int board)
        {
          if (func == 'C')
          {
            // Board count is the lower-is-better board criterion; the
            // negated weight makes larger better here.
            return -board;
          }
          if (func == 'T')
          {
            return board - 1 == pos ? 1 : 0;
          }
          return size - board > pos ? 1 : 0;
        };
        for (const std::size_t player : subro)
        {
          for (auto &[round, rst] : cmps[player].rsts)
          {
            Decimal tscore{ 0, -1 };
            for (const BoardGame &game : rst.games)
            {
              tscore = tscore
                + Decimal::fromInt(weight(game.board)) * game.points;
            }
            rst.tpoints = tscore;
          }
        }
        changes += basicDirectEncounter(tb, subro, TPOINTS, *info.scoretype);
      }
      tb.changes += changes;
      return changes != 0 && loopcount < 30;
    }

    /**
     * tiebreak.compute_basic_direct_encounter: C.07 6.1 and 6.2. The score
     * against the tied opponents (averaged when two met more than once)
     * separates them when all have met; otherwise a competitor is ahead only
     * when no other could still catch up by winning their missing games.
     */
    int Engine::basicDirectEncounter(
      Spec &tb,
      std::vector<std::size_t> subro,
      const PointType points,
      const ScoreSystem &scoretype)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      const std::string name{ lowerCase(tb.name) };
      const auto rankValue = [this, &prefix, &name](const std::size_t player)
        -> Value &
      {
        return val(cmps[player], prefix, name);
      };
      int changes{ };
      const Decimal win{
        scoreValue(scoretype, "W")
        * Decimal::fromInt(points == GPOINTS ? teamsize : 1) };
      const Value currentrank{ rankValue(subro[0]) };
      bool metall{ true };
      const int metmax{ static_cast<int>(subro.size()) - 1 };
      for (const std::size_t player : subro)
      {
        Competitor &de{ cmps[player] };
        de.denum = 0;
        de.deval = Decimal{ };
        de.demax = Decimal{ };
        std::map<int, std::pair<int, Decimal>> delist{ };
        for (const auto &[round, rst] : de.rsts)
        {
          if (round > currentround || rst.opponent <= 0)
          {
            continue;
          }
          const bool played{ tb.modifiers.predetermined || rst.played };
          if (!played)
          {
            continue;
          }
          const auto opponent = index.find(rst.opponent);
          if (opponent == index.end())
          {
            throw std::out_of_range("KeyError: competitor");
          }
          if (!rankValue(opponent->second).equals(currentrank))
          {
            continue;
          }
          const Decimal score{ requirePoints(rst, points) };
          const auto found = delist.find(rst.opponent);
          if (found != delist.end())
          {
            Decimal average{ found->second.second };
            int num{ found->second.first };
            Decimal sumscore{ average * Decimal::fromInt(num) };
            de.deval = de.deval - average;
            ++num;
            sumscore = sumscore + score;
            average = sumscore / Decimal::fromInt(num);
            de.deval = de.deval + average;
            found->second = { num, average };
          }
          else
          {
            ++de.denum;
            de.deval = de.deval + score;
            delist[rst.opponent] = { 1, score };
          }
        }
        if ((!rr && de.denum < metmax) || tb.modifiers.swiss)
        {
          metall = false;
          de.demax = de.deval + Decimal::fromInt(metmax - de.denum) * win;
        }
        else
        {
          de.demax = de.deval;
        }
      }
      if (metall)
      {
        std::sort(
          subro.begin(),
          subro.end(),
          [this](const std::size_t left, const std::size_t right)
          {
            const int comparison{
              cmps[right].deval.compare(cmps[left].deval) };
            return comparison != 0 ? comparison < 0
              : cmps[left].cid < cmps[right].cid;
          });
        Value crank{ rankValue(subro[0]) };
        std::int64_t rank{ crank.integer };
        Decimal previous{ cmps[subro[0]].deval };
        for (std::size_t i{ 1 }; i < subro.size(); ++i)
        {
          ++rank;
          const Competitor &de{ cmps[subro[i]] };
          if (!(previous == de.deval))
          {
            crank = Value::fromInt(rank);
            previous = de.deval;
            ++changes;
          }
          rankValue(subro[i]) = crank;
        }
        return changes;
      }
      std::sort(
        subro.begin(),
        subro.end(),
        [this](const std::size_t left, const std::size_t right)
        {
          const int comparison{ cmps[right].deval.compare(cmps[left].deval) };
          if (comparison != 0)
          {
            return comparison < 0;
          }
          const int maximum{ cmps[right].demax.compare(cmps[left].demax) };
          if (maximum != 0)
          {
            return maximum < 0;
          }
          return cmps[left].cid < cmps[right].cid;
        });
      Value crank{ rankValue(subro[0]) };
      std::int64_t rank{ crank.integer };
      Decimal previous{ cmps[subro[0]].deval };
      bool unique{ true };
      for (std::size_t i{ 1 }; i < subro.size(); ++i)
      {
        ++rank;
        Decimal tbmax{ cmps[subro[i]].demax };
        for (std::size_t j{ i + 1 }; j < subro.size(); ++j)
        {
          if (tbmax < cmps[subro[j]].demax)
          {
            tbmax = cmps[subro[j]].demax;
          }
        }
        const Competitor &de{ cmps[subro[i]] };
        if (unique && tbmax < previous)
        {
          crank = Value::fromInt(rank);
          ++changes;
        }
        else
        {
          unique = false;
        }
        previous = de.deval;
        rankValue(subro[i]) = crank;
      }
      return changes;
    }

    /**
     * tiebreak.compute_singlerun_topbottomboardresult: TBR (C.07 12.2)
     * compares the board points board by board from the top, BBE (12.3)
     * removes the bottom boards one by one.
     */
    bool Engine::singleRunTopBottomBoardResult(
      const Spec &tb,
      const std::vector<std::size_t> &ro,
      const int loopcount)
    {
      const std::string name{ lowerCase(tb.name) };
      const ScoreInfo info{ getScoreinfo(tb, true) };
      const auto boardPoints = [](Competitor &cmp, const int board)
      {
        const std::vector<std::pair<int, Decimal>> &bp{
          values(cmp, "gpoints_").bp };
        const auto found = std::find_if(
          bp.begin(),
          bp.end(),
          [board](const std::pair<int, Decimal> &entry)
          {
            return entry.first == board;
          });
        if (found == bp.end())
        {
          throw std::out_of_range(
            "KeyError: board " + std::to_string(board));
        }
        return found->second;
      };
      if (loopcount == 0)
      {
        for (const std::size_t player : ro)
        {
          Competitor &cmp{ cmps[player] };
          cmp.tbrval = zero(info.match);
          cmp.bbeval = zero(info.match);
          for (const auto &[board, points] : values(cmp, "gpoints_").bp)
          {
            cmp.bbeval = cmp.bbeval + points;
          }
        }
        return true;
      }
      if (ro.empty())
      {
        return false;
      }
      for (const std::size_t player : ro)
      {
        Competitor &cmp{ cmps[player] };
        cmp.tbrval = boardPoints(cmp, loopcount);
        cmp.bbeval = cmp.bbeval - boardPoints(cmp, maxboard - loopcount + 1);
      }
      const bool top{ name == "tbr" };
      const auto score = [this, top](const std::size_t player)
      {
        return top ? cmps[player].tbrval : cmps[player].bbeval;
      };
      std::vector<std::size_t> subro{ ro };
      std::stable_sort(
        subro.begin(),
        subro.end(),
        [this, &score](const std::size_t left, const std::size_t right)
        {
          const int comparison{ score(right).compare(score(left)) };
          return comparison != 0 ? comparison < 0
            : cmps[left].cid < cmps[right].cid;
        });
      std::int64_t count{ val(cmps[ro[0]], info.prefix, name).integer };
      std::int64_t currentrank{ count };
      for (std::size_t player{ }; player < subro.size(); ++player)
      {
        // subro[-1] in the reference: the first is compared with the last.
        const std::size_t previous{
          player == 0 ? subro.size() - 1 : player - 1 };
        if (!(score(subro[player]) == score(subro[previous])))
        {
          currentrank = count;
        }
        val(cmps[subro[player]], info.prefix, name) =
          Value::fromInt(currentrank);
        ++count;
      }
      return loopcount < maxboard;
    }

    /** tiebreak.compute_progressive_score: C.07 7.5, cut from round 1. */
    std::string Engine::computeProgressiveScore(const Spec &tb)
    {
      const ScoreInfo info{ getScoreinfo(tb, true) };
      const int low{ tb.modifiers.cutlow };
      for (Competitor &cmp : cmps)
      {
        Value ps{ Value::fromInt(0) };
        Value ssf{ Value::fromInt(0) };
        for (int round{ 1 }; round <= currentround; ++round)
        {
          const auto found = cmp.rsts.find(round);
          std::optional<Decimal> points{ };
          if (found != cmp.rsts.end())
          {
            points = pointsOf(found->second, info.points);
          }
          ssf = ssf + Value::fromDecimal(points ? *points : zero(info.match));
          if (round > low)
          {
            ps = ps + ssf;
          }
        }
        values(cmp, info.prefix).vals["ps"] = ps;
      }
      return "ps";
    }

    /**
     * tiebreak.compute_koya: C.07 9.2, points against opponents who scored
     * at least the limit (50% by default).
     */
    std::string Engine::computeKoya(const Spec &tb)
    {
      const ScoreInfo info{ getScoreinfo(tb, true) };
      const Decimal win{ scoreValue(*info.scoretype, "W") };
      const int competitors{ static_cast<int>(cmps.size()) };
      int maxgames{ currentround };
      // In a round robin with an odd number of competitors everybody has
      // one game less; a competitor who was never paired does not count.
      if (rr)
      {
        if (currentround % competitors == 0)
        {
          maxgames -= currentround / competitors;
        }
        for (Competitor &cmp : cmps)
        {
          if (values(cmp, info.prefix).lmp == 0)
          {
            --maxgames;
          }
        }
      }
      const Decimal lim{
        tb.modifiers.plim * win * Decimal::fromInt(maxgames)
            * Decimal::fromInt(info.points == GPOINTS ? teamsize : 1)
            / Decimal(1000, -1)
          + tb.modifiers.nlim };
      for (Competitor &cmp : cmps)
      {
        Value ks{ Value::fromInt(0) };
        for (const auto &[round, rst] : cmp.rsts)
        {
          if (round > currentround || rst.opponent <= 0)
          {
            continue;
          }
          const Value oppscore{
            val(byCid(rst.opponent), info.prefix, "points") };
          const Value ownscore{ values(cmp, info.prefix).pointsRound.at(round) };
          if (oppscore.compare(Value::fromDecimal(lim)) >= 0)
          {
            ks = ks + ownscore;
          }
        }
        values(cmp, info.prefix).vals["ks"] = ks;
      }
      return "ks";
    }

    /**
     * tiebreak.compute_buchholz_sonneborn_berger_ver: Buchholz (8.1), Fore
     * Buchholz (8.3), Sonneborn-Berger (9.1) and the extended
     * Sonneborn-Berger of teams (13.2), with the unplayed-round rules of
     * C.07 16 and the cuts of 14.
     */
    std::string Engine::computeBuchholzSonnebornBerger(const Spec &tb)
    {
      std::string name{ lowerCase(tb.name) };
      const bool isfb{ name == "fb" || name == "afb" || tb.modifiers.foremode };
      const ScoreInfo own{ getScoreinfo(tb, true) };
      ScoreInfo second{ getScoreinfo(tb, name == "sb") };
      const std::string vprefix{ isteam ? "mpoints_" : "points_" };
      const Decimal opointsfordraw{
        scoreValue(*own.scoretype, "D")
        * Decimal::fromInt(own.points == GPOINTS ? teamsize : 1) };
      const Decimal spointsfordraw{
        scoreValue(*second.scoretype, "D")
        * Decimal::fromInt(second.points == GPOINTS ? teamsize : 1) };
      if (name == "aob")
      {
        name = "bh";
      }
      const bool esb{ name == "esb"
        || (name.size() == 5 && name[0] == 'e' && name.substr(3) == "sb") };
      const bool isSb{ name == "sb" || esb };
      if (esb)
      {
        second = getScoreinfo(tb, false);
      }
      for (Competitor &cmp : cmps)
      {
        PrefixValues &tbscore{ values(cmp, own.prefix) };
        PrefixValues &vscore{ values(cmp, vprefix) };
        // 16.3.2: for Fore Buchholz the last round is evaluated as a draw.
        if (isfb && vscore.lmp == rounds)
        {
          vscore.lna = vscore.lmp;
        }
        const bool isfore{ isfb && vscore.lmp == rounds };
        bool adjustfore{ };
        Value abh{ Value::fromInt(0) };
        int last{ };
        for (const auto &[round, rst] : cmp.rsts)
        {
          last = round;
          if (round > currentround)
          {
            continue;
          }
          const bool hasopponent{ round <= tbscore.lna || rst.opponent > 0 };
          adjustfore = adjustfore
            || (isfore && round == currentround && rst.opponent > 0);
          abh = abh
            + Value::fromDecimal(
              hasopponent ? requirePoints(rst, own.points)
                : rr ? zero(own.match) : opointsfordraw);
        }
        Value fbscore{ tbscore.vals.at("points") };
        if (adjustfore)
        {
          // The reference reads the points of the last round it iterated.
          const Value adjust{
            Value::fromDecimal(opointsfordraw)
            + Value::fromInt(-1) * tbscore.pointsRound.at(last) };
          abh = abh + adjust;
          fbscore = fbscore + adjust;
        }
        tbscore.vals["abh"] = abh;
        tbscore.ownscore = fbscore;
      }
      if (name == "abh" || name == "afb")
      {
        return "abh";
      }

      struct Contribution
      {
        bool vur;
        Value tbvalue;
        Value score;
      };
      for (Competitor &cmp : cmps)
      {
        std::vector<Contribution> bhvalue{ };
        for (const auto &[round, rst] : cmp.rsts)
        {
          if (round > currentround)
          {
            continue;
          }
          const int opponent{ rst.opponent };
          bool vur{ rst.vur };
          const bool played{
            tb.modifiers.predetermined || (isfb && round == rounds)
              || rst.played };
          Value score{ };
          if (played && opponent > 0)
          {
            vur = false;
            score = val(byCid(opponent), own.prefix, "abh");
          }
          else if (!rr)
          {
            score = values(cmp, own.prefix).ownscore;
            if (tb.modifiers.ver == 2)
            {
              // 2026 rules, 16.4: an unplayed round counts at most what the
              // opponent scored (16.4.1) or a draw in every round (16.4.2).
              const Value cap{
                opponent > 0 ? val(byCid(opponent), own.prefix, "abh")
                  : Value::fromDecimal(
                    opointsfordraw * Decimal::fromInt(currentround)) };
              if (cap.compare(score) < 0)
              {
                score = cap;
              }
            }
          }
          else
          {
            score = Value::fromDecimal(Decimal(0, 0));
          }
          Decimal sres{ };
          if (tb.modifiers.urd && !rr)
          {
            sres = spointsfordraw;
          }
          else
          {
            const std::optional<Decimal> points{
              pointsOf(rst, second.points) };
            sres = points ? *points : zero(second.match);
          }
          const Value tbvalue{
            isSb ? score * Value::fromDecimal(sres) : score };
          if (opponent > 0 || !rr)
          {
            bhvalue.push_back({ vur, tbvalue, score });
          }
        }
        int low{ std::min(tb.modifiers.cutlow, currentround) };
        int high{ tb.modifiers.cuthigh };
        if (low + high > currentround)
        {
          high = currentround - low;
        }
        while (low > 0 && !bhvalue.empty())
        {
          std::vector<Contribution> sortall{ bhvalue };
          std::stable_sort(
            sortall.begin(),
            sortall.end(),
            [](const Contribution &a, const Contribution &b)
            {
              const int score{ a.score.compare(b.score) };
              return score != 0 ? score < 0 : a.tbvalue.compare(b.tbvalue) < 0;
            });
          std::vector<Contribution> sortexp{ bhvalue };
          std::stable_sort(
            sortexp.begin(),
            sortexp.end(),
            [](const Contribution &a, const Contribution &b)
            {
              if (a.vur != b.vur)
              {
                return a.vur;
              }
              const int value{ a.tbvalue.compare(b.tbvalue) };
              return value != 0 ? value < 0 : a.score.compare(b.score) < 0;
            });
          // 14.1: the least significant value is cut, but an unplayed round
          // goes first unless a played one is worth less.
          if (!sortexp[0].vur
            || sortexp[0].tbvalue.compare(sortall[0].tbvalue) < 0
            || tb.modifiers.vun)
          {
            bhvalue.assign(sortall.begin() + 1, sortall.end());
          }
          else
          {
            bhvalue.assign(sortexp.begin() + 1, sortexp.end());
          }
          --low;
        }
        while (high > 0 && !bhvalue.empty())
        {
          std::stable_sort(
            bhvalue.begin(),
            bhvalue.end(),
            [](const Contribution &a, const Contribution &b)
            {
              const int score{ b.score.compare(a.score) };
              return score != 0 ? score < 0 : b.tbvalue.compare(a.tbvalue) < 0;
            });
          bhvalue.erase(bhvalue.begin());
          --high;
        }
        Value total{ Value::fromInt(0) };
        for (const Contribution &contribution : bhvalue)
        {
          total = total + contribution.tbvalue;
        }
        values(cmp, own.prefix).vals[name] = total;
      }
      return name;
    }

    /**
     * tiebreak.compute_ext_sonneborn_berger: ESB uses the primary and the
     * secondary score; EMMSB, EMGSB, EGMSB and EGGSB name both (13.2.1-4).
     */
    std::string Engine::computeExtSonnebornBerger(Spec &tb)
    {
      if (tb.name.size() == 5)
      {
        tb.pointtype = lowerCase(tb.name.substr(1, 2)) + "points";
      }
      return computeBuchholzSonnebornBerger(tb);
    }

    /** tiebreak.compute_rtng: the rating used for unrated competitors. */
    std::string Engine::computeRtng(const Spec &tb)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      const std::optional<int> unratedValue{
        tb.modifiers.unrated ? tb.modifiers.unrated : unrated };
      const bool allrated{
        unratedValue
        || std::all_of(cmps.begin(), cmps.end(), [](const Competitor &cmp)
          {
            return cmp.rating.has_value();
          }) };
      for (Competitor &cmp : cmps)
      {
        const std::optional<int> rtng{
          cmp.rating ? cmp.rating : unratedValue };
        values(cmp, prefix).vals["rtng"] =
          allrated && rtng ? Value::fromInt(*rtng) : Value::none();
      }
      return "rtng";
    }

    /**
     * tiebreak.compute_ratingperformance: ARO (10.1), TPR (10.2) and PTP
     * (10.3) over the played games, with cuts. All are empty unless every
     * competitor is rated or a rating for unrated competitors is given.
     */
    std::string Engine::computeRatingPerformance(const Spec &tb)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      const std::optional<int> unratedValue{
        tb.modifiers.unrated ? tb.modifiers.unrated : unrated };
      const bool valid{
        unratedValue
        || std::all_of(cmps.begin(), cmps.end(), [](const Competitor &cmp)
          {
            return cmp.rating.has_value();
          }) };
      computeRtng(tb);
      struct Opponent
      {
        int rating;
        Decimal rpoints;
      };
      for (Competitor &cmp : cmps)
      {
        std::vector<Opponent> ratingopp{ };
        int trounds{ };
        for (const auto &[round, rst] : cmp.rsts)
        {
          if (round > currentround || !rst.played || rst.opponent <= 0)
          {
            continue;
          }
          ++trounds;
          if (rst.opprating || unratedValue)
          {
            ratingopp.push_back({
              rst.opprating ? *rst.opprating : *unratedValue,
              rst.rpoints });
          }
        }
        int low{ std::min(tb.modifiers.cutlow, currentround) };
        int high{ tb.modifiers.cuthigh };
        if (low + high > currentround)
        {
          high = currentround - low;
        }
        if (tb.modifiers.ver == 0 && (low > 0 || high > 0))
        {
          // 2022 rules (World Rapid and Blitz): unplayed rounds are cut
          // first.
          const int unplayed{ currentround - trounds };
          int difference{ std::min(unplayed, low) };
          low -= difference;
          difference = std::min(unplayed - difference, high);
          high -= difference;
        }
        const auto byRating = [](const Opponent &a, const Opponent &b)
        {
          return a.rating < b.rating;
        };
        while (low > 0)
        {
          if (trounds == static_cast<int>(ratingopp.size()))
          {
            std::stable_sort(ratingopp.begin(), ratingopp.end(), byRating);
            if (!ratingopp.empty())
            {
              ratingopp.erase(ratingopp.begin());
            }
          }
          --trounds;
          --low;
        }
        while (high > 0)
        {
          if (trounds == static_cast<int>(ratingopp.size()))
          {
            std::stable_sort(ratingopp.begin(), ratingopp.end(), byRating);
            if (!ratingopp.empty())
            {
              ratingopp.pop_back();
            }
          }
          --trounds;
          --high;
        }
        Decimal rscore{ 0, 0 };
        std::vector<int> ratings{ };
        for (const Opponent &opponent : ratingopp)
        {
          rscore = rscore + opponent.rpoints;
          ratings.push_back(opponent.rating);
        }
        const auto toValue = [valid](const std::optional<int> value)
        {
          return valid && value ? Value::fromInt(*value) : Value::none();
        };
        std::map<std::string, Value> &vals{ values(cmp, prefix).vals };
        vals["aro"] = toValue(rating::averageRatingOfOpponents(ratings));
        vals["tpr"] =
          toValue(rating::tournamentPerformanceRating(rscore, ratings));
        vals["ptp"] =
          toValue(rating::perfectTournamentPerformance(rscore, ratings));
      }
      return lowerCase(tb.name);
    }

    /**
     * tiebreak.compute_average: AOB (8.2), APRO (10.4) and APPO (10.5), the
     * average of a value over the opponents of the played games, rounded
     * half up.
     */
    std::string Engine::computeAverage(
      const Spec &tb,
      const std::string &name,
      const int exponent)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      const std::string tbname{ lowerCase(tb.name) };
      for (Competitor &cmp : cmps)
      {
        Value sum{ Value::fromDecimal(Decimal(0, 0)) };
        int num{ };
        for (const auto &[round, rst] : cmp.rsts)
        {
          if (!rst.played || rst.opponent <= 0 || round > currentround)
          {
            continue;
          }
          const Value &value{ val(byCid(rst.opponent), prefix, name) };
          if (!value.isNone())
          {
            ++num;
            sum = sum + value;
          }
        }
        if (num == 0)
        {
          values(cmp, prefix).vals[tbname] = Value::none();
          continue;
        }
        Decimal average{ sum.asDecimal() / Decimal::fromInt(num) };
        // A zero average stays as divided: the reference only rounds a
        // value that is true.
        if (!average.isZero())
        {
          average = average.quantize(exponent, true);
        }
        values(cmp, prefix).vals[tbname] = Value::fromDecimal(average);
      }
      return tbname;
    }

    /**
     * tiebreak.compute_boardcount: BC (C.07 12.1), the board numbers
     * weighted by the game points scored on them.
     */
    std::string Engine::computeBoardcount(const Spec &tb)
    {
      const std::string prefix{ getScoreinfo(tb, true).prefix };
      for (Competitor &cmp : cmps)
      {
        Value bc{ Value::fromInt(0) };
        for (const auto &[board, points] : values(cmp, "gpoints_").bp)
        {
          bc = bc + Value::fromDecimal(Decimal::fromInt(board) * points);
        }
        values(cmp, prefix).vals["bc"] = bc;
      }
      return "bc";
    }

    /**
     * tiebreak.compute_score_strength_combination: SSSC (C.07 13.4), the
     * secondary score plus the Buchholz of the primary score divided by a
     * normalising factor, rounded half up to two decimals.
     */
    std::string Engine::computeScoreStrengthCombination(const Spec &tb)
    {
      computeBuchholzSonnebornBerger(tb);
      const ScoreInfo info{ getScoreinfo(tb, true) };
      const char key{ tb.pointtype.at(0) };
      const Decimal win{ scoreValue(*info.scoretype, "W") };
      // The reference divides by zero here and stops with a program error.
      const InvalidRequestException zeroFactor(
        "SSSC: the normalising factor is 0 in round "
        + std::to_string(currentround) + " with " + std::to_string(maxboard)
        + " boards; give one with /K");
      for (Competitor &cmp : cmps)
      {
        const Value dividend{ val(cmp, info.prefix, "sssc") };
        Value score{ };
        std::int64_t divisor{ 1 };
        if (key == 'm')
        {
          score = val(cmp, "gpoints_", "points");
          if (maxboard == 0)
          {
            throw zeroFactor;
          }
          divisor = floorDecimal(
            win * Decimal::fromInt(currentround) / scoreValue(gamescore, "W")
            / Decimal::fromInt(maxboard));
        }
        else if (key == 'g')
        {
          score = val(cmp, "mpoints_", "points");
          divisor = floorDecimal(
            win * Decimal::fromInt(currentround) * Decimal::fromInt(maxboard)
            / scoreValue(matchscore, "W"));
        }
        else
        {
          throw std::logic_error("UnboundLocalError: score");
        }
        Decimal quotient{ };
        if (Decimal{ } < tb.modifiers.nlim)
        {
          quotient = dividend.asDecimal() / tb.modifiers.nlim;
        }
        else if (dividend.kind == Value::INTEGER)
        {
          // int / int is a float in Python, and a float does not add to a
          // Decimal.
          throw std::logic_error("TypeError: float and Decimal");
        }
        else if (divisor == 0)
        {
          throw zeroFactor;
        }
        else
        {
          quotient = dividend.decimal / Decimal::fromInt(divisor);
        }
        const Decimal total{
          (score + Value::fromDecimal(quotient)).asDecimal() };
        values(cmp, info.prefix).vals["sssc"] =
          Value::fromDecimal(total.quantize(-2, true));
      }
      return "sssc";
    }

    Result Engine::computeTiebreaks(Options options)
    {
      findTmversion();
      if (options.tiebreaks.empty())
      {
        options.tiebreaks = tournament.rankOrder;
      }
      if (cmps.empty())
      {
        throw FileFormatException("The tournament has no players");
      }
      for (const std::string &descriptor : options.tiebreaks)
      {
        Spec tb{ parseTiebreak(descriptor) };
        computeSingleTiebreak(tb);
      }
      Result result{ };
      result.tiebreaks = options.tiebreaks;
      result.check = true;
      for (const Competitor &cmp : cmps)
      {
        result.competitors.push_back({ cmp.cid, cmp.rank, cmp.tiebreakScore });
        result.check = result.check && cmp.rank == cmp.orgrank;
      }
      return result;
    }
  }

  Result computeTiebreaks(const Tournament &tournament, Options options)
  {
    Engine engine{ tournament, options };
    return engine.computeTiebreaks(std::move(options));
  }
}
