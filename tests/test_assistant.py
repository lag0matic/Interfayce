import json
import unittest

from interfayce.assistant import (
    AssistantMemory, AssistantState, AssistantTool, ConversationalAssistant,
)
from interfayce.llm_client import LlmResponse, LlmToolCall


class FakeClient:
    def __init__(self, responses):
        self.responses = list(responses)
        self.requests = []

    def chat(self, **request):
        self.requests.append(request)
        return self.responses.pop(0)


class AssistantTests(unittest.TestCase):
    def test_direct_conversation_is_bounded_and_remembered(self) -> None:
        client = FakeClient([LlmResponse("  A candid answer.  ")])
        states = []
        assistant = ConversationalAssistant(client=client, on_state=states.append)

        result = assistant.ask("  What do you think?  ")

        self.assertTrue(result.succeeded)
        self.assertEqual(result.response, "A candid answer.")
        self.assertEqual(states[0].state, AssistantState.THINKING)
        self.assertEqual(states[-1].state, AssistantState.RESPONDING)
        remembered = assistant.memory.messages()
        self.assertEqual(remembered[-2]["content"], "What do you think?")
        self.assertEqual(remembered[-1]["content"], "A candid answer.")

    def test_allowlisted_tool_round_trip(self) -> None:
        client = FakeClient([
            LlmResponse("", tool_calls=(LlmToolCall(
                "call_time", "get_time", {"location": "Richmond, Indiana"}),)),
            LlmResponse("It is 7:30 PM in Richmond."),
        ])
        calls = []
        tool = AssistantTool(
            "get_time",
            "Return the current time in a location.",
            {
                "type": "object",
                "properties": {"location": {"type": "string"}},
                "required": ["location"],
            },
            lambda arguments: calls.append(arguments) or {"time": "7:30 PM"},
        )
        assistant = ConversationalAssistant(client=client, tools=(tool,))

        result = assistant.ask("What time is it here?")

        self.assertTrue(result.succeeded)
        self.assertEqual(result.tools_used, ("get_time",))
        self.assertEqual(calls, [{"location": "Richmond, Indiana"}])
        second_messages = client.requests[1]["messages"]
        tool_result = json.loads(second_messages[-1]["content"])
        self.assertEqual(tool_result, {"ok": True, "result": {"time": "7:30 PM"}})
        self.assertEqual(second_messages[-1]["tool_call_id"], "call_time")

    def test_unknown_tool_is_not_executed_or_granted(self) -> None:
        client = FakeClient([
            LlmResponse("", tool_calls=(LlmToolCall(
                "call_shell", "run_shell", {"command": "whoami"}),)),
            LlmResponse("I cannot do that."),
        ])
        assistant = ConversationalAssistant(client=client)

        result = assistant.ask("Run a command.")

        self.assertTrue(result.succeeded)
        self.assertEqual(result.tools_used, ())
        tool_result = json.loads(client.requests[1]["messages"][-1]["content"])
        self.assertEqual(tool_result, {"ok": False, "error": "Tool is not available."})

    def test_tool_failure_does_not_expose_exception_details(self) -> None:
        def fail(_arguments):
            raise RuntimeError("personal secret detail")

        client = FakeClient([
            LlmResponse("", tool_calls=(LlmToolCall("call_fail", "lookup", {}),)),
            LlmResponse("The lookup failed."),
        ])
        assistant = ConversationalAssistant(client=client, tools=(AssistantTool(
            "lookup", "Look up a test value.", {"type": "object", "properties": {}}, fail
        ),))

        assistant.ask("Look it up.")

        content = client.requests[1]["messages"][-1]["content"]
        self.assertNotIn("personal secret detail", content)
        self.assertIn("failed safely", content)

    def test_invalid_tool_arguments_never_reach_handler(self) -> None:
        calls = []
        client = FakeClient([
            LlmResponse("", tool_calls=(LlmToolCall(
                "call_time", "get_time", {"location": "Richmond", "shell": "whoami"}),)),
            LlmResponse("That request could not be completed."),
        ])
        assistant = ConversationalAssistant(client=client, tools=(AssistantTool(
            "get_time",
            "Return the current time in a location.",
            {
                "type": "object",
                "properties": {"location": {"type": "string", "maxLength": 160}},
                "required": ["location"],
            },
            lambda arguments: calls.append(arguments),
        ),))

        result = assistant.ask("What time is it?")

        self.assertTrue(result.succeeded)
        self.assertEqual(calls, [])
        tool_result = json.loads(client.requests[1]["messages"][-1]["content"])
        self.assertEqual(tool_result,
                         {"ok": False, "error": "Tool execution failed safely."})

    def test_memory_expires_and_is_limited(self) -> None:
        now = [100.0]
        memory = AssistantMemory(max_turns=2, max_age_seconds=30,
                                 clock=lambda: now[0])
        memory.remember("one", "first")
        now[0] += 1
        memory.remember("two", "second")
        now[0] += 1
        memory.remember("three", "third")
        self.assertEqual([message["content"] for message in memory.messages()],
                         ["two", "second", "three", "third"])
        now[0] += 31
        self.assertEqual(memory.messages(), [])

    def test_cancelled_request_is_not_remembered(self) -> None:
        class CancellingClient(FakeClient):
            def chat(inner_self, **request):
                self.assistant.cancel()
                return super().chat(**request)

        client = CancellingClient([LlmResponse("This should not be used.")])
        self.assistant = ConversationalAssistant(client=client)

        result = self.assistant.ask("Cancel this.")

        self.assertFalse(result.succeeded)
        self.assertEqual(result.state, AssistantState.CANCELLED)
        self.assertEqual(self.assistant.memory.messages(), [])


if __name__ == "__main__":
    unittest.main()
