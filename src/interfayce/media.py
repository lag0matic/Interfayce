"""Platform-neutral media models used by the cockpit."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class MediaTrack:
    artist: str
    title: str
    source_id: str

    @property
    def key(self) -> str:
        return f"{self.source_id}\x1f{self.artist.casefold()}\x1f{self.title.casefold()}"

