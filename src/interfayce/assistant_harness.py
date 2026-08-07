"""Outside-VR harness for proving the conversational assistant end to end."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
from typing import Callable

from .assistant import AssistantResult, AssistantSnapshot, ConversationalAssistant
from .assistant_tools import deterministic_assistant_tools
from .remote_stt import transcribe_audio_file
from .settings import load_settings
from .web_research import BraveSearchProvider, ResearchSession


@dataclass(frozen=True, slots=True)
class HarnessResult:
    transcript: str
    answer: str
    spoken_answer: str
    succeeded: bool
    tools_used: tuple[str, ...]
    sources: tuple[tuple[str, str, str], ...]


def tts_text(answer: str) -> str:
    """Remove display-only citation and Markdown marks before speech synthesis."""
    text = re.sub(r"\[S[1-5]\]", "", answer)
    text = re.sub(r"[*_`#>]", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    text = re.sub(r"\s+([.,!?;:])", r"\1", text)
    return text[:1_200]


class AssistantHarness:
    def __init__(self, *, assistant: ConversationalAssistant | None = None,
                 research: ResearchSession | None = None,
                 on_state: Callable[[AssistantSnapshot], None] | None = None) -> None:
        if assistant is None:
            provider = BraveSearchProvider()
            research = ResearchSession(provider) if provider.configured else None
            tools = list(deterministic_assistant_tools())
            if research is not None:
                tools.extend(research.tools())
            assistant = ConversationalAssistant(tools=tuple(tools), on_state=on_state)
        self.assistant = assistant
        self.research = research

    @property
    def research_configured(self) -> bool:
        return self.research is not None

    def ask(self, transcript: str, *, speak: bool = False) -> HarnessResult:
        result: AssistantResult = self.assistant.ask(transcript)
        spoken = tts_text(result.response)
        if speak and result.response:
            # The CLI is a proof harness, so speech is intentionally synchronous:
            # the process must not exit while the daemon speech queue is pending.
            from .kokoro import play_wav, synthesize
            play_wav(synthesize(spoken))
        citations = self.research.citations() if self.research is not None else {}
        labels = set(re.findall(r"\[S[1-5]\]", result.response))
        sources = tuple(
            (label, citations[label]["title"], citations[label]["url"])
            for label in sorted(labels) if label in citations
        )
        return HarnessResult(
            transcript=" ".join(transcript.split())[:2_000],
            answer=result.response,
            spoken_answer=spoken,
            succeeded=result.succeeded,
            tools_used=result.tools_used,
            sources=sources,
        )

    def ask_audio_file(self, path: str | Path, *, speak: bool = False) -> HarnessResult:
        settings = load_settings()
        if not settings.stt_endpoint:
            raise RuntimeError(
                "Audio-file testing requires the configured remote STT server; "
                "the local Parakeet adapter accepts captured PCM only."
            )
        transcript = transcribe_audio_file(
            path, settings.stt_endpoint, settings.stt_model
        )
        return self.ask(transcript, speak=speak)


def print_harness_result(result: HarnessResult) -> None:
    print(f"TRANSCRIPT\t{result.transcript}")
    print(f"TOOLS\t{','.join(result.tools_used) if result.tools_used else 'none'}")
    print(f"ANSWER\t{result.answer}")
    for label, title, url in result.sources:
        print(f"SOURCE\t{label}\t{title}\t{url}")
