import unittest
from interfayce.chatbox import ChatboxCoordinator

class Osc:
    def __init__(self): self.events = []
    def send_chatbox_message(self, text): self.events.append(text)
    def clear_chatbox(self): self.events.append("CLEAR")

class ChatboxTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.enabled = True
        self.osc = Osc()
        self.box = ChatboxCoordinator(self.osc, clock=lambda: self.now,
                                      song_enabled=lambda: self.enabled)
    def test_song_waits_remaining_chat_lifetime_then_clears_after_seven(self):
        self.box.send_chatbox_message("hello")
        self.now = 5
        self.box.announce_song("song")
        self.now = 29.9
        self.box.tick()
        self.assertEqual(self.osc.events, ["hello"])
        self.now = 30
        self.box.tick()
        self.assertEqual(self.osc.events, ["hello", "song"])
        self.now = 37
        self.box.tick()
        self.assertEqual(self.osc.events[-1], "CLEAR")
    def test_new_speech_cancels_old_song_clear(self):
        self.box.announce_song("song")
        self.now = 5
        self.box.send_chatbox_message("speech")
        self.now = 7
        self.box.tick()
        self.now = 35
        self.box.tick()
        self.assertEqual(self.osc.events, ["song", "speech"])
    def test_new_chat_extends_wait_and_only_latest_song_survives(self):
        self.box.send_chatbox_message("one")
        self.box.announce_song("old song")
        self.now = 20
        self.box.send_chatbox_message("two")
        self.box.announce_song("new song")
        self.now = 30
        self.box.tick()
        self.assertEqual(self.osc.events, ["one", "two"])
        self.now = 50
        self.box.tick()
        self.assertEqual(self.osc.events[-1], "new song")
    def test_capture_and_transcription_defer_song(self):
        self.box.set_busy(True)
        self.box.announce_song("song")
        self.assertEqual(self.osc.events, [])
        self.box.send_chatbox_message("speech")
        self.box.set_busy(False)
        self.box.tick()
        self.assertEqual(self.osc.events, ["speech"])
    def test_disabled_announcements_drop_pending(self):
        self.box.send_chatbox_message("speech")
        self.box.announce_song("song")
        self.enabled = False
        self.now = 30
        self.box.tick()
        self.enabled = True
        self.box.tick()
        self.assertEqual(self.osc.events, ["speech"])
    def test_explicit_clear_releases_wait(self):
        self.box.send_chatbox_message("speech")
        self.box.announce_song("song")
        self.box.clear_chatbox()
        self.box.tick()
        self.assertEqual(self.osc.events, ["speech", "CLEAR", "song"])
