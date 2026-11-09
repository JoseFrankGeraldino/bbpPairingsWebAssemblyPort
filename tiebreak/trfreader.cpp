/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The reading rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#include <algorithm>
#include <cctype>
#include <cstddef>
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
      score["W"] = { false, "", Decimal(10, -1) };
      score["D"] = { false, "", Decimal(5, -1) };
      score["L"] = { false, "", Decimal(0, -1) };
      score["F"] = { true, "W", Decimal{ } };
      score["H"] = { true, "D", Decimal{ } };
      score["Z"] = { false, "", Decimal(0, -1) };
      score["P"] = { true, "W", Decimal{ } };
      score["A"] = { true, "D", Decimal{ } };
      score["U"] = { true, "Z", Decimal{ } };
      return score;
    }

    struct Equation
    {
      int cid{ };
      Decimal sum{ };
      std::map<std::string, int> counts{ };
    };

    struct Bye
    {
      int competitor{ };
      int round{ };
      std::string score{ };
    };

    struct Abnormal
    {
      char att{ };
      Decimal gamePoints{ };
      int round{ };
      std::vector<int> teams{ };
    };

    // The order trf2json parses the records in; later records rely on what
    // earlier ones set (the player section before the byes, for example).
    const std::vector<std::string> RECORD_ORDER{
      "###", "012", "022", "032", "042", "052", "062", "072", "082", "092",
      "102", "112", "122", "132", "142", "152", "162", "172", "182", "192",
      "202", "212", "222", "352", "362", "001", "FID", "310", "013", "250",
      "260", "240", "320", "330", "299", "300", "801", "802", "XXR", "XXZ" };

    const std::set<std::string> TEAM_CODES{
      "FIDE_SCHILLER", "CUSTOM_SCHILLER", "FIDE_SCHEVENINGEN",
      "FIDE_DOUBLESCHEVENINGEN", "CUSTOM_SCHEVENINGEN",
      "FIDE_TEAM_TYPEA_MP_GP", "FIDE_TEAM_TYPEA_GP_MP", "FIDE_TEAM_TYPEA_MP",
      "FIDE_TEAM_TYPEA_GP", "FIDE_TEAM_TYPEB_MP_GP", "FIDE_TEAM_TYPEB_GP_MP",
      "FIDE_TEAM_TYPEB_MP", "FIDE_TEAM_TYPEB_GP", "FIDE_TEAM_MP_GP",
      "FIDE_TEAM_GP_MP", "FIDE_TEAM_MP", "FIDE_TEAM_GP", "FIDE_TEAM",
      "CUSTOM_TEAM_SWISS_MP", "CUSTOM_TEAM_SWISS_GP",
      "FIDE_TEAM_TYPEA_MP_GP_BAKU", "FIDE_TEAM_TYPEA_MP_BAKU",
      "FIDE_TEAM_TYPEB_MP_GP_BAKU", "FIDE_TEAM_TYPEB_MP_BAKU",
      "FIDE_TEAM_MP_GP_BAKU", "FIDE_TEAM_MP_BAKU", "FIDE_TEAM_BAKU",
      "CUSTOM_TEAM_SWISS", "BERGER_TEAM_ROUNDROBIN",
      "BERGER_TEAM_DOUBLEROUNDROBIN", "FIDE_TEAM_ROUNDROBIN",
      "FIDE_TEAM_DOUBLEROUNDROBIN", "CUSTOM_TEAM_ROUNDROBIN",
      "CUSTOM_TEAM_KNOCKOUT" };

    /** The state of one read, in the shape of trf2json's members. */
    class Reader
    {
    public:
      Tournament tournament{ };
      std::map<int, Competitor> players{ };
      std::vector<Equation> equations{ };
      std::vector<Bye> byes{ };
      std::vector<Abnormal> abnormals{ };
      ScoreSystem declaredGameScore{ };
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
      void parseScoreSystem(const std::string &line);
      void parseBye(const std::string &line);
      void parseAbnormal(const std::string &line);
      void parseAbsent(const std::string &line);
      void checkPlayer(const std::string &record, int player) const;
      void appendResult(const Game &game);
      ScoreSystem fillDefaultGameScore() const;
      void updateGameScore(bool declared);
      void updateByes();
      void updateAbnormals();
    };

    void Reader::checkPlayer(const std::string &record, const int player)
      const
    {
      if (players.count(player) || players.empty())
      {
        return;
      }
      throw FileFormatException(
        "Record " + record + " names player " + std::to_string(player)
          + ", the tournament has " + std::to_string(players.size())
          + " players (" + std::to_string(players.begin()->first) + " - "
          + std::to_string(players.rbegin()->first) + ")");
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
        game.black = GameSide{ black, result };
      }
      else
      {
        game.white.result = result;
        if (black != 0)
        {
          game.black = GameSide{ black, std::nullopt };
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
      const int startNumber{ parseInt(slice(line, 4, 8)) };
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

    void Reader::parseScoreSystem(const std::string &line)
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
          declaredGameScore[found->second] = { false, "", points };
        }
      }
    }

    void Reader::parseBye(const std::string &line)
    {
      static const std::map<char, std::string> trans{
        { ' ', "Z" }, { 'Z', "Z" }, { 'H', "D" }, { 'F', "W" } };
      const auto found = trans.find(upper(at(line, 4)));
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
          checkPlayer(slice(line, 0, 3), competitor);
          byes.push_back({ competitor, round, found->second });
        }
      }
    }

    void Reader::parseAbnormal(const std::string &line)
    {
      Abnormal abnormal{ };
      abnormal.att = at(line, 4);
      parseFloat(slice(line, 7, 11));
      abnormal.gamePoints = parseFloat(slice(line, 13, 17));
      abnormal.round = parseInt(slice(line, 19, 22));
      for (std::size_t end{ 27 }; end <= line.size(); end += 5)
      {
        const int team{ parseInt(slice(line, end - 4, end)) };
        abnormal.teams.push_back(team);
        checkPlayer(slice(line, 0, 3), team);
      }
      // A round-0 record without players sets the value of an unplayed
      // result in the match score system, which an individual event never
      // reads.
      if (abnormal.round == 0
        && (abnormal.teams.empty() || abnormal.teams[0] == 0))
      {
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
        parseScoreSystem(line);
      }
      else if (key == "192")
      {
        if (TEAM_CODES.count(upper(slice(line, 4))))
        {
          throw FileFormatException(
            "Team tournaments are not supported yet (record 192)");
        }
      }
      else if (key == "202" || key == "212")
      {
        const std::string prefix{ key == "202" ? "PTS " : "" };
        tournament.rankOrder =
          splitOnSpace(replaceAll(prefix + slice(line, 4), ',', ' '));
      }
      else if (key == "352")
      {
        throw FileFormatException(
          "Record 352 is only valid in a team tournament");
      }
      else if (key == "310" || key == "013" || key == "320" || key == "330")
      {
        throw FileFormatException(
          "Team tournaments are not supported yet (record " + key + ")");
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
      else if (key == "240")
      {
        parseBye(line);
      }
      else if (key == "299")
      {
        parseAbnormal(line);
      }
      else if (key == "XXZ")
      {
        parseAbsent(line);
      }
    }

    /**
     * scoresystem.fill_default_scoresystem: the defaults with the declared
     * points over them, every number rounded to the decimals of the most
     * precise of W, D and L.
     */
    ScoreSystem Reader::fillDefaultGameScore() const
    {
      ScoreSystem score{ defaultGameScore() };
      for (const auto &[key, entry] : declaredGameScore)
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
      ScoreSystem score{ fillDefaultGameScore() };
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
            entry = { true, letter, Decimal{ } };
            break;
          }
        }
      }
      declaredGameScore = score;
      score = fillDefaultGameScore();
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
          game.white = GameSide{ bye.competitor, bye.score };
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

    /** trf2json.update_abnormal_list for an individual tournament. */
    void Reader::updateAbnormals()
    {
      for (const Abnormal &abnormal : abnormals)
      {
        const char att{ upper(abnormal.att) };
        if (att != ' ' && abnormal.round == 0 && abnormal.teams.empty())
        {
          const std::string key{
            att == '+' ? "F" : att == '-' ? "Z" : std::string(1, att) };
          tournament.gameScore[key] = { false, "", abnormal.gamePoints };
        }
        else if (att != ' ' && abnormal.round > 0 && !abnormal.teams.empty())
        {
          for (const int team : abnormal.teams)
          {
            std::vector<GameSide *> sides{ };
            for (Game &game : tournament.gameList)
            {
              if (game.round == abnormal.round && game.white.cid == team)
              {
                sides.push_back(&game.white);
              }
            }
            for (Game &game : tournament.gameList)
            {
              if (game.round == abnormal.round && game.black
                && game.black->cid == team)
              {
                sides.push_back(&*game.black);
              }
            }
            if (sides.size() != 1)
            {
              throw FileFormatException(
                "Error in 299 Abnormal, Round = "
                  + std::to_string(abnormal.round) + ", player/team "
                  + std::to_string(team));
            }
            sides[0]->result =
              att == '-' ? "Z" : att == '+' ? "W" : std::string(1, att);
          }
        }
        else if (att == ' ' && !abnormal.teams.empty())
        {
          for (const int team : abnormal.teams)
          {
            for (Competitor &competitor : tournament.competitors)
            {
              if (competitor.cid == team)
              {
                competitor.adjust.push_back({
                  abnormal.round % 999,
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
      for (const std::string &key : RECORD_ORDER)
      {
        const auto found = lines.find(key);
        if (found == lines.end())
        {
          continue;
        }
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
      }
      if (players.empty())
      {
        throw FileFormatException("Error in trf, no 001 records");
      }
      for (const auto &[cid, competitor] : players)
      {
        tournament.competitors.push_back(competitor);
      }
      if (!lines.count("299"))
      {
        updateGameScore(lines.count("162") || lines.count("222"));
      }
      else
      {
        tournament.gameScore = fillDefaultGameScore();
      }
      updateByes();
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
      if (!found->second.isLetter)
      {
        return found->second.points;
      }
      result = found->second.letter;
    }
    throw FileFormatException("The score system has no points for " + result);
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
