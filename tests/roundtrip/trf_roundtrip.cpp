// trf-roundtrip: white-box round-trip check of the engine's TRF reader and
// writer. Part of bbpPairingsWebAssemblyPort (MIT). It links the engine's own
// objects unchanged and only calls the public functions declared in
// third_party/bbpPairings/src/fileformats/trf.h.
//
// The bbpPairings CLI has no "read a TRF and write it back" mode, so this is the
// one place where the suite needs white-box access.
//
// Usage: trf-roundtrip <file.trf>
// Prints a deterministic report on stdout (compared with a golden file):
//   1. read the file (as the checker does);
//   2. canonical write (the writer used for generated tournaments), read that
//      back, write again: the two canonical writes must be identical, which
//      proves read -> write -> read loses nothing the engine models;
//   3. model-preserving write (the writer used with a model file): the lines of
//      the input that do not survive verbatim are listed, documenting which
//      fields the writer does not reproduce; that output is read back and must
//      give the same tournament as step 1.
// Exit code: 0 when the report was produced and every check that could run
// passed, 1 when a check failed, 2 on usage errors.

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fileformats/trf.h>
#include <fileformats/types.h>
#include <swisssystems/common.h>
#include <tournament/tournament.h>

namespace
{
  /**
   * A file without a 192 line reads with no pairing system; the CLI always
   * supplies one (--dutch). Do the same, so the canonical writer can run.
   * Returns true when the system was assumed.
   */
  bool assumeDutchIfUnset(tournament::Tournament &tournament)
  {
    if (tournament.swissSystem == swisssystems::NONE)
    {
      tournament.swissSystem = swisssystems::DUTCH;
      return true;
    }
    return false;
  }

  std::string readAll(const std::string &path)
  {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
  }

  /** Split on CR, LF or CRLF; empty lines are dropped. */
  std::vector<std::string> splitLines(const std::string &text)
  {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text)
    {
      if (c == '\r' || c == '\n')
      {
        if (!current.empty())
        {
          lines.push_back(current);
        }
        current.clear();
      }
      else
      {
        current += c;
      }
    }
    if (!current.empty())
    {
      lines.push_back(current);
    }
    return lines;
  }

  /** Visible form of a line: trailing spaces shown as '~' so diffs are clear. */
  std::string show(const std::string &line)
  {
    std::string::size_type end = line.find_last_not_of(' ');
    std::string result = end == std::string::npos ? "" : line.substr(0, end + 1);
    result.append(line.size() - result.size(), '~');
    return result;
  }

  /**
   * Read a TRF from text. Returns an empty string on success, otherwise a
   * description of the error.
   */
  std::string tryRead(
    const std::string &text,
    tournament::Tournament &result,
    fileformats::trf::FileData *data = nullptr)
  {
    try
    {
      std::istringstream stream(text);
      result = fileformats::trf::readFile(stream, false, data);
      return "";
    }
    catch (const fileformats::FileFormatException &exception)
    {
      return std::string("format error: ") + exception.what();
    }
    catch (const fileformats::FileReaderException &exception)
    {
      return std::string("reader error: ") + exception.what();
    }
    catch (const tournament::BuildLimitExceededException &exception)
    {
      return std::string("build limit: ") + exception.what();
    }
  }

  /** Canonical write. Returns an empty string on success, else the error. */
  std::string tryCanonicalWrite(
    const tournament::Tournament &tournament,
    std::string &output)
  {
    try
    {
      std::ostringstream stream;
      fileformats::trf::writeSeed(stream, 0u);
      fileformats::trf::writeFile(stream, tournament);
      output = stream.str();
      return "";
    }
    catch (const fileformats::LimitExceededException &exception)
    {
      return std::string("unsupported: ") + exception.what();
    }
  }
}

int main(const int argc, char **const argv)
{
  if (argc != 2)
  {
    std::cerr << "Usage: trf-roundtrip <file.trf>" << std::endl;
    return 2;
  }
  const std::string input = readAll(argv[1]);
  bool failed = false;

  std::cout << "file: " << std::filesystem::path(argv[1]).filename().string()
    << '\n';

  // 1. Read.
  tournament::Tournament original;
  fileformats::trf::FileData data;
  const std::string readError = tryRead(input, original, &data);
  if (!readError.empty())
  {
    std::cout << "read: " << readError << '\n'
      << "round-trip: not possible, the engine cannot read this file\n";
    return 0;
  }
  std::cout << "read: ok (" << original.players.size() << " player slots, "
    << static_cast<unsigned long>(original.playedRounds) << " rounds played, "
    << static_cast<unsigned long>(original.expectedRounds)
    << " rounds expected)\n"
    << "system: "
    << (assumeDutchIfUnset(original)
          ? "none in file, Dutch assumed (as with --dutch)"
          : "from file")
    << '\n';

  // 2. Canonical write -> read -> write.
  std::string canonical1;
  const std::string writeError = tryCanonicalWrite(original, canonical1);
  if (!writeError.empty())
  {
    std::cout << "canonical-write: " << writeError << '\n';
  }
  else
  {
    tournament::Tournament reread;
    const std::string rereadError = tryRead(canonical1, reread);
    assumeDutchIfUnset(reread);
    std::string canonical2;
    if (!rereadError.empty())
    {
      std::cout << "canonical-reread: " << rereadError << '\n';
      failed = true;
    }
    else if (!tryCanonicalWrite(reread, canonical2).empty())
    {
      std::cout << "canonical-rewrite: failed\n";
      failed = true;
    }
    else
    {
      const bool stable = canonical1 == canonical2;
      std::cout << "canonical-write: ok\n"
        << "canonical-stable: " << (stable ? "yes" : "NO") << '\n';
      failed = failed || !stable;
    }
  }

  // 3. Model-preserving write (lines of the original file kept, games and
  //    scores replaced from the tournament model).
  std::string modelOutput;
  {
    std::ostringstream stream;
    fileformats::trf::writeFile(stream, original, std::move(data));
    modelOutput = stream.str();
  }
  const std::vector<std::string> inLines = splitLines(input);
  const std::vector<std::string> outLines = splitLines(modelOutput);
  const std::multiset<std::string> inSet(inLines.begin(), inLines.end());
  const std::multiset<std::string> outSet(outLines.begin(), outLines.end());
  std::vector<std::string> removed;
  std::vector<std::string> added;
  for (const std::string &line : inLines)
  {
    if (!outSet.count(line))
    {
      removed.push_back(line);
    }
  }
  for (const std::string &line : outLines)
  {
    if (!inSet.count(line))
    {
      added.push_back(line);
    }
  }
  std::cout << "model-write: " << inLines.size() << " input lines, "
    << outLines.size() << " output lines, " << removed.size()
    << " not reproduced verbatim\n";
  for (const std::string &line : removed)
  {
    std::cout << "  - " << show(line) << '\n';
  }
  for (const std::string &line : added)
  {
    std::cout << "  + " << show(line) << '\n';
  }

  tournament::Tournament modelReread;
  const std::string modelError = tryRead(modelOutput, modelReread);
  if (!modelError.empty())
  {
    std::cout << "model-reread: " << modelError << '\n';
    failed = true;
  }
  else if (writeError.empty())
  {
    std::string canonical3;
    assumeDutchIfUnset(modelReread);
    const bool same =
      tryCanonicalWrite(modelReread, canonical3).empty()
        && canonical3 == canonical1;
    std::cout << "model-reread-equal: " << (same ? "yes" : "NO") << '\n';
    failed = failed || !same;
  }
  else
  {
    std::cout << "model-reread: ok (equality not checked, canonical write "
      "unsupported)\n";
  }

  return failed ? 1 : 0;
}
