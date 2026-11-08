"""Inline test cases: TRF features and error paths (part of bbpPairingsWebAssemblyPort, MIT).

Every input here is synthetic and built with tests/trf_builder.py, so each
case states exactly what it exercises. Each case is a dict:
  name     golden file name (under tests/golden/features or tests/golden/errors)
  steps    list of engine argument lists, run in one working directory
  inputs   {file name: bytes}
  outputs  files to capture after the run
  exit     allowed exit codes for every step
Exit codes (bbpPairings README): 0 ok, 1 no valid pairing, 2 unexpected error,
3 invalid request, 4 build/size limit, 5 file error.
"""

from __future__ import annotations

from trf_builder import Player, PointSystem, build, round_robin_history

HEADER = ["012 Synthetic test tournament", "XXR 3", "XXC white1"]


def base_players() -> list[Player]:
    """4 players, round 1 played (1-3 1-0, 4-2 1-0), round 2 to pair."""
    return [
        Player(1, 2400, [(3, "w", "1")]),
        Player(2, 2300, [(4, "b", "0")]),
        Player(3, 2200, [(1, "b", "0")]),
        Player(4, 2100, [(2, "w", "1")]),
    ]


def base(header=None, footer=None, players=None, **kw) -> bytes:
    return build(
        base_players() if players is None else players,
        HEADER if header is None else header,
        footer,
        **kw,
    )


def history_players(count=8, rounds=3, ratings_from=2500, step=50) -> list[Player]:
    games = round_robin_history(count, rounds)
    return [Player(pid, ratings_from - step * pid, games[pid]) for pid in range(1, count + 1)]


def case(name, steps, inputs=None, outputs=None, exit=(0,)):
    return {
        "name": name,
        "steps": steps,
        "inputs": inputs or {},
        "outputs": outputs or [],
        "exit": tuple(exit),
    }


def pair_and_check(name, data, systems=("dutch", "burstein"), pair_exit=(0,), check_exit=(0,)):
    """Pair the next round (stdout) and check the played rounds (checklist)."""
    out = []
    for system in systems:
        flag = "--" + system
        out.append(case("%s-%s-pair" % (name, system), [[flag, "in.trf", "-p", "-l", "in.list"]],
                        {"in.trf": data}, ["in.list"], pair_exit))
        out.append(case("%s-%s-check" % (name, system), [[flag, "in.trf", "-c", "-l"]],
                        {"in.trf": data}, ["in.list"], check_exit))
    return out


def pair_error(name, data, exit=(3,), mode="-p", system="--dutch"):
    return case(name, [[system, "in.trf", mode]], {"in.trf": data}, exit=exit)


# --- features -----------------------------------------------------------------


def feature_cases() -> list[dict]:
    cases = []
    hist = history_players()
    std_header = ["012 Synthetic feature tournament", "XXR 7"]

    cases += pair_and_check("history8-xxc-white1", build(hist, std_header + ["XXC white1"]))
    cases += pair_and_check("history8-xxc-black1", build(hist, std_header + ["XXC black1"]))
    cases += pair_and_check("history8-152-white", build(hist, std_header + ["152 W"]))
    cases += pair_and_check("history8-152-black", build(hist, std_header + ["152 B"]))
    cases += pair_and_check("history8-colour-inferred", build(hist, std_header))
    cases += pair_and_check("history8-142-rounds", build(hist, ["012 x", "142 7", "XXC white1"]))
    for code in ("FIDE_DUTCH_2025", "FIDE_DUTCH_2026", "FIDE_DUTCH"):
        cases += pair_and_check("history8-192-" + code, build(hist, std_header + ["XXC white1", "192 " + code]),
                                systems=("dutch",))
    cases += pair_and_check("history8-192-FIDE_BURSTEIN",
                            build(hist, std_header + ["XXC white1", "192 FIDE_BURSTEIN"]), systems=("burstein",))

    # Pairing numbers taken from the rank column (XXC rank).
    ranked = history_players()
    for p in ranked:
        p.rank = 9 - p.pid
    cases += pair_and_check("history8-xxc-rank", build(ranked, std_header + ["XXC rank white1"]))

    # Line endings: CRLF and CR only (the engine splits on both).
    cases += pair_and_check("history8-crlf", build(hist, std_header + ["XXC white1"], newline="\r\n"),
                            systems=("dutch",))
    cases += pair_and_check("history8-cr", build(hist, std_header + ["XXC white1"], newline="\r"),
                            systems=("dutch",))

    # Non-ASCII names (valid UTF-8).
    utf8 = history_players()
    utf8[0].name, utf8[1].name = "Núñez, José", "Müller, Jürgen"
    cases += pair_and_check("history8-utf8-names", build(utf8, std_header + ["XXC white1"]), systems=("dutch",))

    # Odd number of players, unrated players: pairing-allocated bye.
    odd = [Player(pid, None if pid in (6, 7) else 2400 - 30 * pid) for pid in range(1, 8)]
    cases += pair_and_check("round1-odd-unrated", build(odd, ["012 x", "XXR 5", "XXC white1"]))

    # Point systems: 162 line and BB* lines (scores computed accordingly).
    three = PointSystem(win=3.0, draw=1.0, loss=0.0, zero_bye=0.0, forfeit_loss=0.0, pab=3.0)
    cases += pair_and_check(
        "history8-162-points",
        build(hist, std_header + ["XXC white1", "162  W 3.0    D 1.0    L 0.0    Z 0.0    P 3.0"], system=three))
    cases += pair_and_check(
        "history8-162-points-A",
        build(hist, std_header + ["XXC white1", "162  W 3.0    D 1.0    A 0.0"],
              system=PointSystem(3.0, 1.0)))
    cases += pair_and_check(
        "history8-bb-points",
        build(hist, std_header + ["XXC white1", "BBW  3.0", "BBD  1.0", "BBL  0.0", "BBZ  0.0",
                                  "BBF  0.0", "BBU  3.0"], system=three))
    cases += pair_and_check(
        "history8-bb-pab-before-win",
        build(hist, std_header + ["XXC white1", "BBU  1.0", "BBW  2.0", "BBD  1.0"],
              system=PointSystem(2.0, 1.0, pab=1.0)))

    # Accelerations: 250 lines and XXA lines, round 1 and with history.
    fresh = [Player(pid, 2500 - 25 * pid) for pid in range(1, 13)]
    acc250 = ["250 %4s %4s %3d %3d %4d %4d" % ("", "1.0", 1, 2, 1, 6),
              "250 %4s %4s %3d %3d %4d %4d" % ("", "0.5", 3, 3, 1, 6),
              # overlaps the first line: overwrites round 2 for players 1-3
              "250 %4s %4s %3d %3d %4d %4d" % ("", "0.5", 2, 2, 1, 3)]
    cases += pair_and_check("round1-accel-250", build(fresh, ["012 x", "XXR 7", "XXC white1"] + acc250))
    cases += pair_and_check("history8-accel-250", build(hist, std_header + ["XXC white1"] + [
        "250 %4s %4s %3d %3d %4d %4d" % ("", "1.0", 1, 5, 1, 4)]))
    xxa = ["XXA %4d  1.0  1.0  0.5" % pid for pid in range(1, 7)]
    cases += pair_and_check("round1-accel-xxa", build(fresh, ["012 x", "XXR 7", "XXC white1"] + xxa))
    cases += pair_and_check("round1-accel-xxa-zero", build(fresh, ["012 x", "XXR 7", "XXC white1",
                                                                  "XXA    1  0.0"]))

    # Baku acceleration from the 192 tournament type.
    for code, systems in (("FIDE_DUTCH_BAKU", ("dutch",)), ("FIDE_DUTCH_2026_BAKU", ("dutch",)),
                          ("FIDE_DUTCH_2025_BAKU", ("dutch",)), ("FIDE_BURSTEIN_BAKU", ("burstein",))):
        cases += pair_and_check("round1-192-" + code,
                                build(fresh, ["012 x", "XXR 7", "XXC white1", "192 " + code]), systems=systems)
    cases += pair_and_check("history8-192-FIDE_DUTCH_BAKU",
                            build(hist, std_header + ["XXC white1", "192 FIDE_DUTCH_BAKU"]), systems=("dutch",))
    # Baku code followed by a non-Baku code: the last 192 line wins.
    cases += pair_and_check("round1-192-baku-then-plain",
                            build(fresh, ["012 x", "XXR 7", "XXC white1", "192 FIDE_DUTCH_BAKU",
                                          "192 FIDE_DUTCH"]), systems=("dutch",))

    # Forbidden pairs: 260 (round range) and XXP (whole tournament).
    cases += pair_and_check("history8-forbidden-260", build(hist, std_header + ["XXC white1",
                            "260 %3d %3d %4d %4d %4d" % (4, 7, 1, 2, 3)]))
    cases += pair_and_check("history8-forbidden-xxp", build(hist, std_header + ["XXC white1", "XXP 1 2",
                                                                               "XXP 3\t4 5"]))

    # Requested byes for the round being paired: 240 line and a pre-entered
    # column in the unpaired round.
    hist10 = history_players(count=10)
    cases += pair_and_check("history10-bye-240", build(hist10, std_header + ["XXC white1",
                            "240 H %3d %4d %4d" % (4, 2, 5)]))
    future = history_players(count=10)
    future[1].games.append((0, "-", "H"))
    future[4].games.append((0, "-", "Z"))
    cases += pair_and_check("history10-bye-column", build(future, std_header + ["XXC white1"]))

    # Result codes: forfeits, all bye kinds, W/D/L letters, withdrawn player.
    mixed = [
        Player(1, 2400, [(2, "w", "+"), (3, "b", "W"), (5, "w", "D")]),
        Player(2, 2350, [(1, "b", "-"), (0, "-", "H"), (4, "w", "L")]),
        Player(3, 2300, [(4, "w", "1"), (1, "w", "L"), (0, "-", "F")]),
        Player(4, 2250, [(3, "b", "0"), (5, "b", "="), (2, "b", "W")]),
        Player(5, 2200, [(0, "-", "U"), (4, "w", "="), (1, "b", "D")]),
        Player(6, 2150, [(7, "w", "d"), (0, "-", "Z"), (7, "b", "l")]),
        Player(7, 2100, [(6, "b", "d"), (0, "-", "Z"), (6, "w", "w")]),
        Player(8, 2050, [(9, "w", "1"), None, None]),
        Player(9, 2000, [(8, "b", "0"), (0, "-", " "), (0, "-", "U")]),
    ]
    cases += pair_and_check("history9-result-codes", build(mixed, ["012 x", "XXR 5", "XXC white1"]))

    # Rematch in the history: the checker reports that no valid pairing exists.
    rematch = [Player(1, 2400, [(2, "w", "1"), (2, "b", "0")]), Player(2, 2300, [(1, "b", "0"), (1, "w", "1")])]
    cases += pair_and_check("history2-rematch", build(rematch, ["012 x", "XXR 3", "XXC white1"]),
                            pair_exit=(1,))

    # No valid pairing for the next round (exit 1).
    two = [Player(1, 2400, [(2, "w", "1")]), Player(2, 2300, [(1, "b", "0")])]
    cases.append(pair_error("no-valid-pairing-dutch", build(two, ["012 x", "XXR 2", "XXC white1"]), exit=(1,)))
    cases.append(pair_error("no-valid-pairing-burstein", build(two, ["012 x", "XXR 2", "XXC white1"]),
                            exit=(1,), system="--burstein"))
    return cases


# --- limits -------------------------------------------------------------------


def limit_cases() -> list[dict]:
    """Cases for the engine built with Makefile LIMITS_VARS: max_players=40,
    max_points=99, max_rating=255, max_rounds=9 (all 8-bit types), without the
    checker and generator (engine_comparison=no)."""
    def small(players=None, header=None, footer=None, **kw):
        if players is None:
            players = base_players()
            for p in players:
                p.rating = 250 - 10 * p.pid
        return build(players, HEADER if header is None else header, footer, **kw)

    cases = [
        pair_error("small-tournament-pairs", small(), exit=(0,)),
        pair_error("rating-beyond-type", base(), exit=(4,)),
        pair_error("player-id-beyond-type",
                   small(players=[Player(1, 200), Player(300, 190)]), exit=(4,)),
        # 41 players: one more than max_players, but still within the 8-bit type.
        pair_error("player-count-above-max-players",
                   small(players=[Player(pid, 250 - pid) for pid in range(1, 42)],
                         header=["012 x", "XXR 5", "XXC white1"]), exit=(0, 4)),
        pair_error("score-beyond-type",
                   small(players=[Player(1, 200, points="30.0"), Player(2, 190)]), exit=(4,)),
        pair_error("rounds-beyond-type", small(header=["012 x", "XXR 300", "XXC white1"]), exit=(4,)),
    ]
    # Valid per-line scores whose sum overflows the 8-bit points type.
    wins = [Player(1, 200, [(2, "w", "1"), (3, "b", "1")], points=" 0.0"),
            Player(2, 190, [(1, "b", "0"), None]),
            Player(3, 180, [None, (1, "w", "0")]),
            Player(4, 170)]
    cases.append(pair_error("score-sum-beyond-type",
                            small(players=wins, header=["012 x", "XXR 5", "XXC white1", "BBW 20.0"]),
                            exit=(4,)))
    # Modes that this configuration does not include.
    cases.append(pair_error("checker-not-built", small(), mode="-c", exit=(3,)))
    cases.append(case("generator-not-built", [["--dutch", "-g", "-o", "o.trf", "-s", "1"]], exit=(3,)))
    return cases


# --- errors -------------------------------------------------------------------


def replace_player(index, **changes):
    players = base_players()
    for key, value in changes.items():
        setattr(players[index], key, value)
    return players


def raw_line_case(name, line, exit=(3,), mode="-p", header=None):
    data = base(header=header) + line.encode("utf-8") + b"\n"
    return pair_error(name, data, exit=exit, mode=mode)


def error_cases(fixtures) -> list[dict]:
    cases = []
    good = base()

    # Command line.
    cases.append(case("cli-no-arguments", [[]]))
    cases.append(case("cli-r-only", [["-r"]]))
    cases.append(case("cli-r-with-pairing", [["-r", "--dutch", "in.trf", "-p"]], {"in.trf": good}))
    cases.append(case("cli-unknown-system", [["--swiss", "in.trf", "-p"]], {"in.trf": good}, exit=(3,)))
    cases.append(case("cli-missing-mode", [["--dutch", "in.trf"]], {"in.trf": good}, exit=(3,)))
    cases.append(case("cli-unknown-mode", [["--dutch", "in.trf", "-x"]], {"in.trf": good}, exit=(3,)))
    cases.append(case("cli-extra-argument", [["--dutch", "in.trf", "-p", "out.txt", "extra"]],
                      {"in.trf": good}, exit=(3,)))
    cases.append(case("cli-check-with-output", [["--dutch", "in.trf", "-c", "out.txt"]],
                      {"in.trf": good}, exit=(3,)))
    cases.append(case("cli-generator-missing-output", [["--dutch", "-g"]], exit=(3,)))
    cases.append(case("cli-generator-model-and-config",
                      [["--dutch", "in.trf", "-g", "c.cfg", "-o", "out.trf"]],
                      {"in.trf": good, "c.cfg": b"PlayersNumber=10\n"}, exit=(3,)))
    cases.append(case("cli-system-only", [["--burstein"]], exit=(3,)))

    # Files.
    cases.append(case("file-missing-pair", [["--dutch", "missing.trf", "-p"]], exit=(5,)))
    cases.append(case("file-missing-check", [["--dutch", "missing.trf", "-c"]], exit=(5,)))
    cases.append(case("file-missing-model", [["--dutch", "missing.trf", "-g", "-o", "o.trf", "-s", "1"]], exit=(5,)))
    cases.append(case("file-missing-config", [["--dutch", "-g", "missing.cfg", "-o", "o.trf", "-s", "1"]], exit=(5,)))
    cases.append(case("file-directory-as-input", [["--dutch", "dir", "-p"]], {"dir/keep": b""}, exit=(5,)))
    cases.append(case("file-directory-as-config", [["--dutch", "-g", "dir", "-o", "o.trf", "-s", "1"]],
                      {"dir/keep": b""}, exit=(5,)))
    cases.append(case("file-output-unwritable", [["--dutch", "in.trf", "-p", "nodir/out.txt"]],
                      {"in.trf": good}, exit=(5,)))
    cases.append(case("file-generator-output-unwritable", [["--dutch", "-g", "-o", "nodir/o.trf", "-s", "1"]],
                      exit=(5,)))
    cases.append(case("file-checklist-unwritable-pair", [["--dutch", "in.trf", "-p", "-l", "nodir/x.list"]],
                      {"in.trf": good}))
    cases.append(case("file-checklist-unwritable-check", [["--dutch", "in.trf", "-c", "-l", "nodir/x.list"]],
                      {"in.trf": good}))
    cases.append(case("file-output-relative-to-input", [["--dutch", "sub/in.trf", "-p", "out.txt"]],
                      {"sub/in.trf": good}, outputs=["sub/out.txt"]))
    cases.append(case("file-checklist-default-name-check", [["--dutch", "in.trf", "-c", "-l"]],
                      {"in.trf": good}, outputs=["in.list"]))

    # Generator: seeds and configuration files.
    # The seed type is std::minstd_rand::result_type (64 bits on Linux x86-64).
    cases.append(case("generator-seed-out-of-range",
                      [["--dutch", "-g", "-o", "o.trf", "-s", "99999999999999999999999"]], exit=(4,)))
    cases.append(case("generator-seed-not-a-number", [["--dutch", "-g", "-o", "o.trf", "-s", "abc"]], exit=(2, 3)))
    config_errors = [
        ("unknown-parameter", "Players=10\n", (3,)),
        ("no-equals-sign", "PlayersNumber\n", (3,)),
        ("players-not-a-number", "PlayersNumber=ten\n", (3,)),
        ("players-out-of-range", "PlayersNumber=9999999\n", (4,)),
        ("draw-percentage-over-100", "DrawPercentage=101\n", (3,)),
        ("forfeit-rate-below-1", "ForfeitRate=0.5\n", (3,)),
        ("retired-rate-below-2", "RetiredRate=1\n", (3,)),
        ("half-point-bye-rate-below-1", "HalfPointByeRate=0\n", (3,)),
        ("half-point-byte-rate-alias", "HalfPointByteRate=0\n", (3,)),
        ("rating-range-inverted", "HighestRating=1500\nLowestRating=2000\n", (3,)),
        ("points-not-a-number", "PointsForWin=x\n", (3,)),
        ("score-overflow", "PlayersNumber=10\nRoundsNumber=9\nPointsForWin=999.9\nPointsForPAB=999.9\n", (4,)),
        ("rounds-out-of-range", "RoundsNumber=99999999999999999999999\n", (4,)),
    ]
    for name, content, exit in config_errors:
        cases.append(case("generator-config-" + name,
                          [["--dutch", "-g", "c.cfg", "-o", "o.trf", "-s", "3"]],
                          {"c.cfg": ("# comment line\n" + content).encode()}, exit=exit))
    cases.append(case("generator-config-cr-line-endings",
                      [["--dutch", "-g", "c.cfg", "-o", "o.trf", "-s", "3"]],
                      {"c.cfg": b"PlayersNumber=12\rRoundsNumber=5\r\r"}, outputs=["o.trf"]))
    unrated = base(players=replace_player(2, rating=None))
    cases.append(case("generator-model-unrated-player", [["--dutch", "in.trf", "-g", "-o", "o.trf", "-s", "1"]],
                      {"in.trf": unrated}, exit=(3,)))
    cases.append(case("generator-model-invalid", [["--dutch", "in.trf", "-g", "-o", "o.trf", "-s", "1"]],
                      {"in.trf": b"001    0\n"}, exit=(3,)))
    cases.append(case("generator-model-limit", [["--dutch", "in.trf", "-g", "-o", "o.trf", "-s", "1"]],
                      {"in.trf": base(footer=["XXP 99999"])}, exit=(4,)))

    # TRF syntax errors (one defect per case).
    cases.append(pair_error("trf-invalid-utf8", base() + b"012 \xff\xfe\n"))
    cases.append(pair_error("trf-player-line-too-short", base(footer=["001    5"])))
    cases.append(pair_error("trf-player-id-zero", base(players=base_players() + [Player(0, 2000)])))
    cases.append(pair_error("trf-player-id-not-a-number",
                            base() + b"001    x" + b" " * 76 + b"\n"))
    cases.append(pair_error("trf-rating-not-a-number",
                            base().replace(b"2400", b"24x0", 1)))
    cases.append(pair_error("trf-score-not-a-number", base(players=replace_player(0, points=" x.y"))))
    cases.append(pair_error("trf-score-out-of-range", base(players=replace_player(0, points="9999")), exit=(4,)))
    cases.append(pair_error("trf-score-mismatch", base(players=replace_player(0, points=" 0.5"))))
    cases.append(pair_error("trf-opponent-is-self", base(players=replace_player(0, games=[(1, "w", "1")]))))
    cases.append(pair_error("trf-opponent-not-a-number", base().replace(b"   3 w 1", b"  x3 w 1", 1)))
    cases.append(pair_error("trf-colour-invalid", base(players=replace_player(0, games=[(3, "x", "1")]))))
    cases.append(pair_error("trf-bye-with-colour", base(players=replace_player(0, games=[(0, "w", "=")]))))
    cases.append(pair_error("trf-result-invalid", base(players=replace_player(0, games=[(3, "w", "Q")]))))
    cases.append(pair_error("trf-result-non-ascii", base(players=replace_player(0, games=[(3, "w", "é")]))))
    cases.append(pair_error("trf-bye-result-with-opponent",
                            base(players=replace_player(0, games=[(3, "w", "H")]))))
    cases.append(pair_error("trf-played-result-without-colour",
                            base(players=replace_player(0, games=[(3, " ", "1")]))))
    cases.append(pair_error("trf-trailing-characters", base().replace(b"   3 w 1\n", b"   3 w 1  xx\n", 1)))
    cases.append(pair_error("trf-pairing-number-repeated", base(players=base_players() + [Player(1, 2000)])))
    cases.append(pair_error("trf-pairing-number-missing",
                            build([Player(1, 2000), Player(2, 1900), Player(4, 1800)], HEADER)))
    cases.append(pair_error("trf-pair-contradiction",
                            base(players=replace_player(2, games=[(2, "b", "0")]))))
    cases.append(pair_error("trf-pair-same-colour",
                            base(players=replace_player(2, games=[(1, "w", "0")]))))
    cases.append(pair_error("trf-team-013", base(footer=["013 Team A          1    2"])))
    cases.append(pair_error("trf-team-310", base(footer=["310    1 Team A"])))
    cases.append(pair_error("trf-299-abnormal-points", base(footer=["299 1"])))
    cases.append(pair_error("trf-192-too-short", base(footer=["192 "])))
    cases.append(pair_error("trf-192-unknown", base(footer=["192 FIDE_LIM"])))
    cases.append(pair_error("trf-system-mismatch-check", base(footer=["192 FIDE_BURSTEIN"]), mode="-c"))
    cases.append(pair_error("trf-152-wrong-length", base(header=["012 x", "XXR 3", "152 WB"])))
    cases.append(pair_error("trf-152-invalid", base(header=["012 x", "XXR 3", "152 X"])))
    cases.append(pair_error("trf-xxr-not-a-number", base(header=["012 x", "XXR many", "XXC white1"])))
    cases.append(pair_error("trf-xxr-zero", base(header=["012 x", "XXR 0", "XXC white1"])))
    cases.append(pair_error("trf-xxr-out-of-range",
                            base(header=["012 x", "XXR 99999999999999999999999", "XXC white1"]), exit=(4,)))
    cases.append(pair_error("trf-rounds-not-specified", base(header=["012 x", "XXC white1"])))
    cases.append(pair_error("trf-rounds-exceeded", base(header=["012 x", "XXR 1", "XXC white1"])))
    cases.append(pair_error("trf-no-initial-colour",
                            build([Player(1, 2000), Player(2, 1900)], ["012 x", "XXR 3"])))
    cases.append(pair_error("trf-empty-file", b""))
    cases.append(pair_error("trf-empty-file-check", b"", mode="-c", exit=(0,)))

    # 240 (byes), 250/XXA (accelerations), 260/XXP (forbidden pairs).
    cases.append(raw_line_case("trf-240-too-short", "240 H   2"))
    cases.append(raw_line_case("trf-240-trailing", "240 H   2    1  x"))
    cases.append(raw_line_case("trf-240-round-zero", "240 H   0    1"))
    cases.append(raw_line_case("trf-240-round-not-a-number", "240 H   x    1"))
    cases.append(raw_line_case("trf-240-later-round-ignored", "240 H 999    1", exit=(0,)))
    cases.append(raw_line_case("trf-250-too-short", "250      1.0   1   2    1"))
    cases.append(raw_line_case("trf-250-match-points", "250  1.0  1.0   1   2    1    2"))
    cases.append(raw_line_case("trf-250-game-points-zero", "250       0.0   1   2    1    2"))
    cases.append(raw_line_case("trf-250-rounds-inverted", "250       1.0   2   1    1    2"))
    cases.append(raw_line_case("trf-250-players-inverted", "250       1.0   1   2    2    1"))
    cases.append(raw_line_case("trf-250-more-than-rounds", "250       1.0   1   5    1    2"))
    cases.append(raw_line_case("trf-xxa-trailing", "XXA    1  1.0 x"))
    cases.append(raw_line_case("trf-xxa-more-than-rounds", "XXA    1  1.0  1.0  1.0  1.0"))
    cases.append(raw_line_case("trf-260-too-short", "260   1   2    1"))
    cases.append(raw_line_case("trf-260-trailing", "260   1   2    1    2  x"))
    cases.append(raw_line_case("trf-xxp-not-a-number", "XXP 1 two"))
    cases.append(raw_line_case("trf-xxp-out-of-range", "XXP 1 99999", exit=(4,)))
    cases.append(raw_line_case("trf-162-too-short", "162  W"))
    cases.append(raw_line_case("trf-162-invalid-symbol", "162  Q 1.0"))
    cases.append(raw_line_case("trf-162-x-symbol", "162  X 1.0"))
    cases.append(raw_line_case("trf-162-trailing", "162  W 1.0    x"))
    cases.append(raw_line_case("trf-bbw-too-short", "BBW 1"))
    cases.append(raw_line_case("trf-baku-inconsistent-points", "192 FIDE_DUTCH_BAKU",
                               header=["012 x", "XXR 3", "XXC white1", "162  W 3.0    D 1.0"]))
    return cases
