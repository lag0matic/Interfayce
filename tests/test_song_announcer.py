import unittest
import time

from interfayce.media import MediaTrack
from interfayce.song_announcer import (
    MAX_CHATBOX_CHARACTERS,
    SongAnnouncement,
    SongAnnouncementGate,
    ResidentSongAnnouncer,
    StableSongChangeWatcher,
)


class SongAnnouncementTests(unittest.TestCase):
    def test_formats_the_intended_chatbox_text(self) -> None:
        song = SongAnnouncement(artist="Ghost", title="Witch Image")

        self.assertEqual(song.chatbox_text(), "♫ Ghost — Witch Image")

    def test_normalizes_whitespace(self) -> None:
        song = SongAnnouncement(artist="Ghost  ", title="  Witch\nImage")

        self.assertEqual(song.chatbox_text(), "♫ Ghost — Witch Image")

    def test_shortens_a_long_title(self) -> None:
        song = SongAnnouncement(artist="Artist", title="x" * 300)

        result = song.chatbox_text()
        self.assertLessEqual(len(result), MAX_CHATBOX_CHARACTERS)
        self.assertTrue(result.endswith("…"))

    def test_gate_announces_each_track_once(self) -> None:
        gate = SongAnnouncementGate()
        song = SongAnnouncement(artist="Ghost", title="Witch Image", track_id="abc")

        self.assertTrue(gate.should_announce(song))
        self.assertFalse(gate.should_announce(song))
        self.assertTrue(gate.should_announce(SongAnnouncement("Ghost", "Cirice", "def")))

    def test_watcher_ignores_startup_then_reports_a_stable_change(self) -> None:
        watcher = StableSongChangeWatcher(stability_seconds=3)
        existing_track = MediaTrack("Ghost", "Witch Image", "spotify")
        next_track = MediaTrack("Ghost", "Cirice", "spotify")

        self.assertIsNone(watcher.observe(existing_track, now=0))
        self.assertIsNone(watcher.observe(next_track, now=1))
        self.assertIsNone(watcher.observe(next_track, now=3.9))

        announcement = watcher.observe(next_track, now=4)
        self.assertEqual(announcement.chatbox_text(), "♫ Ghost — Cirice")

    def test_resident_announcer_owns_polling_and_shutdown(self) -> None:
        existing_track = MediaTrack("Ghost", "Witch Image", "spotify")
        next_track = MediaTrack("Ghost", "Cirice", "spotify")
        reads = iter((existing_track, next_track, next_track, next_track))
        messages: list[str] = []
        announcer = ResidentSongAnnouncer(
            lambda: next(reads, next_track), messages.append, lambda: None,
            poll_seconds=0.01, clear_seconds=1.0, stability_seconds=0.01)

        announcer.start()
        deadline = time.monotonic() + 0.3
        while not messages and time.monotonic() < deadline:
            time.sleep(0.01)
        announcer.stop()

        self.assertEqual(len(messages), 1)
        self.assertIn("Ghost", messages[0])
        self.assertIn("Cirice", messages[0])
