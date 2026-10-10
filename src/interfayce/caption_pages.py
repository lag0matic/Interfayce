"""Mutable live utterances paged without replaying already displayed text."""
from dataclasses import dataclass
from difflib import SequenceMatcher


@dataclass
class _Utterance:
    text: str = ''
    consumed: int = 0
    final: bool = False


class CaptionPages:
    def __init__(self, limit=144):
        self.limit = limit
        self._items = []
        self._displayed = []

    def begin(self):
        item = _Utterance()
        self._items.append(item)
        return item

    def update(self, item, text, *, final=False):
        if not any(existing is item for existing in self._items):
            return False
        text = ' '.join(text.split())
        # Map the consumed boundary through late ASR corrections. Already-read
        # pages are never replayed, but corrections to pending text are retained.
        if item.consumed and item.text != text:
            boundary = item.consumed
            for _, i1, i2, j1, j2 in SequenceMatcher(None, item.text, text, autojunk=False).get_opcodes():
                if i1 <= boundary <= i2:
                    item.consumed = j1 + boundary - i1 if item.text[i1:i2] == text[j1:j2] else j2
                    break
        item.text = text
        item.final = final
        return True

    def page(self):
        pieces = []
        spans = []
        length = 0
        for item in self._items:
            while item.consumed < len(item.text) and item.text[item.consumed].isspace():
                item.consumed += 1
            remaining = item.text[item.consumed:]
            if not remaining:
                continue
            if pieces:
                length += 1
            spans.append((item, length, len(remaining)))
            pieces.append(remaining)
            length += len(remaining)
        text = ' '.join(pieces)
        cut = len(text)
        if cut > self.limit:
            cut = text.rfind(' ', 0, self.limit + 1)
            if cut <= 0:
                cut = self.limit
        page = text[:cut]
        consumed = cut + (1 if text[cut:cut + 1] == ' ' else 0)
        self._displayed = [(item, min(size, max(0, consumed - start))) for item, start, size in spans]
        return page

    def advance(self):
        for item, count in self._displayed:
            item.consumed += count
        self._items = [item for item in self._items if not item.final or item.consumed < len(item.text)]
        self._displayed = []

    def queued(self):
        displayed = {id(item): count for item, count in self._displayed}
        return any(item.text[item.consumed + displayed.get(id(item), 0):].strip() for item in self._items)
