"""Build small synthetic TRF files for tests (part of bbpPairingsWebAssemblyPort, MIT).

Column layout follows TRF16 / TRF-2026 (the 001 player line): the fields the
engine reads are id (cols 5-8), rating (49-52), points (81-84), rank (86-89)
and one 10-character block per round from col 90 (opponent, colour, result).

Scores are computed with the same rules the engine uses to validate them
(tournament::Tournament::getPoints), so a built file is consistent unless a
test deliberately overrides the score.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

WIN_RESULTS = "1W+FU"
DRAW_RESULTS = "=DH"


@dataclass
class PointSystem:
    win: float = 1.0
    draw: float = 0.5
    loss: float = 0.0
    zero_bye: float = 0.0
    forfeit_loss: float = 0.0
    pab: Optional[float] = None  # None = same as win

    def points(self, own_id: int, opponent: int, result: str) -> float:
        result = result.upper()
        opponent_is_self = opponent in (0, own_id)
        participated = (not opponent_is_self) or result in "U+"
        played = not opponent_is_self and result not in "+-HFUZ "
        if result in DRAW_RESULTS:
            return self.draw
        if result in WIN_RESULTS:
            if opponent_is_self and participated:
                return self.win if self.pab is None else self.pab
            return self.win
        # loss: '0', 'L', '-', 'Z', ' '
        if participated:
            return self.loss if played else self.forfeit_loss
        return self.zero_bye


@dataclass
class Player:
    pid: int
    rating: Optional[int] = None
    # One (opponent, colour, result) per round; opponent 0 = no opponent,
    # None = empty block (round not played / not yet paired).
    games: list = field(default_factory=list)
    name: Optional[str] = None
    rank: Optional[int] = None
    points: Optional[str] = None  # override the computed score (4 chars)
    title: str = ""
    sex: str = ""
    federation: str = ""
    fide_id: str = ""
    birth: str = ""


def game_block(game) -> str:
    if game is None:
        return " " * 10
    opponent, colour, result = game
    opp = "0000" if opponent == 0 else "%4d" % opponent
    return "  %s %s %s" % (opp, colour, result)


def player_line(player: Player, system: PointSystem) -> str:
    if player.points is not None:
        points = player.points
    else:
        total = sum(
            system.points(player.pid, g[0], g[2]) for g in player.games if g is not None
        )
        points = "%4.1f" % total
    line = "001 %4d %1s%3s %-33s %4s %3s %11s %10s %4s %4s" % (
        player.pid,
        player.sex,
        player.title,
        player.name or "Player %04d" % player.pid,
        "" if player.rating is None else str(player.rating),
        player.federation,
        player.fide_id,
        player.birth,
        points,
        "" if player.rank is None else str(player.rank),
    )
    line += "".join(game_block(g) for g in player.games)
    return line.rstrip()


def build(
    players: list[Player],
    header: Optional[list[str]] = None,
    footer: Optional[list[str]] = None,
    system: Optional[PointSystem] = None,
    newline: str = "\n",
) -> bytes:
    system = system or PointSystem()
    lines = list(header or [])
    lines += [player_line(p, system) for p in players]
    lines += list(footer or [])
    return (newline.join(lines) + newline).encode("utf-8")


def round_robin_history(count: int, rounds: int):
    """Deterministic, valid game histories for `count` players (count even).

    Uses the circle method, so nobody meets twice; colours alternate, and the
    higher-numbered player of each pair wins every third game, else a draw or
    a win for the lower number.
    """
    assert count % 2 == 0 and rounds < count
    games = {pid: [] for pid in range(1, count + 1)}
    ring = list(range(2, count + 1))
    for rnd in range(rounds):
        order = [1] + ring
        for board in range(count // 2):
            a, b = order[board], order[count - 1 - board]
            white, black = (a, b) if (rnd + board) % 2 == 0 else (b, a)
            pattern = (rnd + board + white) % 3
            res_w, res_b = (("1", "0"), ("=", "="), ("0", "1"))[pattern]
            games[white].append((black, "w", res_w))
            games[black].append((white, "b", res_b))
        ring = ring[-1:] + ring[:-1]
    return games
