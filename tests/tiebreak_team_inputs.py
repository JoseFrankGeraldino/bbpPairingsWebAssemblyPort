"""Generated team tournaments for the tie-break comparison (part of bbpPairingsWebAssemblyPort, MIT).

The engine generates individual tournaments only, so the team events that
tests/tiebreak_compare.py runs through Gacrux and through our tool come from
here: seeded, deterministic TRF-2026 team files (Python's random.Random with
the seed, nothing else). Every file is consistent: the players' points add up
under the score system, teams are 1 to N, and every match has its boards.

Each tournament draws its own shape from the seed (Swiss or round robin,
single or double, team size 2-6, reserves, odd numbers of teams) and its own
records: 310 or the older 013, 352, 362, 192, 142, 042 on either side of the
2026 rules, 202/212, pairing-allocated byes in 320 (with or without "U" in
the player records), team byes in 240, forfeited matches in 330, forfeited
and double-forfeited boards, absent players, out-of-order line-ups in 300 and
the assignments and penalties of record 299. team_feature_inputs() forces one
feature each, so that every record is covered whatever the seeds give.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field, replace
from typing import Optional

from tiebreak_inputs import DEFAULT_POINTS, LETTER, join, player_line

SWISS_CODES = (
    None, "FIDE_TEAM_TYPEA_MP_GP", "FIDE_TEAM_TYPEB_GP_MP", "FIDE_TEAM_MP",
    "FIDE_TEAM_GP", "FIDE_TEAM", "FIDE_TEAM_BAKU", "CUSTOM_TEAM_SWISS",
)
ROUND_ROBIN_CODES = (
    None, "FIDE_TEAM_ROUNDROBIN", "BERGER_TEAM_ROUNDROBIN",
    "FIDE_TEAM_DOUBLEROUNDROBIN",
)
RANK_ORDERS = (
    "212 MPTS GPTS EDE BH:MP",
    "212 MPTS BH:MP/C1 SB:MP BC",
    "202 GPTS MPVGP TBR",
    "212 GPTS EGGSB/C1 BBE",
    "202 SSSC EDEBT",
)


@dataclass
class Options:
    """The shape and the records of one generated tournament."""

    teams: int
    size: int
    reserves: int
    round_robin: bool
    double: bool
    rounds: int
    team_record: str = "310"
    sequence: Optional[str] = None
    match_score: Optional[str] = None
    code: Optional[str] = None
    start: str = "2026/04/01"
    rank_order: Optional[str] = None
    pab: str = "320"            # "320" or "320U": byes also as "U" boards
    pab_points: Optional[tuple[float, float]] = None
    team_byes: float = 0.0      # chance of a 240 bye per round
    match_forfeits: float = 0.0  # chance of a 330 forfeited match
    board_forfeits: float = 0.0
    double_forfeits: float = 0.0
    absences: float = 0.0       # a board with no player
    out_of_order: float = 0.0   # a line-up not in roster order (record 300)
    abnormal: bool = False      # record 299 assignments and penalties
    unplayed_codes: float = 0.0  # W/D/L (unrated) result codes
    # Fixed board results of the matches of each round (the last pattern
    # repeats), from the side of the team with white on the W boards; the
    # other team gets the reverse.
    pattern: Optional[list[list[str]]] = None
    title: str = "Generated team tournament"
    extra: list[str] = field(default_factory=list)


def draw_options(rng: random.Random) -> Options:
    round_robin = rng.random() < 0.25
    double = round_robin and rng.random() < 0.3
    teams = rng.randint(4, 8 if round_robin else 12)
    if round_robin:
        rounds = (teams - 1 if teams % 2 == 0 else teams) * (2 if double else 1)
    else:
        rounds = rng.randint(3, min(9, teams - 1))
    return Options(
        teams=teams,
        size=rng.randint(2, 6),
        reserves=rng.randint(0, 2),
        round_robin=round_robin,
        double=double,
        rounds=rounds,
        team_record=rng.choice(("310", "310", "310", "013")),
        sequence=rng.choice((None, None, "WB", "BW", "WBBW", "BWWB")),
        match_score=rng.choice((None, None, "2.0 1.0 0.0", "3 1 0", "3.0 1.0 0.0")),
        code=rng.choice(ROUND_ROBIN_CODES if round_robin else SWISS_CODES),
        start=rng.choice(("2025/10/01", "2026/04/01", "2026/04/01")),
        rank_order=rng.choice((None, None, None) + RANK_ORDERS),
        pab=rng.choice(("320", "320U")),
        pab_points=rng.choice((None, None, (1.0, 2.0), (2.0, 2.5), (1.0, 0.0))),
        team_byes=rng.choice((0.0, 0.0, 0.15)),
        match_forfeits=rng.choice((0.0, 0.0, 0.1)),
        board_forfeits=rng.choice((0.0, 0.03, 0.08)),
        double_forfeits=rng.choice((0.0, 0.0, 0.03)),
        absences=rng.choice((0.0, 0.0, 0.04)),
        out_of_order=rng.choice((0.0, 0.0, 0.15)),
        abnormal=rng.random() < 0.2,
        unplayed_codes=rng.choice((0.0, 0.0, 0.05)),
    )


def berger_rounds(count: int) -> list[list[tuple[int, int]]]:
    """Round robin pairings by the circle method (teams 1..count, even)."""
    order = list(range(1, count + 1))
    rounds = []
    for number in range(count - 1):
        pairs = []
        for board in range(count // 2):
            a, b = order[board], order[count - 1 - board]
            pairs.append((a, b) if (board + number) % 2 == 0 else (b, a))
        rounds.append(pairs)
        order = [order[0], order[-1]] + order[1:-1]
    return rounds


class Tournament:
    def __init__(self, seed: int, options: Options) -> None:
        self.rng = random.Random(seed)
        if options.sequence:
            # Record 352 gives the colour of every board: the team size.
            options = replace(
                options, sequence=(options.sequence * options.size)[: options.size])
        self.options = options
        self.size = options.size
        roster = options.size + options.reserves
        self.rosters = {
            team: [(team - 1) * roster + slot + 1 for slot in range(roster)]
            for team in range(1, options.teams + 1)
        }
        self.players = [pid for team in self.rosters for pid in self.rosters[team]]
        self.rating = {
            pid: (None if self.rng.random() < 0.1 else self.rng.randint(1400, 2600))
            for pid in self.players
        }
        # pid -> round -> (opponent, colour, code) or None
        self.games: dict[int, dict[int, tuple[int, str, str]]] = {
            pid: {} for pid in self.players
        }
        self.match_points = {team: 0.0 for team in self.rosters}
        self.game_points = {team: 0.0 for team in self.rosters}
        self.met: set[tuple[int, int]] = set()
        self.pabs: dict[int, int] = {}       # round -> team
        self.byes: list[tuple[str, int, int]] = []  # (type, round, team)
        self.forfeited: list[tuple[str, int, int, int]] = []
        self.out_of_order: list[tuple[int, int, int, list[int]]] = []
        self.had_pab: set[int] = set()
        scores = (options.match_score or "2.0 1.0 0.0").split()
        self.match_value = {"W": float(scores[0]), "D": float(scores[1]),
                            "L": float(scores[2])}

    def line_up(self, team: int) -> list[int]:
        roster = self.rosters[team]
        chosen = sorted(self.rng.sample(range(len(roster)), self.size))
        return [roster[slot] for slot in chosen]

    def board_result(self, white: int, black: int) -> tuple[str, str]:
        rng = self.rng
        draw = rng.random()
        if draw < self.options.double_forfeits:
            return "-", "-"
        if draw < self.options.double_forfeits + self.options.board_forfeits:
            return ("+", "-") if rng.random() < 0.5 else ("-", "+")
        wr = self.rating[white] or 1800
        br = self.rating[black] or 1800
        expected = 1 / (1 + 10 ** ((br - wr) / 400))
        roll = rng.random()
        if roll < expected * 0.75:
            codes = ("1", "0")
        elif roll < expected * 0.75 + 0.3:
            codes = ("=", "=")
        else:
            codes = ("0", "1")
        if rng.random() < self.options.unplayed_codes:
            codes = tuple({"1": "W", "0": "L", "=": "D"}[code] for code in codes)
        return codes

    def play_match(self, rnd: int, white: int, black: int, first: bool) -> None:
        """white is the team with white on the boards the sequence says W.

        The first match of a round always has a game played on board 1: a
        round with no game played over the board is one the reader does not
        count as played (trf2json counts the team size per played round).
        """
        options = self.options
        rng = self.rng
        self.met.add((min(white, black), max(white, black)))
        sequence = options.sequence or "WB"
        whites = self.line_up(white)
        blacks = self.line_up(black)
        # The board order of record 300 replaces the roster order; it is also
        # what places the boards of a match with an empty board.
        reordered = rng.random() < options.out_of_order
        if reordered:
            rng.shuffle(whites)
            rng.shuffle(blacks)
        orders = {white: whites[:], black: blacks[:]}
        forfeited = not first and rng.random() < options.match_forfeits
        losing = rng.choice((white, black))
        points = {white: 0.0, black: 0.0}
        for board in range(self.size):
            a, b = whites[board], blacks[board]
            if sequence[board % len(sequence)] == "W":
                wp, bp = a, b
            else:
                wp, bp = b, a
            if forfeited:
                lost = wp if (losing == white) == (wp == a) else bp
                codes = ("-", "+") if lost == wp else ("+", "-")
            elif board > 0 and rng.random() < options.absences:
                # One side has nobody on this board.
                present = rng.choice((wp, bp))
                self.games[present][rnd] = (0, "-", "+")
                points[white if present == a else black] += 1.0
                orders[black if present == a else white][board] = 0
                reordered = True
                continue
            elif options.pattern:
                own = options.pattern[min(rnd, len(options.pattern)) - 1][board]
                other = {"1": "0", "0": "1", "=": "="}[own]
                codes = (own, other) if wp == a else (other, own)
            elif first and board == 0:
                codes = rng.choice((("1", "0"), ("=", "="), ("0", "1")))
            else:
                codes = self.board_result(wp, bp)
            self.games[wp][rnd] = (bp, "w", codes[0])
            self.games[bp][rnd] = (wp, "b", codes[1])
            points[white if wp == a else black] += DEFAULT_POINTS[LETTER[codes[0]]]
            points[black if bp == b else white] += DEFAULT_POINTS[LETTER[codes[1]]]
        if forfeited:
            self.forfeited.append(
                ("+-" if losing == black else "-+", rnd, white, black))
        if reordered:
            self.out_of_order.append((rnd, white, black, orders[white]))
            self.out_of_order.append((rnd, black, white, orders[black]))
        self.game_points[white] += points[white]
        self.game_points[black] += points[black]
        if points[white] > points[black]:
            self.match_points[white] += self.match_value["W"]
            self.match_points[black] += self.match_value["L"]
        elif points[white] < points[black]:
            self.match_points[black] += self.match_value["W"]
            self.match_points[white] += self.match_value["L"]
        else:
            self.match_points[white] += self.match_value["D"]
            self.match_points[black] += self.match_value["D"]

    def give_pab(self, rnd: int, team: int) -> None:
        self.pabs[rnd] = team
        self.had_pab.add(team)
        if self.options.pab == "320U":
            for pid in self.line_up(team):
                self.games[pid][rnd] = (0, "-", "U")

    def swiss_round(self, rnd: int) -> None:
        rng = self.rng
        teams = list(self.rosters)
        rng.shuffle(teams)
        teams.sort(key=lambda t: (-self.match_points[t], -self.game_points[t]))
        if rng.random() < self.options.team_byes and len(teams) > 3:
            team = teams.pop(rng.randrange(len(teams)))
            self.byes.append((rng.choice("HFZ"), rnd, team))
        if len(teams) % 2:
            candidates = [t for t in reversed(teams) if t not in self.had_pab]
            team = candidates[0] if candidates else teams[-1]
            teams.remove(team)
            self.give_pab(rnd, team)
        first = True
        while teams:
            a = teams.pop(0)
            partner = next(
                (t for t in teams if (min(a, t), max(a, t)) not in self.met),
                teams[0])
            teams.remove(partner)
            if rng.random() < 0.5:
                self.play_match(rnd, a, partner, first)
            else:
                self.play_match(rnd, partner, a, first)
            first = False

    def run(self) -> None:
        options = self.options
        if options.round_robin:
            count = options.teams + options.teams % 2
            cycle = berger_rounds(count)
            schedule = cycle + ([[(b, a) for a, b in pairs] for pairs in cycle]
                                if options.double else [])
            for rnd, pairs in enumerate(schedule[: options.rounds], start=1):
                first = True
                for a, b in pairs:
                    if b > options.teams:
                        self.byes.append(("Z", rnd, a))
                    elif a > options.teams:
                        self.byes.append(("Z", rnd, b))
                    else:
                        self.play_match(rnd, a, b, first)
                        first = False
        else:
            for rnd in range(1, options.rounds + 1):
                self.swiss_round(rnd)

    def lines(self) -> list[str]:
        options = self.options
        rounds = options.rounds
        lines = ["012 " + options.title, "042 " + options.start]
        lines.append("092 " + ("Team Round Robin" if options.round_robin else "Team Swiss"))
        lines.append("142 %d" % rounds)
        if options.code:
            lines.append("192 " + options.code)
        if options.rank_order:
            lines.append(options.rank_order)
        if options.sequence:
            lines.append("352 " + options.sequence)
        if options.match_score:
            symbols = ("W", "D", "L")
            lines.append("362 " + "".join(
                "T%s%-4s   " % (symbol, value)
                for symbol, value in zip(symbols, options.match_score.split())).rstrip())
        lines.extend(options.extra)
        order = sorted(self.rosters, key=lambda t: (-self.match_points[t], -self.game_points[t], t))
        team_rank = {team: position + 1 for position, team in enumerate(order)}
        player_points = {}
        for pid in self.players:
            games = [self.games[pid].get(rnd) for rnd in range(1, rounds + 1)]
            player_points[pid] = sum(
                DEFAULT_POINTS[LETTER[game[2]]] for game in games if game is not None)
            lines.append(player_line(
                pid, self.rating[pid], games, DEFAULT_POINTS,
                rank=self.players.index(pid) + 1))
        for team, roster in self.rosters.items():
            name = "Team %02d" % team
            if options.team_record == "310":
                lines.append(
                    "310 %3d %-32s %5s %6s %6.1f %6.1f %3d  " % (
                        team, name, "", "", self.match_points[team],
                        sum(player_points[pid] for pid in roster), team_rank[team])
                    + " ".join("%4d" % pid for pid in roster))
            else:
                lines.append("013 %-32s" % name + " ".join("%4d" % pid for pid in roster))
        for kind in "HFZ":
            for rnd in range(1, rounds + 1):
                teams = [team for (t, r, team) in self.byes if t == kind and r == rnd]
                if teams:
                    lines.append("240 %s %3d " % (kind, rnd)
                                 + " ".join("%4d" % team for team in teams))
        if self.pabs:
            mp, gp = options.pab_points or (1.0, float(self.size))
            line = "320 %4.1f %4.1f " % (mp, gp)
            line += " ".join(
                "%3d" % self.pabs[rnd] if rnd in self.pabs else "   "
                for rnd in range(1, rounds + 1))
            lines.append(line.rstrip())
        for kind, rnd, white, black in self.forfeited:
            lines.append("330 %s %3d %3d %3d" % (kind, rnd, white, black))
        for rnd, team, other, order in self.out_of_order:
            lines.append("300 %3d %3d %3d " % (rnd, team, other)
                         + " ".join("%4d" % pid for pid in order))
        if options.abnormal:
            teams = sorted(self.rosters)
            penalised = self.rng.choice(teams)
            lines.append("299    -1.0  -1.0  %3d %4d" % (
                self.rng.randint(1, rounds), penalised))
            lines.append("299     0.0  -%3.1f  999 %4d" % (
                self.rng.choice((0.5, 1.0, 2.0)), self.rng.choice(teams)))
        return lines


def team_tournament(seed: int, options: Optional[Options] = None) -> bytes:
    rng = random.Random(seed * 7919 + 17)
    tournament = Tournament(seed, options or draw_options(rng))
    tournament.run()
    return join(tournament.lines())


def generated_team_inputs(count: int) -> list[tuple[str, bytes]]:
    return [("seed-%04d" % seed, team_tournament(seed)) for seed in range(1, count + 1)]


def team_feature_inputs() -> list[tuple[str, bytes]]:
    """One tournament per feature, whatever the seeds of the generator draw."""
    base = dict(teams=7, size=4, reserves=1, round_robin=False, double=False,
                rounds=5)
    features = {
        "team-013-records": Options(**base, team_record="013"),
        "team-pab-u-boards": Options(**base, pab="320U", pab_points=(2.0, 2.5),
                                     match_score="3 1 0"),
        "team-byes-and-forfeits": Options(**base, team_byes=0.4,
                                          match_forfeits=0.3, board_forfeits=0.1,
                                          double_forfeits=0.05),
        "team-absent-boards": Options(**base, absences=0.15),
        "team-out-of-order": Options(**base, out_of_order=0.5, sequence="BW"),
        "team-abnormal": Options(
            **base, abnormal=True, code="FIDE_TEAM_GP",
            extra=["250  1.0  2.0   1   2    1    2", "260   1   3    1    2"]),
        # Two teams tied in both scores after two drawn matches, so that the
        # board criteria of EDE (13.3.2) decide: from the top board, from the
        # bottom boards, and not at all (the second match mirrors the first).
        "team-board-criteria-top": Options(
            teams=2, size=4, reserves=0, round_robin=True, double=True,
            rounds=2, pattern=[["1", "0", "=", "="], ["=", "=", "=", "="]]),
        "team-board-criteria-bottom": Options(
            teams=2, size=4, reserves=1, round_robin=True, double=True,
            rounds=2, pattern=[["=", "=", "1", "0"], ["=", "=", "=", "="]],
            sequence="WB"),
        "team-board-criteria-even": Options(
            teams=2, size=4, reserves=0, round_robin=True, double=True,
            rounds=2, pattern=[["1", "0", "=", "1"]]),
        "team-old-rules": Options(**base, start="2025/09/01",
                                  code="FIDE_TEAM_TYPEB_GP_MP"),
        "team-round-robin-odd": Options(teams=5, size=3, reserves=0,
                                        round_robin=True, double=False, rounds=5,
                                        code="FIDE_TEAM_ROUNDROBIN"),
        "team-double-round-robin": Options(teams=4, size=2, reserves=1,
                                           round_robin=True, double=True,
                                           rounds=6),
        "team-unrated-codes": Options(**base, unplayed_codes=0.2,
                                      rank_order=RANK_ORDERS[0]),
    }
    return [(name, team_tournament(1000 + number, options))
            for number, (name, options) in enumerate(features.items())]
