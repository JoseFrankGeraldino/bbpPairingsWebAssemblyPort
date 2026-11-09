"""Synthetic TRF inputs for the tie-break comparison (part of bbpPairingsWebAssemblyPort, MIT).

Each function returns one small, consistent tournament that exercises input
features the generated tournaments do not have: the records 042 (start date
formats and the 2024/2026 rules switch), 162, 202/212, 240, 299 and XXZ,
round robins (single, odd, double), unrated players and the unrated result
codes (W/D/L/X), byes, forfeits, CRLF line ends and a byte order mark.
tests/tiebreak_compare.py runs each through Gacrux and through our tool.
"""

from __future__ import annotations

from typing import Optional

from trf_builder import round_robin_history

# TRF result code -> score system letter (as Gacrux's trf2json reads them).
LETTER = {
    "1": "W", "W": "W", "+": "W", "F": "W",
    "=": "D", "D": "D", "H": "D",
    "0": "L", "L": "L",
    "U": "P",
    "X": "A", "?": "A",
    "-": "Z", "Z": "Z", " ": "Z",
}

DEFAULT_POINTS = {"W": 1.0, "D": 0.5, "L": 0.0, "P": 1.0, "A": 0.5, "Z": 0.0}


def player_line(
    pid: int,
    rating: Optional[int],
    games: list,
    points: dict[str, float],
    rank: int = 0,
    override: Optional[float] = None,
) -> str:
    """A 001 record; games are (opponent, colour, code) or None (empty)."""
    total = override
    if total is None:
        total = sum(points[LETTER[g[2]]] for g in games if g is not None)
    line = "001 %4d      %-33s %4s %3s %11s %10s %4.1f %4s" % (
        pid,
        "Player %04d" % pid,
        "" if rating is None else str(rating),
        "",
        "",
        "",
        total,
        str(rank) if rank else "",
    )
    for game in games:
        if game is None:
            line += " " * 10
        else:
            opponent, colour, code = game
            line += "  %s %s %s" % (
                "0000" if opponent == 0 else "%4d" % opponent, colour, code)
    return line.rstrip()


def join(lines: list[str], newline: str = "\n", bom: bool = False) -> bytes:
    data = (newline.join(lines) + newline).encode("latin-1")
    return (b"\xef\xbb\xbf" + data) if bom else data


def rated(pid: int) -> int:
    return 2400 - 37 * pid


def byes_absent_rank_order() -> bytes:
    """Swiss, 2025 start (2024 rules), byes in records 240, XXZ, 202."""
    history = round_robin_history(8, 3)
    games = {pid: list(history[pid]) for pid in history}
    # Round 4: three games, a half-point bye and a full-point bye. 1 and 2
    # meet a second time (direct encounter averages repeated games).
    for white, black, wres, bres in ((1, 2, "=", "="), (3, 4, "1", "0"),
                                     (5, 6, "0", "1")):
        games[white].append((black, "w", wres))
        games[black].append((white, "b", bres))
    games[7].append((0, "-", "H"))
    games[8].append((0, "-", "F"))
    lines = [
        "012 Byes, absences and a rank order record",
        "042 2025/09/01",
        "092 Swiss Dutch",
        "142 5",
        "202 BH SB DE",
    ]
    lines += [player_line(pid, rated(pid), games[pid], DEFAULT_POINTS)
              for pid in range(1, 9)]
    lines += ["240 H   4    7", "240 F   4    8", "XXZ 3, 5/6"]
    return join(lines)


def late_bye_abnormal() -> bytes:
    """A bye only in record 240, and record 299 assignments and penalties."""
    history = round_robin_history(8, 3)
    games = {pid: list(history[pid]) for pid in history}
    for white, black in ((1, 3), (2, 4), (5, 7)):
        games[white].append((black, "w", "1"))
        games[black].append((white, "b", "0"))
    games[6].append((0, "-", "-"))
    games[8].append(None)
    lines = [
        "012 Abnormal points",
        "042 01.03.2026",
        "212 PTS SB BH/C1 WIN",
    ]
    lines += [player_line(pid, rated(pid), games[pid], DEFAULT_POINTS)
              for pid in range(1, 9)]
    lines += [
        "240 F   4    8",
        "299 +   1.0   1.0",
        "299 W   1.0   1.0    2    3",
        "299    -1.0  -1.0    3    5",
        "299     0.0  -0.5  999    6",
    ]
    return join(lines)


def round_robin_odd() -> bytes:
    """Five players, round robin with a bye each round; some unrated."""
    history = round_robin_history(6, 5)
    games = {}
    for pid in range(1, 6):
        games[pid] = [
            (0, "-", "-") if game[0] == 6 else game for game in history[pid]
        ]
    lines = ["012 Odd round robin", "042 26/03/05", "092 Round Robin"]
    lines += [
        player_line(pid, None if pid in (2, 5) else rated(pid), games[pid],
                    DEFAULT_POINTS)
        for pid in range(1, 6)
    ]
    return join(lines, newline="\r\n", bom=True)


def double_round_robin() -> bytes:
    """Four players meeting twice; second cycle with colours reversed."""
    history = round_robin_history(4, 3)
    games = {pid: list(history[pid]) for pid in history}
    # Second cycle: the first cycle's white plays black; its result rotates.
    second = {"1": "=", "=": "0", "0": "1"}
    opposite = {"1": "0", "=": "=", "0": "1"}
    for round_index in range(3):
        for pid in range(1, 5):
            opponent, colour, code = history[pid][round_index]
            if colour == "w":
                games[pid].append((opponent, "b", second[code]))
                games[opponent].append((pid, "w", opposite[second[code]]))
    lines = ["012 Double round robin", "042 2026-04-10", "092 Double round robin"]
    lines += [player_line(pid, rated(pid), games[pid], DEFAULT_POINTS)
              for pid in range(1, 5)]
    return join(lines)


def declared_score_system() -> bytes:
    """Record 162 (3-1-0, PAB 1, X games 1) and every unplayed result code."""
    points = {"W": 3.0, "D": 1.0, "L": 0.0, "P": 1.0, "A": 1.0, "Z": 0.0}
    rounds = [
        # (white, black, white code, black code); black 0 = no opponent
        [(1, 2, "W", "L"), (3, 4, "D", "D"), (5, 6, "X", "X"), (7, 0, "U", "")],
        [(2, 3, "+", "-"), (4, 5, "L", "W"), (6, 7, "1", "0"), (1, 0, "H", "")],
        [(3, 1, "=", "="), (5, 2, "0", "1"), (7, 4, "D", "D"), (6, 0, "F", "")],
        [(1, 4, "1", "0"), (2, 6, "=", "="), (3, 7, "0", "1"), (5, 0, "Z", "")],
    ]
    games: dict[int, list] = {pid: [] for pid in range(1, 8)}
    for pairings in rounds:
        for white, black, wcode, bcode in pairings:
            if black == 0:
                games[white].append((0, "-", wcode))
            else:
                games[white].append((black, "w", wcode))
                games[black].append((white, "b", bcode))
    lines = [
        "012 Declared score system",
        "162 " + "".join(" %s%4.1f   " % (symbol, value)
                         for symbol, value in (("W", 3.0), ("D", 1.0),
                                               ("L", 0.0), ("P", 1.0),
                                               ("A", 0.0), ("X", 1.0))),
    ]
    lines += [player_line(pid, rated(pid) if pid != 4 else None, games[pid],
                          points)
              for pid in range(1, 8)]
    return join(lines)


def feature_inputs() -> list[tuple[str, bytes]]:
    return [
        ("byes-absent-rank-order", byes_absent_rank_order()),
        ("late-bye-abnormal", late_bye_abnormal()),
        ("round-robin-odd", round_robin_odd()),
        ("double-round-robin", double_round_robin()),
        ("declared-score-system", declared_score_system()),
    ]
