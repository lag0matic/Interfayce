"""One shared recognition route, refreshed at operation boundaries."""
import threading

from .settings import load_settings
from .remote_stt import RemoteSttTranscriber


class ConfiguredTranscriber:
    def __init__(self, local, settings=load_settings, remote_factory=RemoteSttTranscriber):
        self.local = local
        self._settings = settings
        self._factory = remote_factory
        self._key = None
        self._provider = local
        self._lock = threading.Lock()
        self._operation = threading.local()

    def current_provider(self):
        settings = self._settings()
        key = (settings.stt_endpoint, settings.stt_model)
        with self._lock:
            if key != self._key:
                self._provider = (self._factory(*key, fallback=self.local)
                                  if key[0] else self.local)
                self._key = key
            return self._provider

    @property
    def description(self):
        return getattr(self.current_provider(), 'description', 'Local speech recognition')

    def warm(self):
        self.current_provider().warm()

    def start_stream(self, on_partial):
        provider = self.current_provider()
        self._operation.provider = provider
        if not hasattr(provider, 'start_stream'):
            raise NotImplementedError('Local recognition uses completed captures.')
        return provider.start_stream(on_partial)

    def transcribe(self, audio):
        provider = getattr(self._operation, 'provider', None) or self.current_provider()
        try:
            return provider.transcribe(audio)
        finally:
            self._operation.provider = None
