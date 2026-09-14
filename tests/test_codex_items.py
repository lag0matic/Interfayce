from interfayce.codex_items import CodexItems


def test_progress_does_not_consume_final_speech_budget():
    items = CodexItems()
    for identity, phase, text in [('progress', 'commentary', 'Checking... ' * 150),
                                  ('answer', 'final_answer', 'Your answer is ready.')]:
        items.observe('item/started', {'item': {'id': identity, 'type': 'agentMessage', 'phase': phase, 'text': ''}})
        items.observe('item/agentMessage/delta', {'itemId': identity, 'delta': text})
        items.observe('item/completed', {'item': {'id': identity, 'type': 'agentMessage', 'phase': phase, 'text': text}})
    assert items.speech() == 'Your answer is ready.'
    assert '\n\nYour answer is ready.' in items.display()


def test_file_approval_needs_correlated_change():
    items = CodexItems()
    assert items.file_approval('edit') is None
    items.observe('item/started', {'item': {'id': 'edit', 'type': 'fileChange', 'changes': [
        {'path': 'notes.txt', 'kind': {'type': 'update'}, 'diff': '-old\n+new'}]}})
    assert items.file_approval('edit') == 'notes.txt\n-old\n+new'
    assert items.file_approval('different') is None


def test_unphased_provider_uses_last_message_for_speech():
    items = CodexItems()
    items.observe('item/agentMessage/delta', {'itemId': 'a', 'delta': 'Working'})
    items.observe('item/agentMessage/delta', {'itemId': 'b', 'delta': 'Done'})
    assert items.speech() == 'Done'
