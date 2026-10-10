import unittest
from interfayce.chatbox import ChatboxCoordinator
from interfayce.caption_pages import CaptionPages
from interfayce.live_stt import LiveCaptionPublisher

class Osc:
    def __init__(self): self.events = []
    def send_chatbox_message(self, text):
        assert 0 < len(text) <= 144
        self.events.append(text)
    def clear_chatbox(self): self.events.append('CLEAR')
    def set_typing(self, active): pass

class BufferedCaptionTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.osc = Osc()
        self.box = ChatboxCoordinator(self.osc, clock=lambda: self.now)
    def say(self, text):
        token = self.box.begin_dictation()
        self.box.update_dictation(token, text, final=True)
        return token
    def test_append_extends_page_deadline_by_visible_growth(self):
        self.say('Hello there.')
        self.now = 5
        self.say('How are you?')
        self.assertEqual(self.osc.events[-1], 'Hello there. How are you?')
        self.now = 29.9
        self.box.tick()
        self.assertNotEqual(self.osc.events[-1], 'CLEAR')
        self.now = 30.8
        self.box.tick()
        self.assertNotEqual(self.osc.events[-1], 'CLEAR')
        self.now = 31
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'CLEAR')
    def test_late_addition_has_time_to_be_read_and_corrections_do_not_stack(self):
        self.say('Hello.')
        self.now = 29.5
        token = self.say('A sentence that needs reading.')
        deadline = self.box._until
        self.assertGreater(deadline, 34)
        self.box.update_dictation(token, 'A sentence that needs reading.')
        self.box.update_dictation(token, 'A sentence that needs reading!')
        self.assertEqual(self.box._until, deadline)
        self.now = 30
        self.box.tick()
        self.assertNotEqual(self.osc.events[-1], 'CLEAR')
        self.now = deadline
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'CLEAR')
    def test_overflow_does_not_extend_visible_page(self):
        token = self.say('x' * 144)
        self.now = 29
        self.box.update_dictation(token, 'x' * 144 + ' pending words', final=True)
        self.now = 30
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'pending words')
    def test_comms_preserves_long_transcript_and_appends_next_capture(self):
        import threading
        from interfayce.comms import CommsDictation
        class Transcriber:
            text = 'Hello.'
            def transcribe(self, audio): return self.text
        transcriber = Transcriber()
        comms = CommsDictation(transcriber, threading.Lock(),
                              capture=lambda *args, **kwargs: object(), osc=self.box)
        comms._run()
        self.now = 5
        transcriber.text = ' '.join(f'word{i}' for i in range(60))
        comms._run()
        self.assertEqual(comms.snapshot().state, 'QUEUED')
        self.assertTrue(self.osc.events[-1].startswith('Hello. word0'))
        pages = [self.osc.events[-1]]
        while self.box.dictation_queued():
            self.now = self.box._until
            self.box.tick()
            pages.append(self.osc.events[-1])
        self.assertEqual(' '.join(pages), 'Hello. ' + transcriber.text)
    def test_live_comms_keeps_overflow_and_final_correction(self):
        import threading
        from interfayce.comms import CommsDictation
        partial = ' '.join(f'word{i}' for i in range(60))
        final = partial + ' finished'
        class Stream:
            def push(self, *args): pass
            def finish(self): return final
            def close(self): pass
        class Transcriber:
            def start_stream(self, callback):
                callback(partial)
                callback(partial)
                return Stream()
            def transcribe(self, audio): raise AssertionError('Unexpected fallback')
        comms = CommsDictation(Transcriber(), threading.Lock(),
                              capture=lambda *args, **kwargs: object(), osc=self.box)
        comms._run()
        self.assertEqual(comms.snapshot().state, 'QUEUED')
        pages = [self.osc.events[-1]]
        while self.box.dictation_queued():
            self.now = self.box._until
            self.box.tick()
            pages.append(self.osc.events[-1])
        self.assertEqual(' '.join(pages), final)
    def test_full_page_holds_then_overflow_gets_its_own_timer(self):
        self.say('x' * 144)
        self.now = 5
        self.say('next page')
        self.assertEqual(self.osc.events, ['x' * 144])
        self.assertTrue(self.box.dictation_queued())
        self.now = 30
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'next page')
        self.now = 59.9
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'next page')
        self.now = 60
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'CLEAR')
    def test_long_speech_preserves_all_words_and_order(self):
        words = [f'word{i}' for i in range(90)]
        self.say(' '.join(words))
        for _ in range(10):
            self.now += 30
            self.box.tick()
        sent = [x for x in self.osc.events if x != 'CLEAR']
        self.assertEqual(' '.join(sent).split(), words)
        self.assertTrue(all(len(x) <= 144 for x in sent))
    def test_live_revisions_replace_utterance_instead_of_duplicating_it(self):
        self.say('Earlier sentence.')
        self.now = 2
        token = self.box.begin_dictation()
        self.box.update_dictation(token, 'Being attacked by a gun')
        self.box.update_dictation(token, 'Being attacked by a cat.', final=True)
        self.assertEqual(self.osc.events[-1], 'Earlier sentence. Being attacked by a cat.')
    def test_queue_continues_during_capture_and_song_waits_for_all_pages(self):
        self.box.set_busy(True)
        self.say('x' * 144 + ' next')
        self.box.announce_song('song')
        self.now = 30
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'next')
        self.box.set_busy(False)
        self.now = 60
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'song')
    def test_clear_invalidates_pending_words_and_late_callbacks(self):
        token = self.say('x' * 144 + ' queued')
        self.box.clear_chatbox()
        self.box.update_dictation(token, 'late result', final=True)
        self.now = 60
        self.box.tick()
        self.assertEqual(self.osc.events, ['x' * 144, 'CLEAR'])
    def test_shortcut_replaces_buffer_and_new_speech_appends_to_shortcut(self):
        token = self.say('x' * 144 + ' obsolete')
        self.box.send_chatbox_message('Be right back.')
        self.box.update_dictation(token, 'late result')
        self.say('In a minute.')
        self.assertEqual(self.osc.events[-1], 'Be right back. In a minute.')
        self.now = 30
        self.box.tick()
        self.assertNotIn('obsolete', self.osc.events)
    def test_expired_text_is_not_revived_by_another_utterance(self):
        self.say('old')
        self.now = 31
        self.say('new')
        self.assertEqual(self.osc.events[-1], 'new')
    def test_speech_after_expired_shortcut_gets_full_lifetime(self):
        self.box.send_chatbox_message('Be right back.')
        self.now = 31
        self.say('Back.')
        self.assertEqual(self.osc.events[-1], 'Back.')
        self.now = 60.9
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'Back.')
        self.now = 61
        self.box.tick()
        self.assertEqual(self.osc.events[-1], 'CLEAR')
    def test_silence_retracts_only_current_partial(self):
        self.say('Keep this.')
        token = self.box.begin_dictation()
        self.box.update_dictation(token, 'noise')
        self.box.update_dictation(token, '', final=True)
        self.assertEqual(self.osc.events[-1], 'Keep this.')
    def test_partials_after_page_turn_do_not_replay_read_text(self):
        token = self.box.begin_dictation()
        prefix = 'one ' * 35
        self.box.update_dictation(token, prefix + 'two three four')
        self.now = 30
        self.box.tick()
        self.box.update_dictation(token, prefix + 'two three five six', final=True)
        self.assertEqual(self.osc.events[-1], 'three five six')
    def test_full_publisher_does_not_drop_beginning_of_long_speech(self):
        sent = []
        publisher = LiveCaptionPublisher(sent.append, preserve_full=True)
        text = 'word ' * 80
        publisher.update(text)
        publisher.update(text)
        publisher.update(text, final=True)
        self.assertEqual(sent[-1], text.strip())
    def test_oversized_single_word_is_not_lost(self):
        pages = CaptionPages()
        token = pages.begin()
        pages.update(token, 'x' * 300, final=True)
        result = []
        while page := pages.page():
            result.append(page)
            pages.advance()
        self.assertEqual([len(x) for x in result], [144,144,12])
        self.assertEqual(''.join(result), 'x' * 300)
