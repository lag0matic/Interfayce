"""Deterministic, allowlisted tools for the conversational assistant."""

from __future__ import annotations

import ast
from datetime import datetime
import json
import math
import operator
import re
from typing import Any, Callable
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from .assistant import AssistantTool


_GEOCODING_ENDPOINT = "https://geocoding-api.open-meteo.com/v1/search"
_WEATHER_ENDPOINT = "https://api.open-meteo.com/v1/forecast"
_MAX_HTTP_BYTES = 256_000


class AssistantToolError(RuntimeError):
    pass


def _read_json(url: str, *, timeout: float = 7.0) -> dict[str, Any]:
    request = Request(url, headers={
        "Accept": "application/json",
        "User-Agent": "Interfayce/1 conversational-assistant",
    })
    try:
        with urlopen(request, timeout=timeout) as response:
            raw = response.read(_MAX_HTTP_BYTES + 1)
    except OSError as error:
        raise AssistantToolError("The live-data service could not be reached.") from error
    if len(raw) > _MAX_HTTP_BYTES:
        raise AssistantToolError("The live-data response was unexpectedly large.")
    try:
        payload = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AssistantToolError("The live-data service returned invalid JSON.") from error
    if not isinstance(payload, dict) or payload.get("error"):
        raise AssistantToolError("The live-data service rejected the request.")
    return payload


def _bounded_location(value: Any) -> str:
    if not isinstance(value, str):
        raise AssistantToolError("A location must be text.")
    location = " ".join(value.split())
    if not 2 <= len(location) <= 160:
        raise AssistantToolError("A location must contain 2 through 160 characters.")
    return location


def _location_score(query: str, candidate: dict[str, Any]) -> tuple[int, int]:
    wanted = set(re.findall(r"[a-z0-9]+", query.casefold()))
    label = " ".join(str(candidate.get(field, "")) for field in
                     ("name", "admin1", "admin2", "country", "country_code"))
    available = set(re.findall(r"[a-z0-9]+", label.casefold()))
    matched = len(wanted & available)
    population = candidate.get("population", 0)
    return matched, int(population) if isinstance(population, int) else 0


def resolve_location(location: str) -> dict[str, Any]:
    query = _bounded_location(location)
    url = _GEOCODING_ENDPOINT + "?" + urlencode({
        "name": query,
        "count": 8,
        "language": "en",
        "format": "json",
    })
    payload = _read_json(url)
    results = [item for item in payload.get("results", []) if isinstance(item, dict)]
    candidates = [item for item in results
                  if isinstance(item.get("latitude"), (int, float))
                  and isinstance(item.get("longitude"), (int, float))
                  and isinstance(item.get("timezone"), str)]
    if not candidates:
        raise AssistantToolError(f"No location matched {query}.")
    selected = max(candidates, key=lambda item: _location_score(query, item))
    name = str(selected.get("name", query))
    admin = str(selected.get("admin1", ""))
    country = str(selected.get("country", ""))
    label = ", ".join(dict.fromkeys(part for part in (name, admin, country) if part))
    return {
        "name": label,
        "latitude": float(selected["latitude"]),
        "longitude": float(selected["longitude"]),
        "timezone": selected["timezone"],
    }


_BINARY_OPERATORS: dict[type[ast.operator], Callable[[float, float], float]] = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.truediv,
    ast.FloorDiv: operator.floordiv,
    ast.Mod: operator.mod,
    ast.Pow: operator.pow,
}
_UNARY_OPERATORS: dict[type[ast.unaryop], Callable[[float], float]] = {
    ast.UAdd: operator.pos,
    ast.USub: operator.neg,
}


def _finite_number(value: Any) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise AssistantToolError("The expression contains a non-numeric value.")
    if not math.isfinite(value) or abs(value) > 1e100:
        raise AssistantToolError("The calculation exceeded its safe numeric range.")
    return value


def _evaluate(node: ast.AST, *, depth: int = 0) -> int | float:
    if depth > 16:
        raise AssistantToolError("The expression is too deeply nested.")
    if isinstance(node, ast.Expression):
        return _evaluate(node.body, depth=depth + 1)
    if isinstance(node, ast.Constant):
        return _finite_number(node.value)
    if isinstance(node, ast.UnaryOp) and type(node.op) in _UNARY_OPERATORS:
        return _finite_number(_UNARY_OPERATORS[type(node.op)](
            _evaluate(node.operand, depth=depth + 1)))
    if isinstance(node, ast.BinOp) and type(node.op) in _BINARY_OPERATORS:
        left = _evaluate(node.left, depth=depth + 1)
        right = _evaluate(node.right, depth=depth + 1)
        if isinstance(node.op, ast.Pow) and (abs(right) > 12 or abs(left) > 1e12):
            raise AssistantToolError("The exponent is outside the safe range.")
        try:
            return _finite_number(_BINARY_OPERATORS[type(node.op)](left, right))
        except (ArithmeticError, OverflowError) as error:
            raise AssistantToolError("The expression could not be calculated safely.") from error
    raise AssistantToolError("The expression contains an unsupported operation.")


def calculate(arguments: dict[str, Any]) -> dict[str, Any]:
    expression = arguments.get("expression")
    if not isinstance(expression, str) or not 1 <= len(expression) <= 200:
        raise AssistantToolError("The expression must be bounded text.")
    try:
        tree = ast.parse(expression, mode="eval")
    except SyntaxError as error:
        raise AssistantToolError("The expression is not valid arithmetic.") from error
    if sum(1 for _node in ast.walk(tree)) > 64:
        raise AssistantToolError("The expression contains too many operations.")
    result = _evaluate(tree)
    return {"expression": expression, "result": result}


def current_time(arguments: dict[str, Any]) -> dict[str, Any]:
    location = resolve_location(_bounded_location(arguments.get("location")))
    try:
        zone = ZoneInfo(location["timezone"])
    except ZoneInfoNotFoundError as error:
        raise AssistantToolError("Time zone data is unavailable for that location.") from error
    now = datetime.now(zone)
    return {
        "location": location["name"],
        "timezone": location["timezone"],
        "local_time": now.isoformat(timespec="minutes"),
        "spoken_time": now.strftime("%I:%M %p").lstrip("0"),
        "day": now.strftime("%A"),
    }


_WEATHER_CODES = {
    0: "clear",
    1: "mostly clear",
    2: "partly cloudy",
    3: "overcast",
    45: "foggy",
    48: "foggy with rime",
    51: "light drizzle",
    53: "drizzle",
    55: "heavy drizzle",
    56: "light freezing drizzle",
    57: "freezing drizzle",
    61: "light rain",
    63: "rain",
    65: "heavy rain",
    66: "light freezing rain",
    67: "freezing rain",
    71: "light snow",
    73: "snow",
    75: "heavy snow",
    77: "snow grains",
    80: "light rain showers",
    81: "rain showers",
    82: "heavy rain showers",
    85: "light snow showers",
    86: "heavy snow showers",
    95: "thunderstorms",
    96: "thunderstorms with light hail",
    99: "thunderstorms with heavy hail",
}


def _number(mapping: Any, key: str) -> int | float | None:
    value = mapping.get(key) if isinstance(mapping, dict) else None
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def current_weather(arguments: dict[str, Any]) -> dict[str, Any]:
    location = resolve_location(_bounded_location(arguments.get("location")))
    url = _WEATHER_ENDPOINT + "?" + urlencode({
        "latitude": location["latitude"],
        "longitude": location["longitude"],
        "current": ("temperature_2m,relative_humidity_2m,apparent_temperature,"
                    "precipitation,weather_code,wind_speed_10m,wind_direction_10m,"
                    "wind_gusts_10m"),
        "daily": ("weather_code,temperature_2m_max,temperature_2m_min,"
                  "precipitation_probability_max,sunrise,sunset"),
        "temperature_unit": "fahrenheit",
        "wind_speed_unit": "mph",
        "precipitation_unit": "inch",
        "timezone": "auto",
        "forecast_days": 2,
    })
    payload = _read_json(url)
    current = payload.get("current")
    daily = payload.get("daily")
    if not isinstance(current, dict) or not isinstance(daily, dict):
        raise AssistantToolError("The weather service omitted current conditions.")
    weather_code = _number(current, "weather_code")
    wind_direction = _number(current, "wind_direction_10m")

    def compass_direction(degrees: int | float | None) -> str | None:
        if degrees is None:
            return None
        points = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
        return points[round(float(degrees) / 45) % len(points)]

    def first(key: str) -> Any:
        values = daily.get(key)
        return values[0] if isinstance(values, list) and values else None

    return {
        "source": "Open-Meteo",
        "source_url": "https://open-meteo.com/",
        "location": location["name"],
        "observed_at": current.get("time"),
        "conditions": _WEATHER_CODES.get(int(weather_code), "unknown")
            if weather_code is not None else "unknown",
        "temperature_f": _number(current, "temperature_2m"),
        "feels_like_f": _number(current, "apparent_temperature"),
        "humidity_percent": _number(current, "relative_humidity_2m"),
        "precipitation_inches": _number(current, "precipitation"),
        "wind_mph": _number(current, "wind_speed_10m"),
        "wind_direction_degrees": wind_direction,
        "wind_direction": compass_direction(wind_direction),
        "wind_gust_mph": _number(current, "wind_gusts_10m"),
        "today": {
            "high_f": first("temperature_2m_max"),
            "low_f": first("temperature_2m_min"),
            "precipitation_chance_percent": first("precipitation_probability_max"),
            "sunrise": first("sunrise"),
            "sunset": first("sunset"),
        },
    }


def deterministic_assistant_tools() -> tuple[AssistantTool, ...]:
    location_parameters = {
        "type": "object",
        "properties": {
            "location": {
                "type": "string",
                "description": "City and state/province or city and country.",
                "minLength": 2,
                "maxLength": 160,
            },
        },
        "required": ["location"],
    }
    return (
        AssistantTool(
            "get_current_time",
            "Get the current local date and time for a named location.",
            location_parameters,
            current_time,
        ),
        AssistantTool(
            "calculate",
            "Safely calculate a basic arithmetic expression. Use this instead of mental arithmetic.",
            {
                "type": "object",
                "properties": {
                    "expression": {
                        "type": "string",
                        "description": "Arithmetic using numbers, parentheses, +, -, *, /, //, %, or **.",
                        "minLength": 1,
                        "maxLength": 200,
                    },
                },
                "required": ["expression"],
            },
            calculate,
        ),
        AssistantTool(
            "get_current_weather",
            "Get live current conditions and today's forecast for a named location.",
            location_parameters,
            current_weather,
        ),
    )
