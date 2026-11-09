#!/usr/bin/env python3
"""Generate tiebreak/fidetables.cpp from Gacrux's gacrux/fidetables.py.

Part of bbpPairingsWebAssemblyPort (MIT). Python 3 standard library only.
The tables are FIDE Handbook B.02 tables 8.1a and 8.1b as Gacrux transcribes
them (MIT, Copyright (c) 2024 FIDE). Run after a Gacrux pin bump:

    python3 tiebreak/gentables.py build/gacrux/gacrux > tiebreak/fidetables.cpp
"""

import sys


def array(name: str, values: list[int]) -> str:
    out = ["  const std::array<int, %d> %s{ {" % (len(values), name)]
    line = "   "
    items = [str(value) for value in values]
    for index, item in enumerate(items):
        piece = " " + item + ("," if index < len(items) - 1 else "")
        if len(line) + len(piece) > 78:
            out.append(line)
            line = "   "
        line += piece
    out.append(line)
    out.append("  } };")
    return "\n".join(out)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    sys.path.insert(0, sys.argv[1])
    import fidetables  # noqa: E402  (gacrux/fidetables.py)

    print("""/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tables are FIDE Handbook B.02 tables 8.1a and 8.1b as
 * transcribed in Gacrux (TieBreakServer, Copyright (c) 2024 FIDE, MIT
 * licence, https://www.gacrux.no); see THIRD_PARTY_NOTICES.md.
 * Generated from gacrux/fidetables.py; do not edit by hand.
 */

#include <array>

#include "fidetables.h"

namespace tiebreak
{""")
    print("  // Rating difference (0 to 800) to the expected score, in percent.")
    print(array("DIFF_TO_PD", fidetables.DiffToPd))
    print()
    print("  // Percentage score (0 to 100) to the rating difference dp.")
    print(array("SCORE_TO_DP", fidetables.ScoreToDp))
    print("}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
