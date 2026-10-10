"""Bounded keyboard dictation; returns text only, never OSC or keyboard events."""
import logging
import threading
from uuid import uuid4
from .capture_cue import capture_with_cues
from .parakeet_stt import capture_microphone_until
from .live_stt import clean_transcript


class KeyboardDictation:
    def __init__(self, transcriber, command_lock, *, capture=capture_microphone_until,
                 cue=lambda starting: None):
        self.transcriber = transcriber
        self.command_lock = command_lock
        self.capture = capture
        self.cue = cue
        self.lock = threading.Lock()
        self.session = None

    def start(self):
        with self.lock:
            if not self.command_lock.acquire(blocking=False):
                return None
            session = dict(token=uuid4().hex, stop=threading.Event(), cancelled=False,
                           state='LISTENING', text='')
            self.session = session
            try:
                threading.Thread(target=self._run, args=(session,), daemon=True,
                                 name='InterfayceKeyboardDictation').start()
            except Exception:
                self.command_lock.release()
                raise
            return session['token']

    def control(self, token, cancel=False):
        with self.lock:
            s = self.session
            if s is None or s['token'] != token:
                return False
            if cancel:
                s.update(cancelled=True, state='CANCELLED', text='')
            elif s['state'] == 'LISTENING':
                s['state'] = 'PROCESSING'
            s['stop'].set()
            return True

    def status(self, token):
        with self.lock:
            s = self.session
            if s is None or s['token'] != token:
                return 'ERROR\tSession expired'
            return s['state'] + '\t' + s['text']

    def _set(self, s, state, text=''):
        with self.lock:
            if not s['cancelled']:
                s.update(state=state, text=text)

    def _run(self, s):
        stream = None
        try:
            try:
                stream = self.transcriber.start_stream(lambda text: None)
            except Exception:
                pass  # Same whole-recording fallback as COMMS.
            audio = capture_with_cues(self.capture, s['stop'], max_seconds=30.0,
                                     cue=self.cue, end_async=True,
                                     **({'on_chunk': stream.push} if stream else {}))
            self._set(s, 'PROCESSING')
            if s['cancelled']:
                return
            if hasattr(audio, 'frame_data') and not audio.frame_data:
                text = ''
            elif stream:
                try:
                    text = stream.finish()
                except Exception:
                    stream.close()
                    text = self.transcriber.transcribe(audio)
            else:
                text = self.transcriber.transcribe(audio)
            self._set(s, 'DONE', clean_transcript(text))
        except Exception:
            logging.getLogger('interfayce.voice').exception('Keyboard dictation failed')
            self._set(s, 'ERROR', 'Speech recognition failed')
        finally:
            try:
                if stream:
                    stream.close()
            finally:
                self.command_lock.release()
