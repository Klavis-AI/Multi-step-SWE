"""Scenario registry."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

_SCENARIOS: list["Scenario"] = []


@dataclass
class Scenario:
    sid: str
    fn: Callable
    tags: tuple[str, ...] = ()
    step: int = 1
    needs: tuple[str, ...] = ()          # capabilities: "federation", "restart", ...


def scenario(sid: str, *, tags=(), step: int = 1, needs=()):
    def deco(fn):
        for existing in _SCENARIOS:
            if existing.sid == sid:
                raise ValueError(
                    f"duplicate scenario id {sid!r}: {existing.fn.__module__}."
                    f"{existing.fn.__name__} already uses it, and the recorded "
                    f"transcripts are named after the id, so one would silently "
                    f"overwrite the other")
        _SCENARIOS.append(Scenario(sid=sid, fn=fn, tags=tuple(tags), step=step,
                                   needs=tuple(needs)))
        return fn
    return deco


def all_scenarios() -> list[Scenario]:
    return list(_SCENARIOS)


def reset() -> None:
    _SCENARIOS.clear()
