"""Bounded item state for streamed display, final speech and file approvals."""


class CodexItems:
    def __init__(self):
        self.messages = {}
        self.changes = {}

    def observe(self, method, params):
        if method in ('item/started', 'item/completed'):
            item = params.get('item', {})
            identity = item.get('id')
            if item.get('type') == 'agentMessage':
                previous = self.messages.get(identity, {})
                self.messages[identity] = {
                    'phase': item.get('phase') or previous.get('phase'),
                    'text': item.get('text') or previous.get('text', '')}
            elif item.get('type') == 'fileChange':
                self.changes[identity] = item.get('changes', [])
        elif method == 'item/agentMessage/delta':
            identity = params.get('itemId', 'legacy')
            message = self.messages.setdefault(identity, {'phase': None, 'text': ''})
            message['text'] = (message['text'] + params['delta'])[-100000:]
        if len(self.messages) > 128 or len(self.changes) > 128:
            raise RuntimeError('This response exceeds the wrist history limit.')

    def display(self):
        return '\n\n'.join(m['text'] for m in self.messages.values() if m['text'])[-100000:]

    def speech(self):
        final = [m['text'] for m in self.messages.values() if m['phase'] == 'final_answer' and m['text']]
        if final:
            return '\n\n'.join(final)
        unknown = [m['text'] for m in self.messages.values() if m['phase'] is None and m['text']]
        return unknown[-1] if unknown else ''

    def file_approval(self, item_id):
        changes = self.changes.get(item_id)
        if not changes or any(not c.get('path') or 'diff' not in c for c in changes):
            return None
        text = '\n\n'.join(c['path'] + '\n' + c['diff'] for c in changes)
        return text if len(text) <= 24000 else None
