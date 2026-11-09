/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The reading rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "decimal.h"
#include "trfreader.h"

namespace tiebreak
{
  namespace
  {
    /** Python's s[begin:end] for non-negative indices. */
    std::string slice(
      const std::string &text,
      const std::size_t begin,
      const std::size_t end = std::string::npos)
    {
      if (begin >= text.size() || end <= begin)
      {
        return "";
      }
      return text.substr(begin, end - begin);
    }

    /** Python's s[index], which fails past the end. */
    char at(const std::string &text, const std::size_t index)
    {
      if (index >= text.size())
      {
        throw std::out_of_range("string index out of range");
      }
      return text[index];
    }

    bool isSpace(const char character)
    {
      return std::isspace(static_cast<unsigned char>(character)) != 0;
    }

    std::string strip(const std::string &text)
    {
      std::size_t begin{ };
      std::size_t end{ text.size() };
      while (begin < end && isSpace(text[begin]))
      {
        ++begin;
      }
      while (end > begin && isSpace(text[end - 1]))
      {
        --end;
      }
      return text.substr(begin, end - begin);
    }

    std::string rstrip(const std::string &text)
    {
      std::size_t end{ text.size() };
      while (end > 0 && isSpace(text[end - 1]))
      {
        --end;
      }
      return text.substr(0, end);
    }

    char upper(const char character)
    {
      return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
    }

    char lower(const char character)
    {
      return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
    }

    std::string upper(std::string text)
    {
      for (char &character : text)
      {
        character = upper(character);
      }
      return text;
    }

    /** Python's str.replace for one character. */
    std::string replaceAll(std::string text, const char from, const char to)
    {
      std::replace(text.begin(), text.end(), from, to);
      return text;
    }

    /** Python's str.split(" "), which keeps empty fields. */
    std::vector<std::string> splitOnSpace(const std::string &text)
    {
      std::vector<std::string> fields{ };
      std::size_t begin{ };
      while (true)
      {
        const std::size_t end{ text.find(' ', begin) };
        if (end == std::string::npos)
        {
          fields.push_back(text.substr(begin));
          return fields;
        }
        fields.push_back(text.substr(begin, end - begin));
        begin = end + 1;
      }
    }

    /**
     * Gacrux's helpers.parse_int: blank is 0, otherwise Python's int().
     *
     * @throws std::invalid_argument if the text is not an integer.
     */
    int parseInt(const std::string &text)
    {
      const std::string stripped{ strip(text) };
      if (stripped.empty())
      {
        return 0;
      }
      std::size_t position{ };
      bool negative{ };
      if (stripped[0] == '+' || stripped[0] == '-')
      {
        negative = stripped[0] == '-';
        position = 1;
      }
      if (position == stripped.size())
      {
        throw std::invalid_argument("invalid integer: " + text);
      }
      long long value{ };
      for (; position < stripped.size(); ++position)
      {
        const char character{ stripped[position] };
        if (character < '0' || character > '9' || value > 100000000)
        {
          throw std::invalid_argument("invalid integer: " + text);
        }
        value = value * 10 + (character - '0');
      }
      return static_cast<int>(negative ? -value : value);
    }

    /** Gacrux's helpers.parse_float: blank is 0.0, a comma is a point. */
    Decimal parseFloat(const std::string &text)
    {
      const std::string stripped{ strip(text) };
      if (stripped.empty())
      {
        return Decimal(0, -1);
      }
      return Decimal::parse(replaceAll(stripped, ',', '.'));
    }

    /** Gacrux's helpers.parse_date. */
    std::string parseDate(const std::string &date)
    {
      const std::size_t space{ date.find(' ') };
      const std::string first{
        space == std::string::npos ? date : date.substr(0, space) };
      const std::string time{
        space == std::string::npos ? "" : date.substr(space + 1) };
      for (const char separator : { '.', '/' })
      {
        std::vector<std::string> parts{ };
        std::size_t begin{ };
        while (true)
        {
          const std::size_t end{ first.find(separator, begin) };
          if (end == std::string::npos)
          {
            parts.push_back(first.substr(begin));
            break;
          }
          parts.push_back(first.substr(begin, end - begin));
          begin = end + 1;
        }
        if (parts.size() != 3)
        {
          continue;
        }
        if (separator == '.')
        {
          if (parts[0].size() == 4)
          {
            return replaceAll(date, '.', '-');
          }
          if (space != std::string::npos)
          {
            return parts[2] + "-" + parts[1] + "-" + parts[0] + " " + time;
          }
          return parts[2] + "-" + parts[1] + "-" + parts[0];
        }
        if (parts[0].size() == 4)
        {
          return replaceAll(date, '/', '-');
        }
        return "20" + replaceAll(date, '/', '-');
      }
      return date;
    }

    struct ResultCode
    {
      const char *result;
      bool played;
      bool rated;
    };

    /** TRF result column to the score system's letters (trf2json.results). */
    ResultCode resultCode(const char code)
    {
      switch (code)
      {
      case '1':
        return { "W", true, true };
      case '=':
        return { "D", true, true };
      case '0':
        return { "L", true, true };
      case 'U':
        return { "P", true, false };
      case 'W':
        return { "W", true, false };
      case 'D':
        return { "D", true, false };
      case 'L':
        return { "L", true, false };
      case 'X':
      case '?':
        return { "A", true, false };
      case '+':
      case 'F':
        return { "W", false, false };
      case 'H':
        return { "D", false, false };
      default:
        return { "Z", false, false };
      }
    }

    /** chessjson.default_score["game"]. */
    ScoreSystem defaultGameScore()
    {
      ScoreSystem score{ };
      score["W"] = { false, "", Decimal(10, -1), false };
      score["D"] = { false, "", Decimal(5, -1), false };
      score["L"] = { false, "", Decimal(0, -1), false };
      score["F"] = { true, "W", Decimal{ }, false };
      score["H"] = { true, "D", Decimal{ }, false };
      score["Z"] = { false, "", Decimal(0, -1), false };
      score["P"] = { true, "W", Decimal{ }, false };
      score["A"] = { true, "D", Decimal{ }, false };
      score["U"] = { true, "Z", Decimal{ }, false };
      return score;
    }

    /**
     * chessjson.default_score["match"]: "FG" and the other starred entries
     * are the game points of the letter times the team size.
     */
    ScoreSystem defaultMatchScore()
    {
      ScoreSystem score{ };
      score["W"] = { false, "", Decimal(20, -1), false };
      score["D"] = { false, "", Decimal(10, -1), false };
      score["L"] = { false, "", Decimal(0, -1), false };
      score["F"] = { true, "W", Decimal{ }, false };
      score["H"] = { true, "D", Decimal{ }, false };
      score["Z"] = { false, "", Decimal(0, -1), false };
      score["P"] = { true, "D", Decimal{ }, false };
      score["A"] = { true, "D", Decimal{ }, false };
      score["U"] = { true, "Z", Decimal{ }, false };
      score["FG"] = { true, "W", Decimal{ }, true };
      score["HG"] = { true, "D", Decimal{ }, true };
      score["ZG"] = { true, "Z", Decimal{ }, true };
      score["PG"] = { true, "P", Decimal{ }, true };
      return score;
    }

    struct Equation
    {
      int cid{ };
      Decimal sum{ };
      std::map<std::string, int> counts{ };
    };

    /** A bye of record 240 or 320 (trf2json.byelist). */
    struct Bye
    {
      char type{ };
      int competitor{ };
      int round{ };
      std::string score{ };
    };

    struct Abnormal
    {
      char att{ };
      Decimal matchPoints{ };
      Decimal gamePoints{ };
      int round{ };
      std::vector<int> teams{ };
    };

    /** A record 330 forfeited match (trf2json.forfeitedlist). */
    struct Forfeited
    {
      std::string type{ };
      int round{ };
      int white{ };
      int black{ };
    };

    /** A record 300 out-of-order board line-up (trf2json.ooolist). */
    struct OutOfOrder
    {
      int round{ };
      int oooteam{ };
      int otherteam{ };
      std::vector<int> order{ };
    };

    // The order trf2json parses the records in; later records rely on what
    // earlier ones set (the player section before the byes, for example).
    const std::vector<std::string> RECORD_ORDER{
      "###", "012", "022", "032", "042", "052", "062", "072", "082", "092",
      "102", "112", "122", "132", "142", "152", "162", "172", "182", "192",
      "202", "212", "222", "352", "362", "001", "FID", "310", "013", "250",
      "260", "240", "320", "330", "299", "300", "801", "802", "XXR", "XXZ" };

    struct Code192
    {
      const char *code;
      bool teamTournament;
      bool fideteam;
    };

    /**
     * trf2json.code192: whether a record 192 code makes the event a team
     * event, and whether it is paired with the FIDE team system (which asks
     * for team pairing numbers 1 to N).
     */
    constexpr Code192 CODE192[]{
      { "FIDE_DUTCH", false, false },
      { "FIDE_DUTCH_2026", false, false },
      { "FIDE_DUTCH_2017", false, false },
      { "FIDE_DUBOV", false, false },
      { "FIDE_BURSTEIN", false, false },
      { "FIDE_DUTCH_BAKU", false, false },
      { "FIDE_DUTCH_2026_BAKU", false, false },
      { "FIDE_DUTCH_2017_BAKU", false, false },
      { "FIDE_DUBOV_BAKU", false, false },
      { "FIDE_BURSTEIN_BAKU", false, false },
      { "CUSTOM_SWISS", false, false },
      { "FIDE_DOUBLESWISS", false, false },
      { "FIDE_DOUBLESWISS_BAKU", false, false },
      { "CUSTOM_DOUBLESWISS", false, false },
      { "BERGER_ROUNDROBIN", false, false },
      { "BERGER_DOUBLEROUNDROBIN", false, false },
      { "FIDE_ROUNDROBIN", false, false },
      { "FIDE_DOUBLEROUNDROBIN", false, false },
      { "CUSTOM_ROUNDROBIN", false, false },
      { "FIDE_SCHILLER", true, false },
      { "CUSTOM_SCHILLER", true, false },
      { "FIDE_SCHEVENINGEN", true, false },
      { "FIDE_DOUBLESCHEVENINGEN", true, false },
      { "CUSTOM_SCHEVENINGEN", true, false },
      { "CUSTOM_KNOCKOUT", false, false },
      { "FIDE_TEAM_TYPEA_MP_GP", true, true },
      { "FIDE_TEAM_TYPEA_GP_MP", true, true },
      { "FIDE_TEAM_TYPEA_MP", true, true },
      { "FIDE_TEAM_TYPEA_GP", true, true },
      { "FIDE_TEAM_TYPEB_MP_GP", true, true },
      { "FIDE_TEAM_TYPEB_GP_MP", true, true },
      { "FIDE_TEAM_TYPEB_MP", true, true },
      { "FIDE_TEAM_TYPEB_GP", true, true },
      { "FIDE_TEAM_MP_GP", true, true },
      { "FIDE_TEAM_GP_MP", true, true },
      { "FIDE_TEAM_MP", true, true },
      { "FIDE_TEAM_GP", true, true },
      { "FIDE_TEAM", true, true },
      { "CUSTOM_TEAM_SWISS_MP", true, false },
      { "CUSTOM_TEAM_SWISS_GP", true, false },
      { "FIDE_TEAM_TYPEA_MP_GP_BAKU", true, true },
      { "FIDE_TEAM_TYPEA_MP_BAKU", true, true },
      { "FIDE_TEAM_TYPEB_MP_GP_BAKU", true, true },
      { "FIDE_TEAM_TYPEB_MP_BAKU", true, true },
      { "FIDE_TEAM_MP_GP_BAKU", true, true },
      { "FIDE_TEAM_MP_BAKU", true, true },
      { "FIDE_TEAM_BAKU", true, true },
      { "CUSTOM_TEAM_SWISS", true, false },
      { "BERGER_TEAM_ROUNDROBIN", true, false },
      { "BERGER_TEAM_DOUBLEROUNDROBIN", true, false },
      { "FIDE_TEAM_ROUNDROBIN", true, false },
      { "FIDE_TEAM_DOUBLEROUNDROBIN", true, false },
      { "CUSTOM_TEAM_ROUNDROBIN", true, false },
      { "CUSTOM_TEAM_KNOCKOUT", true, false } };

    /** The record 192 code, or nothing for a code the list lacks. */
    const Code192 *findCode192(const std::string &code)
    {
      for (const Code192 &entry : CODE192)
      {
        if (code == entry.code)
        {
          return &entry;
        }
      }
      return nullptr;
    }

    /** The cid of a side of a game or match (chessjson.get_result_cid). */
    int sideCid(const std::optional<GameSide> &side)
    {
      return side ? side->cid : 0;
    }

    /** The result of a side, "Z" when none (chessjson.get_result_res). */
    std::string sideResult(const std::optional<GameSide> &side)
    {
      return side && side->result ? *side->result : "Z";
    }

    /**
     * A Python dict: insertion-ordered, a key popped and set again goes to
     * the end. Values live in a deque so that references stay valid.
     */
    template <typename V>
    class OrderedMap
    {
    public:
      struct Entry
      {
        std::string key;
        V value;
        bool live;
      };

      bool contains(const std::string &key) const
      {
        return index.count(key) != 0;
      }

      /** Python's d[key] for reading, KeyError when absent. */
      V &at(const std::string &key)
      {
        const auto found = index.find(key);
        if (found == index.end())
        {
          throw std::out_of_range("KeyError: " + key);
        }
        return entries[found->second].value;
      }

      /** Python's d[key] = value. */
      V &set(const std::string &key, V value)
      {
        const auto found = index.find(key);
        if (found != index.end())
        {
          entries[found->second].value = std::move(value);
          return entries[found->second].value;
        }
        index[key] = entries.size();
        entries.push_back({ key, std::move(value), true });
        return entries.back().value;
      }

      /** Python's d.pop(key, None). */
      std::optional<V> pop(const std::string &key)
      {
        const auto found = index.find(key);
        if (found == index.end())
        {
          return std::nullopt;
        }
        Entry &entry{ entries[found->second] };
        entry.live = false;
        index.erase(found);
        return std::move(entry.value);
      }

      std::size_t size() const
      {
        return entries.size();
      }

      /** The entry at a position of the insertion order (may be dead). */
      Entry &entry(const std::size_t position)
      {
        return entries[position];
      }

    private:
      std::deque<Entry> entries{ };
      std::map<std::string, std::size_t> index{ };
    };

    /** A match while games2matches builds it; "black" may be absent. */
    struct Match
    {
      enum BlackState
      {
        ABSENT,
        NONE,
        SET
      };

      int id{ };
      int round{ };
      int teama{ };
      int teamb{ };
      std::vector<int> games{ };
      std::optional<GameSide> white{ };
      BlackState blackState{ ABSENT };
      GameSide black{ };
      std::optional<bool> played{ };

      std::optional<GameSide> blackSide() const
      {
        if (blackState == SET)
        {
          return black;
        }
        return std::nullopt;
      }
    };

    struct TeamMatch
    {
      int id{ };
      int team{ };
      std::vector<int> games{ };
    };

    std::string matchKey(const int round, const int a, const int b)
    {
      return std::to_string(round) + "-" + std::to_string(a) + "-"
        + std::to_string(b);
    }

    std::string teamKey(const int round, const int team)
    {
      return std::to_string(round) + "-" + std::to_string(team);
    }

    /**
     * games2matches: the matches of a team event, built from the board games
     * of the player section, the byes (records 240 and 320), the forfeited
     * matches (330) and the board orders (300), with the team result of
     * every match decided from its games.
     */
    class Games2Matches
    {
    public:
      Games2Matches(
        Tournament &tournament_,
        const std::vector<Competitor> &teams,
        const std::map<int, int> &playerOrder_,
        const std::vector<Bye> &byelist_,
        const std::vector<Forfeited> &forfeitedlist_,
        const std::vector<OutOfOrder> &ooolist_,
        int &matchid_,
        const std::optional<std::string> &teamSequence)
        : tournament(tournament_), playerOrder(playerOrder_),
          byelist(byelist_), forfeitedlist(forfeitedlist_),
          ooolist(ooolist_), matchid(matchid_),
          seq(teamSequence ? *teamSequence : "WB")
      {
        // build_cpointers
        cteam[0] = 0;
        for (const Competitor &team : teams)
        {
          for (const int player : team.cplayers)
          {
            cteam[player] = team.cid;
            cplayer.insert(player);
          }
        }
        for (Game &game : tournament.gameList)
        {
          cgames[game.id] = &game;
        }
      }

      std::vector<Game> mergeMatches()
      {
        sortGamesToMatchesAndByes();
        findTeamsize();
        addByes();
        addForfeited();
        addOoo();
        mergeByesIntoMatches();
        findZpb();
        buildTmatches();
        sortTmatches();
        sortOoo();
        mergeTmatches();
        decideScore();
        std::vector<Game> result{ };
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          const auto &entry = matches.entry(position);
          if (!entry.live)
          {
            continue;
          }
          const Match &match{ entry.value };
          if (!match.white || !match.played)
          {
            // The reference reads both keys of every match afterwards.
            throw std::out_of_range("KeyError: white/played");
          }
          Game game{ };
          game.id = match.id;
          game.round = match.round;
          game.white = *match.white;
          game.black = match.blackSide();
          game.played = *match.played;
          game.games = match.games;
          result.push_back(game);
        }
        return result;
      }

    private:
      Tournament &tournament;
      const std::map<int, int> &playerOrder;
      const std::vector<Bye> &byelist;
      const std::vector<Forfeited> &forfeitedlist;
      const std::vector<OutOfOrder> &ooolist;
      int &matchid;
      const std::string seq;
      std::map<int, int> cteam{ };
      std::set<int> cplayer{ };
      std::map<int, Game *> cgames{ };
      OrderedMap<Match> matches{ };
      OrderedMap<Match> byes{ };
      OrderedMap<TeamMatch> tmatches{ };
      std::set<int> removed{ };
      std::deque<Game> zgames{ };

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

      int teamOrZero(const int player) const
      {
        const auto found = cteam.find(player);
        return found == cteam.end() ? 0 : found->second;
      }

      int orderOf(const int player) const
      {
        if (player == 0)
        {
          return 0;
        }
        if (!cplayer.count(player))
        {
          throw std::out_of_range(
            "KeyError: player " + std::to_string(player));
        }
        return playerOrder.at(player);
      }

      Game &gameAt(const int id)
      {
        const auto found = cgames.find(id);
        if (found == cgames.end())
        {
          throw std::out_of_range("KeyError: game " + std::to_string(id));
        }
        return *found->second;
      }

      int teamsize() const
      {
        return tournament.teamSize;
      }

      /** Which side has the white pieces on a board (team colour order). */
      bool whiteOnBoard(const std::size_t board) const
      {
        return seq[board % seq.size()] == 'W';
      }

      /** Python's sorted(gameList, key=round) and the match/bye split. */
      void sortGamesToMatchesAndByes()
      {
        std::vector<Game *> games{ };
        for (Game &game : tournament.gameList)
        {
          games.push_back(&game);
        }
        std::stable_sort(
          games.begin(),
          games.end(),
          [](const Game *const a, const Game *const b)
          {
            return a->round < b->round;
          });
        for (Game *const game : games)
        {
          game->board = 0;
          const int round{ game->round };
          const int wt{ teamOf(game->white.cid) };
          const int bt{ teamOf(sideCid(game->black)) };
          const int a{ std::max(wt, bt) };
          const int b{ std::min(wt, bt) };
          const std::string key{ matchKey(round, a, b) };
          OrderedMap<Match> &target{ wt > 0 && bt > 0 ? matches : byes };
          if (!target.contains(key))
          {
            ++matchid;
            Match match{ };
            match.id = matchid;
            match.round = round;
            match.teama = a;
            match.teamb = b;
            target.set(key, match);
          }
          target.at(key).games.push_back(game->id);
        }
      }

      void findTeamsize()
      {
        if (tournament.teamSize != 0)
        {
          return;
        }
        int size{ };
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          if (matches.entry(position).live)
          {
            size = std::max(
              size,
              static_cast<int>(matches.entry(position).value.games.size()));
          }
        }
        tournament.teamSize = size;
      }

      void addByes()
      {
        for (const Bye &bye : byelist)
        {
          static const std::map<char, std::string> byetrans{
            { 'Z', "Z" }, { 'H', "D" }, { 'F', "W" }, { 'P', "P" } };
          const std::string key{ matchKey(bye.round, bye.competitor, 0) };
          const auto found = byetrans.find(bye.type);
          if (found == byetrans.end())
          {
            throw std::out_of_range(
              std::string("KeyError: bye type ") + bye.type);
          }
          if (!byes.contains(key))
          {
            ++matchid;
            Match match{ };
            match.id = matchid;
            match.round = bye.round;
            match.teama = bye.competitor;
            match.teamb = 0;
            byes.set(key, match);
          }
          Match &match{ byes.at(key) };
          match.white = GameSide{ bye.competitor, found->second, std::nullopt };
          match.blackState = Match::NONE;
          match.played = bye.type == 'P';
        }
      }

      void addForfeited()
      {
        for (const Forfeited &forfeited : forfeitedlist)
        {
          const int a{ std::max(forfeited.white, forfeited.black) };
          const int b{ std::min(forfeited.white, forfeited.black) };
          const std::string key{ matchKey(forfeited.round, a, b) };
          if (!matches.contains(key))
          {
            ++matchid;
            Match match{ };
            match.id = matchid;
            match.round = forfeited.round;
            match.teama = a;
            match.teamb = b;
            matches.set(key, match);
          }
          Match &match{ matches.at(key) };
          match.white = GameSide{
            forfeited.white, forfeited.type.substr(0, 1), std::nullopt };
          match.blackState = Match::SET;
          match.black = GameSide{
            forfeited.black, forfeited.type.substr(1, 1), std::nullopt };
          match.played = false;
        }
      }

      void addOoo()
      {
        for (const OutOfOrder &ooo : ooolist)
        {
          const int a{ std::max(ooo.oooteam, ooo.otherteam) };
          const int b{ std::min(ooo.oooteam, ooo.otherteam) };
          const std::string key{ matchKey(ooo.round, a, b) };
          if (!matches.contains(key))
          {
            ++matchid;
            Match match{ };
            match.id = matchid;
            match.round = ooo.round;
            match.teama = a;
            match.teamb = b;
            matches.set(key, match);
          }
        }
      }

      void mergeByesIntoMatches()
      {
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          auto &entry = matches.entry(position);
          if (!entry.live)
          {
            continue;
          }
          Match &match{ entry.value };
          for (const int team : { match.teama, match.teamb })
          {
            const std::optional<Match> bye{
              byes.pop(matchKey(match.round, team, 0)) };
            if (bye)
            {
              match.games.insert(
                match.games.end(),
                bye->games.begin(),
                bye->games.end());
            }
          }
        }
        for (std::size_t position{ }; position < byes.size(); ++position)
        {
          auto &entry = byes.entry(position);
          if (entry.live)
          {
            matches.set(entry.key, entry.value);
          }
        }
      }

      void findZpb()
      {
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          auto &entry = matches.entry(position);
          if (!entry.live || !entry.value.games.empty())
          {
            continue;
          }
          Match &match{ entry.value };
          if (!match.white)
          {
            throw std::out_of_range("KeyError: white");
          }
          if (!match.white->result)
          {
            match.white = GameSide{ match.teama, "Z", std::nullopt };
            match.blackState = Match::NONE;
            match.played = false;
          }
        }
      }

      void buildTmatches()
      {
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          auto &entry = matches.entry(position);
          if (!entry.live)
          {
            continue;
          }
          Match &match{ entry.value };
          if (match.blackState == Match::NONE)
          {
            match.games.clear();
            continue;
          }
          for (const int team : { match.teama, match.teamb })
          {
            if (team <= 0)
            {
              continue;
            }
            TeamMatch tmatch{ };
            tmatch.id = match.id;
            tmatch.team = team;
            for (const int id : match.games)
            {
              const Game &game{ gameAt(id) };
              if (teamOrZero(game.white.cid) == team
                || teamOrZero(sideCid(game.black)) == team)
              {
                tmatch.games.push_back(id);
              }
            }
            tmatches.set(teamKey(match.round, team), tmatch);
          }
        }
      }

      void sortTmatches()
      {
        for (std::size_t position{ }; position < tmatches.size(); ++position)
        {
          auto &entry = tmatches.entry(position);
          if (!entry.live)
          {
            continue;
          }
          const int teamno{ entry.value.team };
          struct SortKey
          {
            bool zeroBye;
            int order;
            int id;
          };
          std::vector<SortKey> keys{ };
          for (const int id : entry.value.games)
          {
            const Game &game{ gameAt(id) };
            const std::optional<GameSide> white{ game.white };
            const bool zeroBye{
              (sideCid(game.black) == 0 && sideResult(white) == "Z")
              || (game.white.cid == 0 && sideResult(game.black) == "Z") };
            const int player{
              teamOf(game.white.cid) == teamno ? game.white.cid
                : sideCid(game.black) };
            keys.push_back({ zeroBye, orderOf(player), id });
          }
          std::stable_sort(
            keys.begin(),
            keys.end(),
            [](const SortKey &a, const SortKey &b)
            {
              if (a.zeroBye != b.zeroBye)
              {
                return b.zeroBye;
              }
              return a.order < b.order;
            });
          entry.value.games.clear();
          for (const SortKey &key : keys)
          {
            entry.value.games.push_back(key.id);
          }
        }
      }

      void sortOoo()
      {
        const int size{ teamsize() };
        for (const OutOfOrder &ooo : ooolist)
        {
          const std::string key{ teamKey(ooo.round, ooo.oooteam) };
          const std::string match{
            std::to_string(ooo.round) + ", team "
            + std::to_string(ooo.oooteam) + "-"
            + std::to_string(ooo.otherteam) };
          if (!tmatches.contains(key))
          {
            throw FileFormatException(
              "Error in Out-of-order record (300), round " + match
                + ", No such match found");
          }
          TeamMatch &tmatch{ tmatches.at(key) };
          std::vector<Game *> games{ };
          for (const int id : tmatch.games)
          {
            if (id > 0)
            {
              games.push_back(&gameAt(id));
            }
            else
            {
              Game zgame{ };
              zgame.round = ooo.round;
              zgame.black = GameSide{ 0, std::nullopt, std::nullopt };
              zgames.push_back(zgame);
              games.push_back(&zgames.back());
            }
          }
          for (int i{ }; i < size; ++i)
          {
            if (i >= static_cast<int>(ooo.order.size()))
            {
              throw FileFormatException(
                "Error in Out-of-order record (300), round "
                  + std::to_string(ooo.round) + ", "
                  + std::to_string(ooo.oooteam) + "-"
                  + std::to_string(ooo.otherteam) + " has only "
                  + std::to_string(ooo.order.size())
                  + " players, but teamSize is " + std::to_string(size));
            }
            const int player{ ooo.order[static_cast<std::size_t>(i)] };
            if (player > 0)
            {
              Game *found{ };
              for (Game *const game : games)
              {
                if (game->white.cid == player || sideCid(game->black) == player)
                {
                  found = game;
                  break;
                }
              }
              if (!found)
              {
                throw FileFormatException(
                  "Error in Out-of-order record (300), round "
                    + std::to_string(ooo.round) + ", team "
                    + std::to_string(ooo.oooteam) + " has no player "
                    + std::to_string(player));
              }
              found->board = i + 1;
            }
          }
          std::vector<Game *> sortedgames(
            static_cast<std::size_t>(size), nullptr);
          std::deque<Game *> unsortedgames{ };
          for (Game *const game : games)
          {
            if (game->board > 0)
            {
              sortedgames.at(static_cast<std::size_t>(game->board - 1)) = game;
            }
            else
            {
              unsortedgames.push_back(game);
            }
          }
          std::vector<int> ids{ };
          for (Game *&game : sortedgames)
          {
            if (!game && !unsortedgames.empty())
            {
              game = unsortedgames.front();
              unsortedgames.pop_front();
            }
            ids.push_back(game ? game->id : 0);
          }
          for (const Game *const game : unsortedgames)
          {
            ids.push_back(game->id);
          }
          tmatch.games = ids;
        }
      }

      static bool contains(const std::vector<int> &list, const int value)
      {
        return std::find(list.begin(), list.end(), value) != list.end();
      }

      static void removeFirst(std::vector<int> &list, const int value)
      {
        const auto found = std::find(list.begin(), list.end(), value);
        if (found != list.end())
        {
          list.erase(found);
        }
      }

      static std::vector<int> head(const std::vector<int> &list, const int size)
      {
        const std::size_t length{
          std::min(list.size(), static_cast<std::size_t>(std::max(size, 0))) };
        return std::vector<int>(
          list.begin(),
          list.begin() + static_cast<std::ptrdiff_t>(length));
      }

      void mergeTmatches()
      {
        const int size{ teamsize() };
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          auto &entry = matches.entry(position);
          if (!entry.live)
          {
            continue;
          }
          Match &match{ entry.value };
          if (match.blackState != Match::NONE && match.teamb != 0)
          {
            std::vector<int> games1{
              head(tmatches.at(teamKey(match.round, match.teama)).games, size) };
            std::vector<int> games2{
              head(tmatches.at(teamKey(match.round, match.teamb)).games, size) };
            if (match.blackState == Match::ABSENT)
            {
              bool found{ };
              for (int game{ }; game < size; ++game)
              {
                const std::size_t board{ static_cast<std::size_t>(game) };
                if (board >= games2.size())
                {
                  throw std::out_of_range("IndexError: list index out of range");
                }
                if (games2[board] != 0 && contains(games1, games2[board]))
                {
                  const Game &cgame{ gameAt(games2[board]) };
                  const bool white{ whiteOnBoard(board) };
                  const int whiteTeam{
                    teamOf(white ? cgame.white.cid : sideCid(cgame.black)) };
                  const int blackTeam{
                    teamOf(white ? sideCid(cgame.black) : cgame.white.cid) };
                  match.white = GameSide{ whiteTeam, std::nullopt, std::nullopt };
                  match.blackState = Match::SET;
                  match.black = GameSide{ blackTeam, std::nullopt, std::nullopt };
                  found = true;
                  break;
                }
              }
              if (!found)
              {
                match.white = GameSide{ 0, std::nullopt, std::nullopt };
                match.blackState = Match::NONE;
              }
            }
            match.games.clear();
            for (int game{ }; game < size; ++game)
            {
              int game2{ };
              if (!games2.empty())
              {
                game2 = games2.front();
                games2.erase(games2.begin());
              }
              if (game2 != 0 && contains(games1, game2))
              {
                match.games.push_back(game2);
                removeFirst(games1, game2);
                continue;
              }
              std::vector<int> gamelist{ };
              for (const int candidate : games1)
              {
                if (candidate == 0 || !contains(games2, candidate))
                {
                  gamelist.push_back(candidate);
                }
              }
              if (gamelist.empty())
              {
                throw FileFormatException(
                  "Error in teams, round " + std::to_string(match.round) + ", "
                    + std::to_string(match.teama) + "-"
                    + std::to_string(match.teamb) + " has only "
                    + std::to_string(match.games.size())
                    + " games, but teamSize is " + std::to_string(size)
                    + ". Add 300 Out-of-order records to solve this");
              }
              const int game1{ gamelist.front() };
              removeFirst(games1, game1);
              const bool whiteBoard{ whiteOnBoard(static_cast<std::size_t>(game)) };
              if (game1 == 0)
              {
                match.games.push_back(game2);
              }
              else if (game2 == 0)
              {
                match.games.push_back(game1);
              }
              else
              {
                Game *wgame{ &gameAt(game1) };
                Game *bgame{ &gameAt(game2) };
                const int whiteTeam{
                  whiteBoard ? sideCid(match.white) : sideCid(match.blackSide()) };
                if (teamOf(wgame->white.cid) != whiteTeam)
                {
                  std::swap(wgame, bgame);
                }
                const std::string result{ sideResult(bgame->white) };
                if (!wgame->black)
                {
                  wgame->black = GameSide{ bgame->white.cid, result, std::nullopt };
                }
                else
                {
                  wgame->black->cid = bgame->white.cid;
                  wgame->black->result = result;
                }
                match.games.push_back(wgame->id);
                removed.insert(bgame->id);
              }
            }
            for (std::size_t board{ }; board < match.games.size(); ++board)
            {
              const auto found = cgames.find(match.games[board]);
              if (found != cgames.end())
              {
                found->second->board = static_cast<int>(board) + 1;
              }
            }
          }
          else if (!match.white)
          {
            const Game &first{ gameAt(match.games.at(0)) };
            match.white = GameSide{
              teamOf(first.white.cid), std::nullopt, std::nullopt };
            match.blackState = Match::NONE;
            match.played = sideResult(first.white) == "P";
          }
        }
        // list.remove in the reference: a merged game leaves the game list.
        std::vector<Game> kept{ };
        for (const Game &game : tournament.gameList)
        {
          if (!removed.count(game.id))
          {
            kept.push_back(game);
          }
        }
        // cgames keeps pointing into the old list until the scores are set.
        oldGames = std::move(tournament.gameList);
        cgames.clear();
        for (Game &game : oldGames)
        {
          cgames[game.id] = &game;
        }
        tournament.gameList = std::move(kept);
      }

      std::vector<Game> oldGames{ };

      Decimal gamePoints(const std::string &letter) const
      {
        return solveScore(tournament, false, letter);
      }

      void decideScore()
      {
        const int size{ teamsize() };
        const Decimal zero{ 0, -1 };
        // Python locals of the loop: they keep the value of an earlier
        // match when a match has no games.
        std::optional<bool> played{ };
        std::optional<bool> whiteIsWhite{ };
        for (std::size_t position{ }; position < matches.size(); ++position)
        {
          auto &entry = matches.entry(position);
          if (!entry.live)
          {
            continue;
          }
          Match &match{ entry.value };
          std::vector<const Game *> games{ };
          for (const int id : match.games)
          {
            const auto found = cgames.find(id);
            if (found != cgames.end())
            {
              games.push_back(found->second);
            }
          }
          Decimal whitePoints{ zero };
          Decimal blackPoints{ zero };
          if (!games.empty())
          {
            const int whitecid{ sideCid(match.white) };
            played = false;
            for (int game{ }; game < size; ++game)
            {
              if (game >= static_cast<int>(games.size()))
              {
                continue;
              }
              const Game &cgame{ *games[static_cast<std::size_t>(game)] };
              whiteIsWhite = teamOf(cgame.white.cid) == whitecid;
              played = *played || cgame.played;
              const Decimal white{ gamePoints(sideResult(cgame.white)) };
              const Decimal black{ gamePoints(sideResult(cgame.black)) };
              (*whiteIsWhite ? whitePoints : blackPoints) =
                (*whiteIsWhite ? whitePoints : blackPoints) + white;
              (*whiteIsWhite ? blackPoints : whitePoints) =
                (*whiteIsWhite ? blackPoints : whitePoints) + black;
            }
            if (!whiteIsWhite)
            {
              throw std::logic_error("UnboundLocalError: wcol");
            }
            match.played = *played
              || (zero < whitePoints && zero < blackPoints);
          }
          if (sideCid(match.blackSide()) > 0)
          {
            if (!played)
            {
              throw std::logic_error("UnboundLocalError: played");
            }
            const std::string loss{ *played ? "L" : "Z" };
            std::string white{ };
            std::string black{ };
            if (blackPoints < whitePoints)
            {
              white = "W";
              black = loss;
            }
            else if (whitePoints < blackPoints)
            {
              white = loss;
              black = "W";
            }
            else if (zero < whitePoints && zero < blackPoints)
            {
              white = "D";
              black = "D";
            }
            else
            {
              white = loss;
              black = loss;
            }
            match.white->result = white;
            match.black.result = black;
          }
          else if (!match.white || !match.white->result)
          {
            const Game &first{ gameAt(match.games.at(0)) };
            if (!match.white)
            {
              throw std::out_of_range("KeyError: white");
            }
            match.white->result = sideResult(first.white);
          }
        }
      }
    };

    /** The state of one read, in the shape of trf2json's members. */
    class Reader
    {
    public:
      Tournament tournament{ };
      std::map<int, Competitor> players{ };
      std::map<int, int> playerOrder{ };
      std::map<int, int> playerTeamId{ };
      std::map<int, Competitor> tcompetitors{ };
      std::map<int, Competitor> bcompetitors{ };
      std::vector<Competitor> teamsInFileOrder{ };
      std::map<std::string, int> teamIds{ };
      std::vector<Equation> equations{ };
      std::vector<Bye> byes{ };
      std::vector<Forfeited> forfeits{ };
      std::vector<OutOfOrder> outOfOrder{ };
      std::vector<Abnormal> abnormals{ };
      ScoreSystem declaredGameScore{ };
      ScoreSystem declaredMatchScore{ };
      std::optional<std::string> teamSequence{ };
      std::optional<bool> fideteam{ };
      std::optional<std::string> typeOfTournament{ };
      std::optional<std::string> pabRecord{ };
      int currentId{ };
      std::map<std::string, std::vector<std::string>> lines{ };

      void read(const std::string &contents);

    private:
      void parseRecord(const std::string &key, const std::string &line);
      void parsePlayer(const std::string &line);
      void parseGame(
        int startNumber,
        int round,
        const std::string &text,
        Equation &equation,
        int &lastPaired);
      void parseTeam(const std::string &line);
      void parseTypeOfTournament(const std::string &line);
      void parseColorSequence(const std::string &line);
      void parseScoreSystem(const std::string &line, ScoreSystem &target);
      void parseBye(const std::string &line);
      void parsePab(const std::string &line);
      void parseForfeited(const std::string &line);
      void parseOutOfOrder(const std::string &line);
      void parseAbnormal(const std::string &line);
      void parseAbsent(const std::string &line);
      void postParseTeams();
      void checkCompetitor(
        const std::string &record,
        int competitor,
        bool player) const;
      void checkPlayer(const std::string &record, int player) const;
      void appendResult(const Game &game);
      void validateTeamPairingNumbers() const;
      ScoreSystem fillDefaultScore(bool match) const;
      void updateGameScore(bool declared);
      void prepareTeamSection();
      void updateByes();
      void updateForfeited();
      void updateAbnormals();
    };

    void Reader::checkCompetitor(
      const std::string &record,
      const int competitor,
      const bool player) const
    {
      const bool team{ !player && tournament.teamTournament };
      const std::map<int, Competitor> &competitors{
        !team ? players
          : !tcompetitors.empty() ? tcompetitors : bcompetitors };
      if (competitors.count(competitor) || competitors.empty())
      {
        return;
      }
      const std::string what{ team ? "team" : "player" };
      throw FileFormatException(
        "Record " + record + " names " + what + " "
          + std::to_string(competitor) + ", the tournament has "
          + std::to_string(competitors.size()) + " " + what + "s ("
          + std::to_string(competitors.begin()->first) + " - "
          + std::to_string(competitors.rbegin()->first) + ")");
    }

    void Reader::checkPlayer(const std::string &record, const int player)
      const
    {
      checkCompetitor(record, player, true);
    }

    /**
     * chessjson.append_result: a game already recorded from the other
     * player's line (same round, same white player) is completed, not
     * repeated.
     */
    void Reader::appendResult(const Game &game)
    {
      for (Game &existing : tournament.gameList)
      {
        if (existing.round != game.round
          || existing.white.cid != game.white.cid)
        {
          continue;
        }
        if (!existing.white.result && game.white.result)
        {
          existing.white.result = game.white.result;
        }
        const bool newBlackResult{ game.black && game.black->result };
        if (existing.black && !existing.black->result && newBlackResult)
        {
          existing.black->result = game.black->result;
        }
        return;
      }
      tournament.gameList.push_back(game);
      tournament.gameList.back().id = ++currentId;
    }

    void Reader::parseGame(
      const int startNumber,
      const int round,
      const std::string &text,
      Equation &equation,
      int &lastPaired)
    {
      if (strip(text).empty())
      {
        return;
      }
      const int opponent{ parseInt(slice(text, 0, 4)) };
      char color{ lower(at(text, 5)) };
      if (color != 'w' && color != 'b' && color != '-' && color != ' ')
      {
        color = ' ';
      }
      const ResultCode code{ resultCode(upper(at(text, 7))) };
      const std::string result{ code.result };
      ++equation.counts[result];
      Game game{ };
      game.round = round;
      game.played = code.played;
      game.rated = code.rated;
      const int white{ color == 'b' ? opponent : startNumber };
      const int black{ color == 'b' ? startNumber : opponent };
      game.white.cid = white;
      if (color == 'b')
      {
        game.black = GameSide{ black, result, std::nullopt };
      }
      else
      {
        game.white.result = result;
        if (black != 0)
        {
          game.black = GameSide{ black, std::nullopt, std::nullopt };
        }
      }
      appendResult(game);
      if (game.played && white > 0 && black > 0 && round > lastPaired)
      {
        lastPaired = round;
      }
    }

    void Reader::parsePlayer(const std::string &line)
    {
      const int rating{ parseInt(slice(line, 48, 52)) };
      // Read for the error it raises on a malformed field, as trf2json does.
      parseInt(slice(line, 57, 68));
      ++currentId;
      const int startNumber{ parseInt(slice(line, 4, 8)) };
      // A colour letter that is not where a round's colour belongs.
      const std::string rest{ slice(line, 86) };
      const std::size_t whiteAt{ rest.find('w') };
      const std::size_t blackAt{ rest.find('b') };
      long col{ };
      for (const std::size_t found : { whiteAt, blackAt })
      {
        if (found != std::string::npos)
        {
          col = std::max(col, static_cast<long>(found));
        }
      }
      if (col % 10 != 0)
      {
        throw FileFormatException(
          "Player " + std::to_string(startNumber)
            + " has misaligned data, may be bad character encoding");
      }
      Competitor competitor{ };
      competitor.cid = startNumber;
      competitor.present = startNumber > 0;
      Equation equation{ };
      equation.cid = startNumber;
      equation.sum = parseFloat(slice(line, 80, 84));
      competitor.rank = parseInt(slice(line, 85, 89));
      if (rating > 0)
      {
        competitor.rating = rating;
      }
      players[startNumber] = competitor;
      int round{ };
      int lastPaired{ };
      for (std::size_t end{ 99 }; end <= line.size(); end += 10)
      {
        ++round;
        parseGame(
          startNumber,
          round,
          slice(line, end - 8, end),
          equation,
          lastPaired);
      }
      equations.push_back(equation);
      tournament.currentRound = std::max(tournament.currentRound, lastPaired);
    }

    /** trf2json.parse_trf_team: record 310 or the older 013. */
    void Reader::parseTeam(const std::string &line)
    {
      const bool ext{ line[0] == '3' };
      tournament.teamTournament = true;
      const int cid{
        ext ? parseInt(slice(line, 4, 7))
          : static_cast<int>(bcompetitors.size()) + 1 };
      const std::string teamname{
        rstrip(ext ? slice(line, 8, 40) : slice(line, 4, 36)) };
      int rank{ };
      if (ext)
      {
        parseFloat(slice(line, 54, 60));
        parseFloat(slice(line, 61, 67));
        rank = parseInt(slice(line, 68, 71));
        if (!strip(slice(line, 71, 73)).empty())
        {
          throw FileFormatException(
            "Team " + std::to_string(cid)
              + " has misaligned data, may be bad character encoding");
        }
      }
      // chessjson.append_team: teams are told apart by their name.
      const auto known = teamIds.find(teamname);
      const int teamid{
        known != teamIds.end() ? known->second : ++currentId };
      teamIds[teamname] = teamid;
      Competitor competitor{ };
      competitor.cid = cid;
      competitor.rank = rank;
      int board{ };
      for (std::size_t end{ ext ? 77u : 40u }; end <= line.size(); end += 5)
      {
        ++board;
        const int pid{ parseInt(slice(line, end - 4, end)) };
        if (pid == 0)
        {
          continue;
        }
        checkPlayer(slice(line, 0, 3), pid);
        if (!players.count(pid))
        {
          // No player section to check against: a KeyError in the reference.
          throw std::out_of_range("KeyError: player " + std::to_string(pid));
        }
        playerOrder[pid] = board;
        competitor.cplayers.push_back(pid);
        playerTeamId[pid] = teamid;
      }
      teamsInFileOrder.push_back(competitor);
      (ext ? tcompetitors : bcompetitors)[cid] = competitor;
    }

    /** trf2json.parse_trf_typetournament. */
    void Reader::parseTypeOfTournament(const std::string &line)
    {
      const std::string value{ upper(slice(line, 4)) };
      typeOfTournament = value;
      const Code192 *const found{ findCode192(value) };
      if (found)
      {
        tournament.teamTournament = found->teamTournament;
        fideteam = found->fideteam;
      }
      if (value.find("_MP_GP") != std::string::npos)
      {
        tournament.primary = "match";
      }
      else if (value.find("_GP_MP") != std::string::npos)
      {
        tournament.primary = "game";
      }
      else if (value.find("_MP") != std::string::npos)
      {
        tournament.primary = "match";
      }
      else if (value.find("_GP") != std::string::npos)
      {
        tournament.primary = "game";
      }
      else if (value == "FIDE_TEAM" || value == "FIDE_TEAM_BAKU")
      {
        tournament.primary = "match";
      }
    }

    /** trf2json.parse_colorsequence: record 352. */
    void Reader::parseColorSequence(const std::string &line)
    {
      if (!tournament.teamTournament && tournament.teamSize <= 1)
      {
        throw FileFormatException(
          "Record 352 is only valid in a team tournament");
      }
      if (teamSequence)
      {
        throw FileFormatException(
          "Record 352 may occur only once in a tournament");
      }
      std::string seq{ };
      for (const char character : strip(slice(line, 4)))
      {
        seq += upper(character);
      }
      if (seq.empty() || seq.find_first_not_of("WB") != std::string::npos)
      {
        throw FileFormatException(
          "Record 352 must contain a non-empty colour sequence using only W"
          " and B");
      }
      tournament.teamSize = static_cast<int>(seq.size());
      teamSequence = seq;
    }

    void Reader::parseScoreSystem(const std::string &line, ScoreSystem &target)
    {
      static const std::map<char, std::string> trans{
        { 'W', "W" }, { 'D', "D" }, { 'L', "L" }, { 'P', "P" }, { 'A', "Z" },
        { 'X', "A" } };
      for (std::size_t position{ 5 }; position + 1 < line.size(); position += 9)
      {
        const char symbol{ upper(line[position]) };
        const Decimal points{
          parseFloat(slice(line, position + 1, position + 5)) };
        const auto found = trans.find(symbol);
        if (found != trans.end())
        {
          target[found->second] = { false, "", points, false };
        }
      }
    }

    void Reader::parseBye(const std::string &line)
    {
      static const std::map<char, std::string> trans{
        { ' ', "Z" }, { 'Z', "Z" }, { 'H', "D" }, { 'F', "W" } };
      const char type{ upper(at(line, 4)) };
      const auto found = trans.find(type);
      if (found == trans.end())
      {
        throw std::invalid_argument("unknown bye type");
      }
      const int round{ parseInt(slice(line, 6, 9)) };
      for (std::size_t end{ 14 }; end <= line.size(); end += 5)
      {
        const int competitor{ parseInt(slice(line, end - 4, end)) };
        if (competitor > 0)
        {
          checkCompetitor(slice(line, 0, 3), competitor, false);
          byes.push_back({ type, competitor, round, found->second });
        }
      }
    }

    /** trf2json.parse_trf_pab: record 320, the pairing-allocated byes. */
    void Reader::parsePab(const std::string &line)
    {
      if (!tournament.teamTournament)
      {
        throw FileFormatException(
          "Record 320 \"" + rstrip(line) + "\" is for team tournaments only"
            " (TRF-2026): this file has no 013 or 310 record declaring a"
            " team");
      }
      if (pabRecord)
      {
        throw FileFormatException(
          "Record 320 may occur only once, one record per tournament"
          " (TRF-2026): a second one was found after \"" + rstrip(*pabRecord)
            + "\"");
      }
      pabRecord = line;
      const Decimal matchPoints{ parseFloat(slice(line, 4, 8)) };
      const Decimal gamePoints{ parseFloat(slice(line, 9, 13)) };
      declaredMatchScore["P"] = { false, "", matchPoints, false };
      declaredMatchScore["PG"] = { false, "", gamePoints, false };
      int round{ 1 };
      for (std::size_t end{ 17 }; end <= line.size(); end += 4)
      {
        const int competitor{ parseInt(slice(line, end - 3, end)) };
        if (competitor > 0)
        {
          checkCompetitor(slice(line, 0, 3), competitor, false);
          byes.push_back({ 'P', competitor, round, "" });
        }
        ++round;
      }
    }

    /** trf2json.parse_forfeited: record 330. */
    void Reader::parseForfeited(const std::string &line)
    {
      static const std::map<std::string, std::string> trans{
        { "10", "WZ" }, { "WL", "WZ" }, { "WZ", "WZ" }, { "+-", "WZ" },
        { "00", "ZZ" }, { "LL", "ZZ" }, { "ZZ", "ZZ" }, { "--", "ZZ" },
        { "01", "ZW" }, { "LW", "ZW" }, { "ZW", "ZW" }, { "-+", "ZW" } };
      const std::string forfeit{ upper(slice(line, 4, 6)) };
      const int round{ parseInt(slice(line, 7, 10)) };
      const int white{ parseInt(slice(line, 11, 14)) };
      const int black{ parseInt(slice(line, 15, 18)) };
      for (const int team : { white, black })
      {
        if (team > 0)
        {
          checkCompetitor(slice(line, 0, 3), team, false);
        }
      }
      forfeits.push_back({ trans.at(forfeit), round, white, black });
    }

    /** trf2json.parse_trf_outoforder: record 300. */
    void Reader::parseOutOfOrder(const std::string &line)
    {
      OutOfOrder ooo{ };
      ooo.round = parseInt(slice(line, 4, 7));
      ooo.oooteam = parseInt(slice(line, 8, 11));
      ooo.otherteam = parseInt(slice(line, 12, 15));
      checkCompetitor(slice(line, 0, 3), ooo.oooteam, false);
      checkCompetitor(slice(line, 0, 3), ooo.otherteam, false);
      for (std::size_t end{ 20 }; end <= line.size(); end += 5)
      {
        const int player{ parseInt(slice(line, end - 4, end)) };
        if (player > 0)
        {
          checkCompetitor(slice(line, 0, 3), player, true);
        }
        ooo.order.push_back(player);
      }
      outOfOrder.push_back(ooo);
    }

    void Reader::parseAbnormal(const std::string &line)
    {
      Abnormal abnormal{ };
      abnormal.att = at(line, 4);
      abnormal.matchPoints = parseFloat(slice(line, 7, 11));
      abnormal.gamePoints = parseFloat(slice(line, 13, 17));
      abnormal.round = parseInt(slice(line, 19, 22));
      for (std::size_t end{ 27 }; end <= line.size(); end += 5)
      {
        const int team{ parseInt(slice(line, end - 4, end)) };
        abnormal.teams.push_back(team);
        checkCompetitor(slice(line, 0, 3), team, false);
      }
      if (abnormal.round == 0
        && (abnormal.teams.empty() || abnormal.teams[0] == 0))
      {
        // scoresystem.add_unplayed: the value of an unplayed result in the
        // match score system.
        const std::string key(1, abnormal.att);
        declaredMatchScore[key] = { false, "", abnormal.matchPoints, false };
        declaredMatchScore[key + "G"] = {
          false, "", abnormal.gamePoints, false };
        return;
      }
      abnormals.push_back(abnormal);
    }

    void Reader::parseAbsent(const std::string &line)
    {
      const std::string text{
        replaceAll(replaceAll(slice(line, 4), ',', ' '), '/', ' ') };
      for (const std::string &field : splitOnSpace(text))
      {
        const int number{ parseInt(field) };
        if (number > 0)
        {
          checkPlayer(slice(line, 0, 3), number);
          players[number].present = false;
        }
      }
    }

    void Reader::parseRecord(const std::string &key, const std::string &line)
    {
      if (key == "042")
      {
        tournament.startDate = parseDate(slice(line, 4));
      }
      else if (key == "092")
      {
        tournament.tournamentType = slice(line, 4);
      }
      else if (key == "142" || key == "XXR")
      {
        tournament.numRounds = parseInt(rstrip(slice(line, 4)));
      }
      else if (key == "162")
      {
        parseScoreSystem(line, declaredGameScore);
      }
      else if (key == "362")
      {
        parseScoreSystem(line, declaredMatchScore);
      }
      else if (key == "192")
      {
        parseTypeOfTournament(line);
      }
      else if (key == "202" || key == "212")
      {
        const std::string prefix{ key == "202" ? "PTS " : "" };
        tournament.rankOrder =
          splitOnSpace(replaceAll(prefix + slice(line, 4), ',', ' '));
      }
      else if (key == "352")
      {
        parseColorSequence(line);
      }
      else if (key == "172" || key == "FID")
      {
        throw FileFormatException(
          "National rating records are not supported (record " + key + ")");
      }
      else if (key == "001")
      {
        parsePlayer(line);
      }
      else if (key == "310" || key == "013")
      {
        parseTeam(line);
      }
      else if (key == "240")
      {
        parseBye(line);
      }
      else if (key == "320")
      {
        parsePab(line);
      }
      else if (key == "330")
      {
        parseForfeited(line);
      }
      else if (key == "299")
      {
        parseAbnormal(line);
      }
      else if (key == "300")
      {
        parseOutOfOrder(line);
      }
      else if (key == "250")
      {
        // trf2json.parse_trf_accelerated: only the competitors are checked
        // here, acceleration is a pairing matter.
        parseFloat(slice(line, 4, 8));
        parseFloat(slice(line, 9, 13));
        parseInt(slice(line, 14, 17));
        parseInt(slice(line, 18, 21));
        const int first{ parseInt(slice(line, 22, 26)) };
        int last{ line.size() >= 31 ? parseInt(slice(line, 27, 31)) : 0 };
        if (last == 0)
        {
          last = first;
        }
        checkCompetitor(slice(line, 0, 3), first, false);
        checkCompetitor(slice(line, 0, 3), last, false);
      }
      else if (key == "260")
      {
        // trf2json.parse_trf_prohibited: likewise only checked.
        parseInt(slice(line, 4, 7));
        parseInt(slice(line, 8, 11));
        for (std::size_t end{ 16 }; end <= line.size(); end += 5)
        {
          checkCompetitor(
            slice(line, 0, 3),
            parseInt(slice(line, end - 4, end)),
            false);
        }
      }
      else if (key == "XXZ")
      {
        parseAbsent(line);
      }
    }

    /**
     * trf2json.post_parse_line after the team records: a team event needs
     * its team section, and without record 352 the team size is the most
     * games a team played in a round.
     */
    void Reader::postParseTeams()
    {
      if (tournament.teamTournament && !players.empty()
        && tcompetitors.empty() && bcompetitors.empty())
      {
        throw FileFormatException(
          "Record 310 is missing: "
          + (typeOfTournament
            ? "record 192 declares " + *typeOfTournament + ", a team tournament"
            : std::string("the file declares a team tournament"))
          + ", and TRF-2026 marks record 310 (the team section) mandatory for"
            " rating and pairing in team events. Every team has to be declared"
            " there, with its team pairing number and the start numbers of its"
            " players.");
      }
      if (!tournament.teamTournament || tournament.teamSize != 0)
      {
        return;
      }
      std::vector<std::map<int, int>> countgames(
        static_cast<std::size_t>(tournament.currentRound));
      int teamsize{ };
      for (const Game &game : tournament.gameList)
      {
        if (game.white.cid <= 0 || sideCid(game.black) <= 0)
        {
          continue;
        }
        const std::size_t round{ static_cast<std::size_t>(game.round - 1) };
        for (const int player : { game.white.cid, sideCid(game.black) })
        {
          const auto team = playerTeamId.find(player);
          if (team == playerTeamId.end())
          {
            // The reference fails here with a KeyError.
            throw FileFormatException(
              "Player " + std::to_string(player)
                + " plays a game but is in no team of the team section");
          }
          const int count{ ++countgames.at(round)[team->second] };
          teamsize = std::max(teamsize, count);
        }
      }
      tournament.teamSize = teamsize;
    }

    /**
     * trf2json.validate_team_pairing_numbers: every record 310 team has its
     * own pairing number, 1 to N when the FIDE team system pairs it.
     */
    void Reader::validateTeamPairingNumbers() const
    {
      const auto found = lines.find("310");
      if (found == lines.end())
      {
        return;
      }
      std::vector<int> numbers{ };
      for (const std::string &line : found->second)
      {
        numbers.push_back(parseInt(slice(line, 4, 7)));
      }
      std::sort(numbers.begin(), numbers.end());
      std::string list{ };
      for (const int number : numbers)
      {
        list += (list.empty() ? "" : ", ") + std::to_string(number);
      }
      if (fideteam.value_or(true))
      {
        std::string expected{ };
        bool ok{ true };
        for (std::size_t position{ }; position < numbers.size(); ++position)
        {
          const int number{ static_cast<int>(position) + 1 };
          ok = ok && numbers[position] == number;
          expected += (expected.empty() ? "" : ", ") + std::to_string(number);
        }
        if (ok)
        {
          return;
        }
        throw FileFormatException(
          "Record 310 must give each team a different tournament pairing"
          " number from 1 through the number of teams (C.04.6 art. 1.1.1);"
          " found " + list + ", expected " + expected);
      }
      const bool distinct{
        std::adjacent_find(numbers.begin(), numbers.end()) == numbers.end() };
      if (distinct && (numbers.empty() || numbers.front() > 0))
      {
        return;
      }
      throw FileFormatException(
        "Record 310 must give each team a different tournament pairing number"
        " from 1 to 999; found " + list);
    }

    /**
     * scoresystem.fill_default_scoresystem: the defaults with the declared
     * points over them, every number rounded to the decimals of the most
     * precise of W, D and L.
     */
    ScoreSystem Reader::fillDefaultScore(const bool match) const
    {
      ScoreSystem score{ match ? defaultMatchScore() : defaultGameScore() };
      for (const auto &[key, entry] : match ? declaredMatchScore
             : declaredGameScore)
      {
        score[key] = entry;
      }
      int exponent{ };
      for (const char *const result : { "W", "D", "L" })
      {
        const ScoreEntry &entry{ score[result] };
        if (!entry.isLetter)
        {
          exponent = std::min(exponent, entry.points.exponent);
        }
      }
      for (auto &[key, entry] : score)
      {
        if (!entry.isLetter)
        {
          entry.points = entry.points.quantize(exponent, false);
        }
      }
      return score;
    }

    /**
     * scoresystem.update_gamescore for a file whose score system is the
     * default one or is declared by record 162: unplayed results worth what
     * a win, draw, loss or zero is worth become that result, and every
     * player's reported points must add up.
     *
     * @throws FileFormatException if they do not.
     */
    void Reader::updateGameScore(const bool declared)
    {
      ScoreSystem score{ fillDefaultScore(false) };
      std::vector<std::pair<Decimal, std::string>> trans{ };
      for (const char *const result : { "W", "D", "L", "Z" })
      {
        const ScoreEntry &entry{ score[result] };
        if (entry.isLetter)
        {
          continue;
        }
        bool replaced{ };
        for (auto &[points, letter] : trans)
        {
          if (points == entry.points)
          {
            letter = result;
            replaced = true;
          }
        }
        if (!replaced)
        {
          trans.emplace_back(entry.points, result);
        }
      }
      for (const char *const result : { "F", "H", "P", "A", "U" })
      {
        ScoreEntry &entry{ score[result] };
        if (entry.isLetter)
        {
          continue;
        }
        for (const auto &[points, letter] : trans)
        {
          if (points == entry.points)
          {
            entry = { true, letter, Decimal{ }, false };
            break;
          }
        }
      }
      declaredGameScore = score;
      score = fillDefaultScore(false);
      std::vector<int> badPlayers{ };
      for (const Equation &equation : equations)
      {
        if (equation.sum.isZero())
        {
          continue;
        }
        Decimal checksum{ 0, -1 };
        for (const char *const result : { "W", "D", "L", "Z", "P", "A", "U" })
        {
          if (!score.count(result))
          {
            continue;
          }
          const auto count = equation.counts.find(result);
          const int number{ count == equation.counts.end() ? 0 : count->second };
          checksum = checksum
            + Decimal::fromInt(number) * resolveScore(score, result, true);
        }
        if (!(checksum == equation.sum))
        {
          badPlayers.push_back(equation.cid);
        }
      }
      if (!badPlayers.empty())
      {
        std::string names{ };
        for (const int player : badPlayers)
        {
          names += (names.empty() ? "" : ", ") + std::to_string(player);
        }
        throw FileFormatException(
          "no score system adds the results of player(s) " + names
            + " up to the points they report. Add a 162 record to declare"
            + " the score system"
            + (declared ? "" : " (deriving a TRF16 score system is not"
              " supported)"));
      }
      tournament.gameScore = score;
    }

    /**
     * trf2json.prepare_team_section: the matches from the games, then the
     * teams in pairing-number order, and every match must be between teams
     * the team section declares.
     */
    void Reader::prepareTeamSection()
    {
      const std::string record{ tcompetitors.empty() ? "013" : "310" };
      std::map<int, Competitor> &teams{
        tcompetitors.empty() ? bcompetitors : tcompetitors };
      Games2Matches g2m(
        tournament,
        teamsInFileOrder,
        playerOrder,
        byes,
        forfeits,
        outOfOrder,
        currentId,
        teamSequence);
      tournament.matchList = g2m.mergeMatches();
      for (const auto &[cid, team] : teams)
      {
        tournament.competitors.push_back(team);
      }
      // trf2json.team_score_totals: the one check of the totals that can
      // fail is a match of a team nobody declared.
      std::set<int> recorded{ };
      for (const Game &game : tournament.gameList)
      {
        recorded.insert(game.round);
      }
      for (const Game &match : tournament.matchList)
      {
        if (!recorded.count(match.round) && sideCid(match.black) <= 0)
        {
          continue;
        }
        for (const int cid : { match.white.cid, sideCid(match.black) })
        {
          if (cid > 0 && !teams.count(cid))
          {
            throw FileFormatException(
              "A match in round " + std::to_string(match.round)
                + " is played by team " + std::to_string(cid) + ", which record "
                + record + " does not declare: the tournament has "
                + std::to_string(teams.size()) + " teams"
                + (teams.empty() ? std::string{ }
                  : " (" + std::to_string(teams.begin()->first) + " - "
                    + std::to_string(teams.rbegin()->first) + ")"));
          }
        }
      }
      if (tournament.teamSize == 0 && !tournament.gameList.empty())
      {
        if (tournament.matchList.empty())
        {
          throw std::domain_error("ZeroDivisionError: no matches");
        }
        tournament.teamSize = static_cast<int>(std::nearbyint(
          static_cast<double>(tournament.gameList.size())
          / static_cast<double>(tournament.matchList.size())));
      }
    }

    /** trf2json.update_individualbye_list: record 240 byes become games. */
    void Reader::updateByes()
    {
      for (const Bye &bye : byes)
      {
        const Game *found{ };
        for (const Game &game : tournament.gameList)
        {
          const int black{ game.black ? game.black->cid : 0 };
          if (game.round == bye.round
            && (bye.competitor == game.white.cid || bye.competitor == black))
          {
            found = &game;
            break;
          }
        }
        if (!found)
        {
          Game game{ };
          game.round = bye.round;
          game.white = GameSide{ bye.competitor, bye.score, std::nullopt };
          appendResult(game);
          continue;
        }
        const bool white{ bye.competitor == found->white.cid };
        std::optional<std::string> recorded{
          white ? found->white.result : found->black->result };
        if (!recorded && found->black && found->black->cid > 0)
        {
          recorded =
            reverseResult(white ? found->black->result : found->white.result);
        }
        if (recorded != bye.score)
        {
          throw FileFormatException(
            "Error in bye score, competitor " + std::to_string(bye.competitor));
        }
      }
    }

    /**
     * trf2json.update_forfeited_list: a record 330 that names two teams
     * found in two different matches of the round joins them into one.
     */
    void Reader::updateForfeited()
    {
      for (const Forfeited &forfeited : forfeits)
      {
        const auto find = [this, &forfeited](const int team) -> Game *
        {
          for (Game &match : tournament.matchList)
          {
            if (match.round == forfeited.round
              && (match.white.cid == team || sideCid(match.black) == team))
            {
              return &match;
            }
          }
          return nullptr;
        };
        Game *const white{ find(forfeited.white) };
        Game *const black{ find(forfeited.black) };
        if (!white || !black || white->id == black->id)
        {
          continue;
        }
        if (!white->black)
        {
          throw std::logic_error("TypeError: match has no black side");
        }
        white->black->cid = std::max(black->white.cid, sideCid(black->black));
        white->white.result = forfeited.type.substr(0, 1);
        white->black->result = forfeited.type.substr(1, 1);
        const int removedId{ black->id };
        tournament.matchList.erase(std::remove_if(
          tournament.matchList.begin(),
          tournament.matchList.end(),
          [removedId](const Game &match)
          {
            return match.id == removedId;
          }), tournament.matchList.end());
      }
    }

    /** trf2json.update_abnormal_list. */
    void Reader::updateAbnormals()
    {
      const bool team{ tournament.teamTournament };
      // A record without round and competitors never gets here: it set the
      // match score system when it was read (parseAbnormal).
      for (const Abnormal &abnormal : abnormals)
      {
        const char att{ upper(abnormal.att) };
        if (att != ' ' && abnormal.round > 0 && !abnormal.teams.empty())
        {
          std::vector<Game> &collection{
            team ? tournament.matchList : tournament.gameList };
          for (const int competitor : abnormal.teams)
          {
            std::vector<GameSide *> sides{ };
            for (Game &game : collection)
            {
              if (game.round == abnormal.round && game.white.cid == competitor)
              {
                sides.push_back(&game.white);
              }
            }
            for (Game &game : collection)
            {
              if (game.round == abnormal.round && game.black
                && game.black->cid == competitor)
              {
                sides.push_back(&*game.black);
              }
            }
            if (sides.size() != 1)
            {
              throw FileFormatException(
                "Error in 299 Abnormal, Round = "
                  + std::to_string(abnormal.round) + ", player/team "
                  + std::to_string(competitor));
            }
            if (team)
            {
              sides[0]->gpoints = abnormal.gamePoints;
            }
            sides[0]->result =
              att == '-' ? "Z" : att == '+' ? "W" : std::string(1, att);
          }
        }
        else if (att == ' ' && !abnormal.teams.empty())
        {
          for (const int competitor : abnormal.teams)
          {
            checkCompetitor("299", competitor, false);
            for (Competitor &entry : tournament.competitors)
            {
              if (entry.cid == competitor)
              {
                entry.adjust.push_back({
                  abnormal.round % 999,
                  team ? abnormal.matchPoints : abnormal.gamePoints,
                  abnormal.gamePoints,
                  abnormal.round < 999 });
              }
            }
          }
        }
        else
        {
          throw FileFormatException(
            std::string("Error in 299 Abnormal, Att = ") + abnormal.att
              + ", round = " + std::to_string(abnormal.round));
        }
      }
    }

    void Reader::read(const std::string &contents)
    {
      std::string text{ contents };
      if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
      {
        text.erase(0, 3);
      }
      text = replaceAll(text, '\r', '\n');
      std::vector<std::pair<std::size_t, std::string>> numbered{ };
      std::size_t lineNumber{ };
      std::size_t begin{ };
      while (begin <= text.size())
      {
        const std::size_t end{ std::min(text.find('\n', begin), text.size()) };
        ++lineNumber;
        const std::string line{ text.substr(begin, end - begin) };
        if (line.size() >= 3)
        {
          lines[line.substr(0, 3)].push_back(line);
          numbered.emplace_back(lineNumber, line);
        }
        begin = end + 1;
      }
      // Team rosters are known before parsing.
      if (lines.count("310") || lines.count("013"))
      {
        tournament.teamTournament = true;
      }
      for (const std::string &key : RECORD_ORDER)
      {
        const auto found = lines.find(key);
        if (key == "013" && found != lines.end() && lines.count("310"))
        {
          // Record 310 replaces the older 013, which is then not read.
          continue;
        }
        if (found != lines.end())
        {
          for (const std::string &line : found->second)
          {
            try
            {
              parseRecord(key, line);
            }
            catch (const FileFormatException &)
            {
              throw;
            }
            catch (const std::exception &)
            {
              std::size_t number{ };
              for (const auto &[candidate, candidateLine] : numbered)
              {
                if (candidateLine == line)
                {
                  number = candidate;
                  break;
                }
              }
              throw FileFormatException(
                "Error in trf-file, line " + std::to_string(number) + ", "
                  + line);
            }
          }
        }
        if (key == "001")
        {
          for (const Game &game : tournament.gameList)
          {
            if (game.white.cid > 0)
            {
              checkPlayer("001", game.white.cid);
            }
            if (game.black && game.black->cid > 0)
            {
              checkPlayer("001", game.black->cid);
            }
          }
        }
        else if (key == "013")
        {
          postParseTeams();
        }
      }
      if (!lines.count("001") && !lines.count("092"))
      {
        throw FileFormatException("Error in trf, no 001 records");
      }
      validateTeamPairingNumbers();
      if (!lines.count("299"))
      {
        updateGameScore(lines.count("162") || lines.count("222"));
      }
      else
      {
        tournament.gameScore = fillDefaultScore(false);
      }
      if (tournament.teamTournament)
      {
        tournament.matchScore = fillDefaultScore(true);
        prepareTeamSection();
      }
      else
      {
        for (const auto &[cid, competitor] : players)
        {
          tournament.competitors.push_back(competitor);
        }
        updateByes();
      }
      updateForfeited();
      updateAbnormals();
    }
  }

  Decimal resolveScore(
    const ScoreSystem &scoreSystem,
    std::string result,
    const bool played)
  {
    // Bounded: a score system whose letters refer to each other in a circle
    // has no points to find.
    for (std::size_t step{ }; step <= scoreSystem.size(); ++step)
    {
      if (result == "L" && !played)
      {
        result = "Z";
      }
      const auto found = scoreSystem.find(result);
      if (found == scoreSystem.end())
      {
        break;
      }
      if (found->second.timesTeamSize)
      {
        // The reference ends its walk on the text "W*" and fails on it.
        throw std::logic_error("TypeError: score " + result);
      }
      if (!found->second.isLetter)
      {
        return found->second.points;
      }
      result = found->second.letter;
    }
    throw FileFormatException("The score system has no points for " + result);
  }

  Decimal solveScore(
    const Tournament &tournament,
    const bool match,
    const std::string &key)
  {
    bool inMatch{ match };
    std::string current{ key };
    Decimal factor{ 1, 0 };
    bool multiplied{ };
    for (int step{ }; step < 64; ++step)
    {
      const ScoreSystem &score{
        inMatch ? tournament.matchScore : tournament.gameScore };
      const auto found = score.find(current);
      if (found == score.end())
      {
        throw std::out_of_range("KeyError: score " + current);
      }
      if (!found->second.isLetter)
      {
        return multiplied ? found->second.points * factor
          : found->second.points;
      }
      if (found->second.timesTeamSize)
      {
        inMatch = false;
        factor = factor * Decimal::fromInt(tournament.teamSize);
        multiplied = true;
      }
      current = found->second.letter;
    }
    throw std::logic_error("RecursionError: score " + key);
  }

  std::optional<std::string> reverseResult(
    const std::optional<std::string> &result)
  {
    if (!result)
    {
      return std::nullopt;
    }
    static const std::map<std::string, std::string> reverse{
      { "W", "L" }, { "D", "D" }, { "L", "W" }, { "F", "Z" }, { "H", "H" },
      { "Z", "W" }, { "P", "Z" }, { "A", "A" }, { "U", "U" } };
    const auto found = reverse.find(*result);
    if (found == reverse.end())
    {
      return std::nullopt;
    }
    return found->second;
  }

  Tournament readTrf(const std::string &contents)
  {
    Reader reader{ };
    reader.read(contents);
    return reader.tournament;
  }
}
