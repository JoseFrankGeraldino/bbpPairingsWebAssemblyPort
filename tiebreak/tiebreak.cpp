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
    /** One round of a competitor (tiebreak.py "rsts"). */
    struct RoundResult
    {
      Decimal points{ };
      Decimal rpoints{ 0, -1 };
      std::string res{ "Z" };
      char color{ 'w' };
      bool played{ };
      bool vur{ true };
      int opponent{ };
      std::optional<int> opprating{ };
    };

    /** A competitor with its intermediate values (tiebreak.py "tbval"). */
    struct Competitor
    {
      int cid{ };
      int orgrank{ };
      int rank{ 1 };
      std::optional<int> rating{ };
      std::vector<Adjustment> adjust{ };
      std::map<int, RoundResult> rsts{ };
      std::vector<Value> tiebreakScore{ };
      std::map<std::string, Value> vals{ };
      std::map<int, Decimal> pointsPerRound{ };
      Value pointsTop{ };
      int lmp{ };
      int lna{ };
      Value ownscore{ };
      Decimal deval{ };
      Decimal demax{ };
      int denum{ };
      bool moreloops{ };
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
    };

    enum Function
    {
      FUNCTION_NUL,
      FUNCTION_PTS,
      FUNCTION_BUILTIN,
      FUNCTION_DE,
      FUNCTION_PS,
      FUNCTION_KS,
      FUNCTION_BH_SB,
      FUNCTION_AOB,
      FUNCTION_RTNG,
      FUNCTION_PERFORMANCE,
      FUNCTION_APRO,
      FUNCTION_APPO,
      FUNCTION_UNSUPPORTED
    };

    struct TiebreakDefinition
    {
      const char *name;
      Function function;
      bool rev;
    };

    /** tiebreak.py "tiebreaklist", individual tie-breaks only. */
    const std::map<std::string, TiebreakDefinition> &tiebreakList()
    {
      static const std::map<std::string, TiebreakDefinition> list{
        { "NUL", { "NUL", FUNCTION_NUL, false } },
        { "PTS", { "PTS", FUNCTION_PTS, true } },
        { "SNO", { "SNO", FUNCTION_BUILTIN, false } },
        { "TPN", { "SNO", FUNCTION_BUILTIN, false } },
        { "RANK", { "RANK", FUNCTION_BUILTIN, false } },
        { "WIN", { "WIN", FUNCTION_BUILTIN, true } },
        { "WON", { "WON", FUNCTION_BUILTIN, true } },
        { "BPG", { "BPG", FUNCTION_BUILTIN, true } },
        { "BWG", { "BWG", FUNCTION_BUILTIN, true } },
        { "GE", { "GE", FUNCTION_BUILTIN, true } },
        { "REP", { "REP", FUNCTION_BUILTIN, true } },
        { "RIP", { "RIP", FUNCTION_BUILTIN, true } },
        { "VUR", { "VUR", FUNCTION_BUILTIN, true } },
        { "NUM", { "NUM", FUNCTION_BUILTIN, true } },
        { "COD", { "COD", FUNCTION_BUILTIN, true } },
        { "RTG", { "RTG", FUNCTION_BUILTIN, true } },
        { "DE", { "DE", FUNCTION_DE, false } },
        { "PS", { "PS", FUNCTION_PS, true } },
        { "KS", { "KS", FUNCTION_KS, true } },
        { "BH", { "BH", FUNCTION_BH_SB, true } },
        { "FB", { "FB", FUNCTION_BH_SB, true } },
        { "SB", { "SB", FUNCTION_BH_SB, true } },
        { "ABH", { "ABH", FUNCTION_BH_SB, true } },
        { "AFB", { "AFB", FUNCTION_BH_SB, true } },
        { "AOB", { "AOB", FUNCTION_AOB, true } },
        { "RTNG", { "RTNG", FUNCTION_RTNG, true } },
        { "ARO", { "ARO", FUNCTION_PERFORMANCE, true } },
        { "TPR", { "TPR", FUNCTION_PERFORMANCE, true } },
        { "PTP", { "PTP", FUNCTION_PERFORMANCE, true } },
        { "APRO", { "APRO", FUNCTION_APRO, true } },
        { "APPO", { "APPO", FUNCTION_APPO, true } },
        // Known to the reference implementation but not computed here: team
        // tie-breaks (milestone 2) and helpers that are not FIDE tie-breaks.
        { "MPTS", { "MPTS", FUNCTION_UNSUPPORTED, true } },
        { "GPTS", { "GPTS", FUNCTION_UNSUPPORTED, true } },
        { "MPVGP", { "MPVGP", FUNCTION_UNSUPPORTED, true } },
        { "EDE", { "EDE", FUNCTION_UNSUPPORTED, false } },
        { "EDEC", { "EDEC", FUNCTION_UNSUPPORTED, false } },
        { "EDET", { "EDET", FUNCTION_UNSUPPORTED, false } },
        { "EDEB", { "EDEB", FUNCTION_UNSUPPORTED, false } },
        { "EDEBT", { "EDEBT", FUNCTION_UNSUPPORTED, false } },
        { "EDEBB", { "EDEBB", FUNCTION_UNSUPPORTED, false } },
        { "ESB", { "ESB", FUNCTION_UNSUPPORTED, true } },
        { "EMMSB", { "EMMSB", FUNCTION_UNSUPPORTED, true } },
        { "EMGSB", { "EMGSB", FUNCTION_UNSUPPORTED, true } },
        { "EGMSB", { "EGMSB", FUNCTION_UNSUPPORTED, true } },
        { "EGGSB", { "EGGSB", FUNCTION_UNSUPPORTED, true } },
        { "BC", { "BC", FUNCTION_UNSUPPORTED, false } },
        { "TBR", { "TBR", FUNCTION_UNSUPPORTED, false } },
        { "BBE", { "BBE", FUNCTION_UNSUPPORTED, false } },
        { "SSSC", { "SSSC", FUNCTION_UNSUPPORTED, true } },
        { "CSQ", { "CSQ", FUNCTION_UNSUPPORTED, true } },
        { "RSES", { "RSES", FUNCTION_UNSUPPORTED, true } },
        { "STD", { "STD", FUNCTION_UNSUPPORTED, true } },
        { "ACC", { "ACC", FUNCTION_UNSUPPORTED, true } },
        { "FLT", { "FLT", FUNCTION_UNSUPPORTED, true } },
        { "FLTD", { "FLTD", FUNCTION_UNSUPPORTED, true } },
        { "FLTFT", { "FLTFT", FUNCTION_UNSUPPORTED, true } },
        { "RFP", { "RFP", FUNCTION_UNSUPPORTED, true } },
        { "TOP", { "TOP", FUNCTION_UNSUPPORTED, true } },
        { "RND", { "RND", FUNCTION_UNSUPPORTED, false } } };
      return list;
    }

    struct Spec
    {
      std::string name{ };
      Modifiers modifiers{ };
      // The direct encounter loop state (tiebreak.py tb["ede"]).
      std::string functions{ };
      int loopcount{ };
      std::vector<bool> edechanges{ };
      std::size_t swap{ };
      int changes{ };
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

    class Engine
    {
    public:
      Engine(const Tournament &tournament, const Options &options);

      Result computeTiebreaks(const Tournament &tournament, Options options);

    private:
      std::vector<Competitor> cmps{ };
      std::map<int, std::size_t> index{ };
      std::vector<std::size_t> rankorder{ };
      int rounds{ };
      int currentround{ };
      bool rr{ };
      std::optional<int> unrated{ };
      int rulesversion{ 2 };
      ScoreSystem gamescore{ };
      ScoreSystem ratingScore{ };
      bool ptsComputed{ };

      Competitor &byCid(const int cid)
      {
        return cmps[index.at(cid)];
      }

      Decimal zero() const;
      Decimal getScore(
        const ScoreSystem &scoreSystem,
        const Game &game,
        bool white) const;
      bool isVur(const Game &game, bool white) const;
      void prepareResult(const Game &game);
      void computeScore();
      void findTmversion(const Tournament &tournament);
      Spec parseTiebreak(const std::string &text);
      void computeSingleTiebreak(Spec &tb);

      std::string computePts();
      std::string computeDirectEncounter(Spec &tb);
      bool singleRunDirectEncounter(
        Spec &tb,
        const std::vector<std::size_t> &subro,
        int loopcount);
      int basicDirectEncounter(
        Spec &tb,
        std::vector<std::size_t> subro);
      std::string computeProgressiveScore(const Spec &tb);
      std::string computeKoya(const Spec &tb);
      std::string computeBuchholzSonnebornBerger(const Spec &tb);
      std::string computeRtng(const Spec &tb);
      std::string computeRatingPerformance(const Spec &tb);
      std::string computeAverage(
        const Spec &tb,
        const std::string &name,
        int exponent);
    };

    Engine::Engine(const Tournament &tournament, const Options &options)
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
      gamescore = tournament.gameScore;
      ratingScore["W"] = { false, "", Decimal(10, -1) };
      ratingScore["D"] = { false, "", Decimal(5, -1) };
      ratingScore["Z"] = { false, "", Decimal(0, -1) };
      for (const char *const letter : { "L", "A", "U", "F", "H", "P" })
      {
        ratingScore[letter] = { true, "Z", Decimal{ } };
      }
      rounds = std::max(tournament.numRounds, tournament.currentRound);
      currentround = options.currentRound >= 0 ? options.currentRound : rounds;

      // prepare_competitors: every round starts as an unplayed zero.
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
          result.points = zero();
          cmp.rsts[round] = result;
        }
        index[cmp.cid] = cmps.size();
        cmps.push_back(cmp);
      }
      for (const Game &game : tournament.gameList)
      {
        prepareResult(game);
      }
      computeScore();
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

    Decimal Engine::zero() const
    {
      return resolveScore(gamescore, "Z", true);
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

    void Engine::prepareResult(const Game &game)
    {
      const int round{ game.round };
      const int white{ game.white.cid };
      const int black{ game.black ? game.black->cid : 0 };
      RoundResult whiteResult{ };
      whiteResult.points = getScore(gamescore, game, true);
      whiteResult.rpoints = getScore(ratingScore, game, true);
      whiteResult.vur = isVur(game, true);
      whiteResult.res = game.white.result.value_or("Z");
      whiteResult.color = 'w';
      whiteResult.played = game.played;
      whiteResult.opponent = black;
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
        blackResult.points = getScore(gamescore, game, false);
        blackResult.rpoints = getScore(ratingScore, game, false);
        blackResult.vur = isVur(game, false);
        blackResult.res = *game.black->result;
        blackResult.color = 'b';
        blackResult.played = game.played;
        blackResult.opponent = white;
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

    /** tiebreak.compute_score for an individual tournament. */
    void Engine::computeScore()
    {
      const Decimal win{ resolveScore(gamescore, "W", true) };
      for (Competitor &cmp : cmps)
      {
        std::map<std::string, Value> &vals{ cmp.vals };
        vals["nul"] = Value::fromInt(0);
        vals["sno"] = Value::fromInt(cmp.cid);
        vals["rank"] = Value::fromInt(cmp.orgrank);
        vals["rtg"] =
          cmp.rating ? Value::fromInt(*cmp.rating) : Value::none();
        Value points{ Value::fromDecimal(zero()) };
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
          cmp.pointsPerRound[round] = rst.points;
          points = points + Value::fromDecimal(rst.points);
          const bool isWin{ rst.points == win };
          wins += isWin;
          won += isWin && rst.played && rst.opponent > 0;
          bpg += rst.color == 'b' && rst.played;
          bwg += rst.color == 'b' && rst.played && isWin;
          ge += rst.played || isWin;
          rip += rst.played || rst.opponent > 0;
          vur += rst.vur;
          // Last round not absent, and last round paired or played (C.07
          // 16.2): Fore Buchholz and the unplayed-round rules read them.
          if (round > cmp.lna && !rst.vur)
          {
            cmp.lna = round;
          }
          if (round > cmp.lmp && rst.opponent > 0)
          {
            cmp.lmp = round;
          }
          if (rst.played && rst.opponent > 0)
          {
            cod += rst.color == 'w' ? 1 : -1;
          }
          if (rst.played)
          {
            num += rst.opponent > 0;
            if (round > cmp.lmp)
            {
              cmp.lmp = round;
            }
          }
        }
        Value adj{ Value::fromInt(0) };
        Value top{ Value::fromInt(0) };
        for (const Adjustment &adjustment : cmp.adjust)
        {
          if (adjustment.pairing && adjustment.round <= currentround)
          {
            adj = adj + Value::fromDecimal(adjustment.points);
          }
          if (!adjustment.pairing)
          {
            top = top + Value::fromDecimal(adjustment.points);
          }
        }
        cmp.pointsTop = top;
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
    void Engine::findTmversion(const Tournament &tournament)
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
     * tiebreak.parse_tiebreak for an individual tournament.
     *
     * @throws InvalidRequestException for a descriptor the tool cannot
     *   compute.
     */
    Spec Engine::parseTiebreak(const std::string &text)
    {
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
      const auto definition = tiebreakList().find(tb.name);
      // A point system suffix (":MP") names team scores, which an
      // individual tournament does not have.
      if (nameparts.size() == 2
        || (definition != tiebreakList().end()
          && definition->second.function == FUNCTION_UNSUPPORTED))
      {
        throw InvalidRequestException(
          "Tie-break not supported for individual tournaments: " + text);
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
      const auto found = tiebreakList().find(tb.name);
      const TiebreakDefinition definition{
        found == tiebreakList().end() ? tiebreakList().at("NUL")
          : found->second };
      tb.modifiers.rev = tb.modifiers.rev != definition.rev;
      std::string key{ };
      switch (definition.function)
      {
      case FUNCTION_NUL:
        key = "nul";
        break;
      case FUNCTION_PTS:
        key = computePts();
        break;
      case FUNCTION_BUILTIN:
        key = lowerCase(definition.name);
        break;
      case FUNCTION_DE:
        key = computeDirectEncounter(tb);
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
      default:
        throw InvalidRequestException("Tie-break not supported: " + tb.name);
      }
      for (Competitor &cmp : cmps)
      {
        cmp.tiebreakScore.push_back(cmp.vals.at(key));
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
      Value val{ cmps[rankorder[0]].tiebreakScore[position] };
      for (std::size_t i{ 1 }; i < rankorder.size(); ++i)
      {
        ++rank;
        Competitor &cmp{ cmps[rankorder[i]] };
        const Value &score{ cmp.tiebreakScore[position] };
        if (cmp.rank == rank || !score.equals(val))
        {
          cmp.rank = rank;
          val = score;
        }
        else
        {
          cmp.rank = cmps[rankorder[i - 1]].rank;
        }
      }
    }

    /** tiebreak.compute_pts: points plus the adjustments outside pairing. */
    std::string Engine::computePts()
    {
      if (!ptsComputed)
      {
        for (Competitor &cmp : cmps)
        {
          cmp.vals["pts"] = cmp.vals.at("points") + cmp.pointsTop;
        }
        ptsComputed = true;
      }
      return "pts";
    }

    /**
     * tiebreak.compute_recursive_if_tied with the direct encounter run
     * (C.07 6): rank the tied competitors by their games against each other,
     * again and again until no group of tied competitors changes.
     */
    std::string Engine::computeDirectEncounter(Spec &tb)
    {
      std::vector<std::size_t> ro{ rankorder };
      for (const std::size_t player : ro)
      {
        cmps[player].vals["de"] = Value::fromInt(cmps[player].rank);
        cmps[player].moreloops = true;
      }
      int loopcount{ };
      bool moretodo{ singleRunDirectEncounter(tb, ro, loopcount) };
      while (moretodo)
      {
        moretodo = false;
        ++loopcount;
        std::size_t start{ };
        while (start < ro.size())
        {
          const Value currentrank{ cmps[ro[start]].vals.at("de") };
          std::size_t stop{ start + 1 };
          while (stop < ro.size()
            && cmps[ro[stop]].vals.at("de").equals(currentrank))
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
              const bool moreloops{
                singleRunDirectEncounter(tb, subro, loopcount) };
              for (const std::size_t player : subro)
              {
                cmps[player].moreloops = moreloops;
              }
              moretodo = moretodo || moreloops;
            }
          }
          start = stop;
        }
        const bool moreloops{ singleRunDirectEncounter(tb, { }, loopcount) };
        moretodo = moretodo || moreloops;
        std::sort(
          ro.begin(),
          ro.end(),
          [this](const std::size_t left, const std::size_t right)
          {
            const Competitor &a{ cmps[left] };
            const Competitor &b{ cmps[right] };
            if (a.rank != b.rank)
            {
              return a.rank < b.rank;
            }
            const int comparison{ a.vals.at("de").compare(b.vals.at("de")) };
            if (comparison != 0)
            {
              return comparison < 0;
            }
            return a.cid < b.cid;
          });
      }
      // Within each group of equal rank, number the direct encounter order
      // from 1, or 0 for all when it separates nobody.
      std::size_t start{ };
      while (start < ro.size())
      {
        const int currentrank{ cmps[ro[start]].rank };
        std::size_t stop{ start };
        while (stop < ro.size() && cmps[ro[stop]].rank == currentrank)
        {
          ++stop;
        }
        std::int64_t offset{ cmps[ro[start]].vals.at("de").integer };
        if (offset != cmps[ro[stop - 1]].vals.at("de").integer)
        {
          --offset;
        }
        for (std::size_t p{ start }; p < stop; ++p)
        {
          Value &value{ cmps[ro[p]].vals.at("de") };
          value = Value::fromInt(value.integer - offset);
        }
        start = stop;
      }
      return "de";
    }

    /** tiebreak.compute_singlerun_ext_direct_encounter for DE ("P"). */
    bool Engine::singleRunDirectEncounter(
      Spec &tb,
      const std::vector<std::size_t> &subro,
      const int loopcount)
    {
      if (loopcount == 0)
      {
        tb.functions = "P";
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
      const int changes{ basicDirectEncounter(tb, subro) };
      tb.changes += changes;
      return changes != 0 && loopcount < 30;
    }

    /**
     * tiebreak.compute_basic_direct_encounter: C.07 6.1 and 6.2. The score
     * against the tied opponents (averaged when two met more than once)
     * separates them when all have met; otherwise a competitor is ahead only
     * when no other could still catch up by winning their missing games.
     */
    int Engine::basicDirectEncounter(Spec &tb, std::vector<std::size_t> subro)
    {
      int changes{ };
      const Decimal win{ resolveScore(gamescore, "W", true) };
      const Value currentrank{ cmps[subro[0]].vals.at("de") };
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
          if (!played
            || !byCid(rst.opponent).vals.at("de").equals(currentrank))
          {
            continue;
          }
          const auto found = delist.find(rst.opponent);
          if (found != delist.end())
          {
            Decimal score{ found->second.second };
            int num{ found->second.first };
            Decimal sumscore{ score * Decimal::fromInt(num) };
            de.deval = de.deval - score;
            ++num;
            sumscore = sumscore + rst.points;
            score = sumscore / Decimal::fromInt(num);
            de.deval = de.deval + score;
            found->second = { num, score };
          }
          else
          {
            ++de.denum;
            de.deval = de.deval + rst.points;
            delist[rst.opponent] = { 1, rst.points };
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
        Value crank{ cmps[subro[0]].vals.at("de") };
        std::int64_t rank{ crank.integer };
        Decimal val{ cmps[subro[0]].deval };
        for (std::size_t i{ 1 }; i < subro.size(); ++i)
        {
          ++rank;
          Competitor &de{ cmps[subro[i]] };
          if (!(val == de.deval))
          {
            crank = Value::fromInt(rank);
            de.vals["de"] = crank;
            val = de.deval;
            ++changes;
          }
          else
          {
            de.vals["de"] = crank;
          }
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
      Value crank{ cmps[subro[0]].vals.at("de") };
      std::int64_t rank{ crank.integer };
      Decimal val{ cmps[subro[0]].deval };
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
        Competitor &de{ cmps[subro[i]] };
        if (unique && tbmax < val)
        {
          crank = Value::fromInt(rank);
          de.vals["de"] = crank;
          val = de.deval;
          ++changes;
        }
        else
        {
          val = de.deval;
          de.vals["de"] = crank;
          unique = false;
        }
      }
      return changes;
    }

    /** tiebreak.compute_progressive_score: C.07 7.5, cut from round 1. */
    std::string Engine::computeProgressiveScore(const Spec &tb)
    {
      const int low{ tb.modifiers.cutlow };
      for (Competitor &cmp : cmps)
      {
        Value ps{ Value::fromInt(0) };
        Value ssf{ Value::fromInt(0) };
        for (int round{ 1 }; round <= currentround; ++round)
        {
          const auto found = cmp.rsts.find(round);
          ssf = ssf
            + Value::fromDecimal(
              found != cmp.rsts.end() ? found->second.points : zero());
          if (round > low)
          {
            ps = ps + ssf;
          }
        }
        cmp.vals["ps"] = ps;
      }
      return "ps";
    }

    /**
     * tiebreak.compute_koya: C.07 9.2, points against opponents who scored
     * at least the limit (50% by default).
     */
    std::string Engine::computeKoya(const Spec &tb)
    {
      const Decimal win{ resolveScore(gamescore, "W", true) };
      const int competitors{ static_cast<int>(cmps.size()) };
      int maxgames{ currentround };
      // In a round robin with an odd number of players everybody has one
      // game less; a player who was never paired does not count.
      if (rr)
      {
        if (currentround % competitors == 0)
        {
          maxgames -= currentround / competitors;
        }
        for (const Competitor &cmp : cmps)
        {
          if (cmp.lmp == 0)
          {
            --maxgames;
          }
        }
      }
      const Decimal lim{
        tb.modifiers.plim * win * Decimal::fromInt(maxgames)
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
          const Value oppscore{ byCid(rst.opponent).vals.at("points") };
          const Decimal ownscore{ cmp.pointsPerRound.at(round) };
          if (oppscore.compare(Value::fromDecimal(lim)) >= 0)
          {
            ks = ks + Value::fromDecimal(ownscore);
          }
        }
        cmp.vals["ks"] = ks;
      }
      return "ks";
    }

    /**
     * tiebreak.compute_buchholz_sonneborn_berger_ver: Buchholz (8.1), Fore
     * Buchholz (8.3) and Sonneborn-Berger (9.1) with the unplayed-round rules
     * of C.07 16 and the cuts of 14.
     */
    std::string Engine::computeBuchholzSonnebornBerger(const Spec &tb)
    {
      std::string name{ lowerCase(tb.name) };
      const bool isfb{ name == "fb" || name == "afb" || tb.modifiers.foremode };
      const Decimal draw{ resolveScore(gamescore, "D", true) };
      if (name == "aob")
      {
        name = "bh";
      }
      const bool isSb{ name == "sb" };
      for (Competitor &cmp : cmps)
      {
        // 16.3.2: for Fore Buchholz the last round is evaluated as a draw.
        if (isfb && cmp.lmp == rounds)
        {
          cmp.lna = cmp.lmp;
        }
        const bool isfore{ isfb && cmp.lmp == rounds };
        bool adjustfore{ };
        Value abh{ Value::fromInt(0) };
        for (const auto &[round, rst] : cmp.rsts)
        {
          if (round > currentround)
          {
            continue;
          }
          const Decimal pointsNoOpp{ rr ? zero() : draw };
          const bool hasopponent{ round <= cmp.lna || rst.opponent > 0 };
          adjustfore = adjustfore
            || (isfore && round == currentround && rst.opponent > 0);
          abh = abh + Value::fromDecimal(hasopponent ? rst.points : pointsNoOpp);
        }
        Value fbscore{ cmp.vals.at("points") };
        if (adjustfore)
        {
          // The reference reads the points of the last round it iterated.
          const Value adjust{
            Value::fromDecimal(
              draw - cmp.pointsPerRound.at(cmp.rsts.rbegin()->first)) };
          abh = abh + adjust;
          fbscore = fbscore + adjust;
        }
        cmp.vals["abh"] = abh;
        cmp.ownscore = fbscore;
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
            score = byCid(opponent).vals.at("abh");
          }
          else if (!rr)
          {
            score = cmp.ownscore;
            if (tb.modifiers.ver == 2)
            {
              // 2026 rules, 16.4: an unplayed round counts at most what the
              // opponent scored (16.4.1) or a draw in every round (16.4.2).
              const Value cap{
                opponent > 0 ? byCid(opponent).vals.at("abh")
                  : Value::fromDecimal(draw * Decimal::fromInt(currentround)) };
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
          const Decimal sres{
            tb.modifiers.urd && !rr ? draw : rst.points };
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
        Value val{ Value::fromInt(0) };
        for (const Contribution &contribution : bhvalue)
        {
          val = val + contribution.tbvalue;
        }
        cmp.vals[name] = val;
      }
      return name;
    }

    /** tiebreak.compute_rtng: the rating used for unrated players. */
    std::string Engine::computeRtng(const Spec &tb)
    {
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
        cmp.vals["rtng"] =
          allrated && rtng ? Value::fromInt(*rtng) : Value::none();
      }
      return "rtng";
    }

    /**
     * tiebreak.compute_ratingperformance: ARO (10.1), TPR (10.2) and PTP
     * (10.3) over the played games, with cuts. All are empty unless every
     * competitor is rated or a rating for unrated players is given.
     */
    std::string Engine::computeRatingPerformance(const Spec &tb)
    {
      const std::optional<int> unratedValue{
        tb.modifiers.unrated ? tb.modifiers.unrated : unrated };
      const bool valid{
        unratedValue
        || std::all_of(cmps.begin(), cmps.end(), [](const Competitor &cmp)
          {
            return cmp.rating.has_value();
          }) };
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
        cmp.vals["aro"] = toValue(rating::averageRatingOfOpponents(ratings));
        cmp.vals["tpr"] =
          toValue(rating::tournamentPerformanceRating(rscore, ratings));
        cmp.vals["ptp"] =
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
          const Value &value{ byCid(rst.opponent).vals.at(name) };
          if (!value.isNone())
          {
            ++num;
            sum = sum + value;
          }
        }
        if (num == 0)
        {
          cmp.vals[tbname] = Value::none();
          continue;
        }
        Decimal val{ sum.asDecimal() / Decimal::fromInt(num) };
        // A zero average stays as divided: the reference only rounds a
        // value that is true.
        if (!val.isZero())
        {
          val = val.quantize(exponent, true);
        }
        cmp.vals[tbname] = Value::fromDecimal(val);
      }
      return tbname;
    }

    Result Engine::computeTiebreaks(
      const Tournament &tournament,
      Options options)
    {
      findTmversion(tournament);
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
    return engine.computeTiebreaks(tournament, std::move(options));
  }
}
