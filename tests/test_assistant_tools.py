from datetime import datetime as RealDateTime
import json
import unittest
from unittest.mock import patch

from interfayce.assistant_tools import (
    AssistantToolError, calculate, current_time, current_weather,
    deterministic_assistant_tools, resolve_location,
)


class _Response:
    def __init__(self, payload):
        self.payload = payload

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, _limit=-1):
        return json.dumps(self.payload).encode("utf-8")


GEOCODING = {
    "results": [
        {
            "name": "Richmond", "admin1": "Virginia", "country": "United States",
            "country_code": "US", "latitude": 37.55, "longitude": -77.46,
            "timezone": "America/New_York", "population": 226000,
        },
        {
            "name": "Richmond", "admin1": "Indiana", "country": "United States",
            "country_code": "US", "latitude": 39.83, "longitude": -84.89,
            "timezone": "America/Indiana/Indianapolis", "population": 35000,
        },
    ]
}


class AssistantToolTests(unittest.TestCase):
    def test_location_qualifier_beats_larger_same_named_city(self):
        with patch("interfayce.assistant_tools.urlopen",
                   return_value=_Response(GEOCODING)):
            result = resolve_location("Richmond, Indiana")
        self.assertEqual(result["name"], "Richmond, Indiana, United States")
        self.assertEqual(result["timezone"], "America/Indiana/Indianapolis")

    def test_calculator_supports_arithmetic_without_eval(self):
        self.assertEqual(calculate({"expression": "(46 + 2) * 3"})["result"], 144)
        self.assertAlmostEqual(calculate({"expression": "22 / 7"})["result"], 22 / 7)
        for expression in ("__import__('os').system('whoami')", "2 ** 100", "[1, 2]"):
            with self.subTest(expression=expression), self.assertRaises(AssistantToolError):
                calculate({"expression": expression})

    def test_current_time_uses_resolved_timezone(self):
        class FixedDateTime:
            @classmethod
            def now(cls, zone):
                return RealDateTime(2026, 8, 7, 19, 30, tzinfo=zone)

        with patch("interfayce.assistant_tools.urlopen",
                   return_value=_Response(GEOCODING)), \
                patch("interfayce.assistant_tools.datetime", FixedDateTime):
            result = current_time({"location": "Richmond, Indiana"})
        self.assertEqual(result["spoken_time"], "7:30 PM")
        self.assertEqual(result["day"], "Friday")

    def test_weather_is_bounded_structured_and_attributed(self):
        weather = {
            "current": {
                "time": "2026-08-07T19:30", "temperature_2m": 81.2,
                "relative_humidity_2m": 54, "apparent_temperature": 82.1,
                "precipitation": 0.0, "weather_code": 1,
                "wind_speed_10m": 7.2, "wind_direction_10m": 190,
                "wind_gusts_10m": 12.4,
            },
            "daily": {
                "temperature_2m_max": [84.0, 86.0],
                "temperature_2m_min": [64.0, 65.0],
                "precipitation_probability_max": [10, 20],
                "sunrise": ["2026-08-07T06:45", "2026-08-08T06:46"],
                "sunset": ["2026-08-07T20:47", "2026-08-08T20:46"],
            },
        }
        with patch("interfayce.assistant_tools.urlopen", side_effect=[
            _Response(GEOCODING), _Response(weather)
        ]):
            result = current_weather({"location": "Richmond, Indiana"})
        self.assertEqual(result["conditions"], "mostly clear")
        self.assertEqual(result["temperature_f"], 81.2)
        self.assertEqual(result["wind_direction"], "S")
        self.assertEqual(result["today"]["high_f"], 84.0)
        self.assertEqual(result["source"], "Open-Meteo")

    def test_tool_set_has_closed_object_schemas(self):
        tools = deterministic_assistant_tools()
        self.assertEqual({tool.name for tool in tools}, {
            "get_current_time", "calculate", "get_current_weather"
        })
        for tool in tools:
            self.assertFalse(tool.definition()["function"]["parameters"]
                             ["additionalProperties"])


if __name__ == "__main__":
    unittest.main()
