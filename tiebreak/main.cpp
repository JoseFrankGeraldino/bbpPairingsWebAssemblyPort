/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 *
 * Command line (a subset of Gacrux's tiebreakchecker, same meaning):
 *   tiebreak -i <file> [-t <descriptor>...] [-c] [-p | -s] [-u <rating>]
 *            [-n <round>] [-r] [-d T|S|C|B] [-o <file>]
 * Output is the tab-separated table tiebreakchecker writes with -d T.
 *
 * Exit codes: 0 success, 1 the -c check found a rank that differs from the
 * file, 2 unexpected error, 3 invalid file or request, 4 a number exceeds
 * what the tool can hold, 5 the input cannot be read or the output written.
 */

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiebreak.h"
#include "trfreader.h"
#include "value.h"

namespace
{
  const char *const USAGE{
    "Usage: tiebreak -i <file> [-t <descriptor>...] [-c] [-p | -s]\n"
    "                [-u <rating>] [-n <round>] [-r] [-d T|S|C|B]"
    " [-o <file>]\n" };

  class UsageException final : public std::runtime_error
  {
  public:
    explicit UsageException(const std::string &message)
      : std::runtime_error(message)
    { }
  };

  class FileException final : public std::runtime_error
  {
  public:
    explicit FileException(const std::string &message)
      : std::runtime_error(message)
    { }
  };

  struct Arguments
  {
    std::string inputFile{ };
    std::string outputFile{ "-" };
    std::string delimiter{ "\t" };
    bool rank{ };
    tiebreak::Options options{ };
  };

  int parseNumber(const std::string &text, const std::string &option)
  {
    try
    {
      std::size_t used{ };
      const int value{ std::stoi(text, &used) };
      if (used == text.size())
      {
        return value;
      }
    }
    catch (const std::logic_error &)
    {
    }
    throw UsageException("Option " + option + " needs a number: " + text);
  }

  /**
   * @throws UsageException for an unknown option or a missing value.
   */
  Arguments parseArguments(const std::vector<std::string> &args)
  {
    Arguments arguments{ };
    for (std::size_t i{ }; i < args.size(); ++i)
    {
      const std::string &arg{ args[i] };
      const auto value = [&]() -> std::string
      {
        if (i + 1 >= args.size())
        {
          throw UsageException("Option " + arg + " needs a value");
        }
        return args[++i];
      };
      if (arg == "-i" || arg == "--input-file")
      {
        arguments.inputFile = value();
      }
      else if (arg == "-o" || arg == "--output-file")
      {
        arguments.outputFile = value();
      }
      else if (arg == "-t" || arg == "--tiebreak")
      {
        while (i + 1 < args.size()
          && (args[i + 1].empty() || args[i + 1][0] != '-'))
        {
          arguments.options.tiebreaks.push_back(args[++i]);
        }
      }
      else if (arg == "-c" || arg == "--check")
      {
        arguments.options.check = true;
      }
      else if (arg == "-p" || arg == "--pre-determined")
      {
        arguments.options.preDetermined = true;
      }
      else if (arg == "-s" || arg == "--swiss")
      {
        arguments.options.swiss = true;
      }
      else if (arg == "-u" || arg == "--unrated")
      {
        arguments.options.unrated = parseNumber(value(), arg);
      }
      else if (arg == "-n" || arg == "--current-round")
      {
        arguments.options.currentRound = parseNumber(value(), arg);
      }
      else if (arg == "-r" || arg == "--rank")
      {
        arguments.rank = true;
      }
      else if (arg == "-d" || arg == "--delimiter")
      {
        const std::string delimiter{ value() };
        if (delimiter == "T")
        {
          arguments.delimiter = "\t";
        }
        else if (delimiter == "S")
        {
          arguments.delimiter = ";";
        }
        else if (delimiter == "C")
        {
          arguments.delimiter = ",";
        }
        else if (delimiter == "B")
        {
          arguments.delimiter = " ";
        }
        else
        {
          throw UsageException("Unknown delimiter: " + delimiter);
        }
      }
      else
      {
        throw UsageException("Unknown option: " + arg);
      }
    }
    if (arguments.inputFile.empty())
    {
      throw UsageException("Missing -i <file>");
    }
    return arguments;
  }

  /**
   * @throws FileException if the file cannot be read.
   */
  std::string readInput(const std::string &fileName)
  {
    if (fileName == "-")
    {
      return std::string(
        std::istreambuf_iterator<char>(std::cin),
        std::istreambuf_iterator<char>());
    }
    std::ifstream stream(fileName, std::ios::binary);
    if (!stream)
    {
      throw FileException("Can't open file: " + fileName);
    }
    std::ostringstream contents{ };
    contents << stream.rdbuf();
    if (stream.bad())
    {
      throw FileException("Error reading file: " + fileName);
    }
    return contents.str();
  }

  /** tiebreakchecker.write_text_file. */
  std::string formatResult(
    const tiebreak::Result &result,
    const Arguments &arguments)
  {
    std::vector<const tiebreak::CompetitorResult *> order{ };
    for (const tiebreak::CompetitorResult &competitor : result.competitors)
    {
      order.push_back(&competitor);
    }
    if (arguments.rank)
    {
      std::stable_sort(
        order.begin(),
        order.end(),
        [](const tiebreak::CompetitorResult *const a,
          const tiebreak::CompetitorResult *const b)
        {
          return a->rank != b->rank ? a->rank < b->rank : a->cid < b->cid;
        });
    }
    const std::string &delimiter{ arguments.delimiter };
    std::ostringstream out{ };
    out << (arguments.rank ? "Rank" : "StartNo") << delimiter
      << (arguments.rank ? "StartNo" : "Rank");
    for (const std::string &descriptor : result.tiebreaks)
    {
      out << delimiter << descriptor;
    }
    out << '\n';
    for (const tiebreak::CompetitorResult *const competitor : order)
    {
      out << (arguments.rank ? competitor->rank : competitor->cid)
        << delimiter << (arguments.rank ? competitor->cid : competitor->rank);
      for (const tiebreak::Value &value : competitor->scores)
      {
        out << delimiter << value.toString();
      }
      out << '\n';
    }
    if (arguments.options.check)
    {
      out << "Check: " << (result.check ? "True" : "False") << '\n';
    }
    return out.str();
  }

  /**
   * @throws FileException if the output cannot be written.
   */
  void writeOutput(const std::string &fileName, const std::string &text)
  {
    if (fileName == "-")
    {
      std::cout << text << std::flush;
      if (!std::cout)
      {
        throw FileException("Error writing the output");
      }
      return;
    }
    std::ofstream stream(fileName, std::ios::binary);
    stream << text;
    stream.close();
    if (!stream)
    {
      throw FileException("Error writing file: " + fileName);
    }
  }
}

int main(const int argc, char *const argv[])
{
  try
  {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help")
    {
      std::cout << USAGE;
      return args.empty() ? 3 : 0;
    }
    const Arguments arguments{ parseArguments(args) };
    const tiebreak::Tournament tournament{
      tiebreak::readTrf(readInput(arguments.inputFile)) };
    const tiebreak::Result result{
      tiebreak::computeTiebreaks(tournament, arguments.options) };
    writeOutput(arguments.outputFile, formatResult(result, arguments));
    return arguments.options.check && !result.check ? 1 : 0;
  }
  catch (const UsageException &exception)
  {
    std::cerr << exception.what() << '\n' << USAGE;
    return 3;
  }
  catch (const tiebreak::FileFormatException &exception)
  {
    std::cerr << exception.what() << '\n';
    return 3;
  }
  catch (const tiebreak::InvalidRequestException &exception)
  {
    std::cerr << exception.what() << '\n';
    return 3;
  }
  catch (const std::invalid_argument &exception)
  {
    std::cerr << exception.what() << '\n';
    return 3;
  }
  catch (const std::overflow_error &exception)
  {
    std::cerr << exception.what() << '\n';
    return 4;
  }
  catch (const FileException &exception)
  {
    std::cerr << exception.what() << '\n';
    return 5;
  }
  catch (const std::exception &exception)
  {
    std::cerr << "Unexpected error: " << exception.what() << '\n';
    return 2;
  }
}
