"""Short-lived, provider-specific choices for follow-up music requests."""
from copy import deepcopy
import re
import threading
import time


class MusicChoices:
    def __init__(self, *, clock=time.monotonic, lifetime=300):
        self._clock = clock
        self._lifetime = lifetime
        self._pending = {}
        self._lock = threading.Lock()

    def get(self, provider):
        with self._lock:
            saved = self._pending.get(provider)
            if saved is None:
                return []
            stamp, choices = saved
            if self._clock() - stamp >= self._lifetime:
                self._pending.pop(provider, None)
                return []
            return deepcopy(choices)

    def save(self, provider, choices):
        with self._lock:
            self._pending[provider] = (self._clock(), deepcopy(choices[:5]))

    def clear(self, provider):
        with self._lock:
            self._pending.pop(provider, None)


def offered_indices(action, count):
    indices = action.get("indices")
    if not isinstance(indices, list) or not 1 <= len(indices) <= 5:
        raise ValueError("Offer one to five returned candidate indices")
    if any(type(i) is not int or not 0 <= i < count for i in indices) or len(set(indices)) != len(indices):
        raise ValueError("Offer distinct returned candidate indices")
    return indices


def ordinal_choice(transcript, count):
    words = " ".join(re.findall(r"[a-z0-9]+", transcript.casefold()))
    match = re.fullmatch(r"(?:please )?(?:play |choose |pick |go with )?(?:the )?"
                         r"(first|second|third|fourth|fifth|1|2|3|4|5)"
                         r"(?: one| option| song)?(?: please)?", words)
    if not match:
        return None
    index = {"first": 0, "second": 1, "third": 2, "fourth": 3, "fifth": 4,
             "1": 0, "2": 1, "3": 2, "4": 3, "5": 4}[match[1]]
    return index if index < count else None


def question(labels):
    return "Which would you like? " + "; ".join(
        f"{i + 1}: {label}" for i, label in enumerate(labels)) + "."
