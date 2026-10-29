"""Optional delivery guidance for replies spoken by Ming; no extra model call."""
from .settings import load_settings


def delivery_guidance(*, music=False):
    if load_settings().tts_provider != "ming":
        return ""
    scope = ("Only in user-facing reply/message text, never action names, searches or arguments. "
             "Routine music confirmations should normally stay neutral. " if music else "")
    return ("\nSpoken delivery: " + scope +
            "You may prefix a final spoken reply with one [voice:neutral], [voice:confident], "
            "[voice:amused], [voice:sarcastic], [voice:angry], [voice:tense], [voice:scared], "
            "[voice:sad], [voice:soft], or [voice:whisper] tag when it suits the meaning. "
            "Use subtle, natural delivery, usually neutral. Do not infer the user's feelings. "
            "Occasionally use [chuckle], [laugh], [sigh], or [hmm] when clearly warranted, "
            "but avoid gratuitous vocalizations. These tags control speech and are not read aloud. "
            "Preserve any required JSON response format; tags belong only inside spoken text fields.")
