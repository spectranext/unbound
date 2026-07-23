#!/usr/bin/env python3
import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


ENV_PATH = Path(".env")


@dataclass(frozen=True)
class Field:
    env_key: str
    prompt: str
    input_key: str | None = None
    default: str | None = None
    required: bool = False
    validate: Callable[[str], str] | None = None

    def existing_or_default(self, values: dict[str, str]) -> str | None:
        if values.get(self.env_key):
            return values[self.env_key]
        if self.input_key and os.environ.get(self.input_key):
            return os.environ[self.input_key]
        return self.default


def validate_required(value: str) -> str:
    if not value:
        raise ValueError("value is required")
    return value


def validate_port(value: str) -> str:
    if not value.isdigit() or not 1 <= int(value) <= 65535:
        raise ValueError("port must be an integer from 1 to 65535")
    return value


def validate_positive_int(value: str) -> str:
    if not value.isdigit() or int(value) <= 0:
        raise ValueError("value must be a positive integer")
    return value


def validate_single_arg(value: str) -> str:
    if not value or re.search(r"\s", value):
        raise ValueError("value must be a single non-empty argument")
    return value


ENV_FIELDS = (
    Field(
        "REPORT_ADDRESS",
        "How clients can reach this server (domain name or ip address, note tcp port 13390 must be reachable)?",
        input_key="ADDRESS",
        required=True,
        validate=validate_required,
    ),
    Field(
        "REPORT_ICON",
        "Server icon hex code",
        input_key="ICON",
        default="FF818181818181FF",
    ),
    Field(
        "REPORT_ICON_COLOR",
        "Server icon color",
        input_key="ICON_COLOR",
        default="4",
    ),
    Field(
        "REPORT_NAME",
        "What is the server title?",
        input_key="NAME",
        required=True,
        validate=validate_required,
    ),
    Field(
        "REPORT_PORT",
        "Server port",
        input_key="PORT",
        default="13390",
        validate=validate_port,
    ),
)

SCENARIO_FIELD = Field(
    "SCENARIO",
    "Scenario",
    input_key="SCENARIO",
    default="default",
    validate=validate_single_arg,
)

MAP_WIDTH_FIELD = Field(
    "MAP_WIDTH",
    "Specify world width in chunks (8 blocks, default 64 chunks)",
    input_key="MAP_WIDTH",
    default="64",
    validate=validate_positive_int,
)


def parse_env(path: Path) -> tuple[dict[str, str], list[str]]:
    values: dict[str, str] = {}
    lines: list[str] = []

    if not path.exists():
        return values, lines

    lines = path.read_text().splitlines()
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in line:
            continue

        key, raw_value = line.split("=", 1)
        key = key.strip()
        if not key:
            continue

        value = raw_value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value

    return values, lines


def env_value(value: str) -> str:
    return value.replace("\r", "").replace("\n", "")


def prompt_value(field: Field, default: str | None) -> str:
    suffix = f" [{default}]" if default else ""
    while True:
        value = input(f"{field.prompt}{suffix}: ").strip()
        value = value or default or ""
        try:
            if field.required:
                value = validate_required(value)
            if field.validate:
                value = field.validate(value)
        except ValueError as e:
            print(f"Invalid {field.env_key}: {e}", file=sys.stderr)
            continue

        return value


def update_lines(lines: list[str], updates: dict[str, str]) -> list[str]:
    remaining = dict(updates)
    result: list[str] = []

    for line in lines:
        if "=" not in line or line.lstrip().startswith("#"):
            result.append(line)
            continue

        key = line.split("=", 1)[0].strip()
        if key in remaining:
            result.append(f"{key}={env_value(remaining.pop(key))}")
        else:
            result.append(line)

    if result and remaining:
        result.append("")

    for key, value in remaining.items():
        result.append(f"{key}={env_value(value)}")

    return result


def build_args(scenario: str, map_width: str) -> str:
    return f"--scenario {scenario} --map-width {map_width} --dont-save-map"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create or complete .env settings for the unbound server."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Prompt for every value, replacing existing .env entries.",
    )
    parser.add_argument(
        "--env-file",
        default=str(ENV_PATH),
        help="Path to the .env file to create or update.",
    )
    args = parser.parse_args()

    env_path = Path(args.env_file)
    values, lines = parse_env(env_path)
    updates: dict[str, str] = {}

    for field in ENV_FIELDS:
        if values.get(field.env_key) and not args.force:
            continue

        updates[field.env_key] = prompt_value(field, field.existing_or_default(values))

    if not values.get("ZX_SERVER_ARGS") or args.force:
        scenario = prompt_value(SCENARIO_FIELD, SCENARIO_FIELD.existing_or_default(values))
        map_width = prompt_value(MAP_WIDTH_FIELD, MAP_WIDTH_FIELD.existing_or_default(values))
        updates["ZX_SERVER_ARGS"] = build_args(scenario, map_width)

    if not updates:
        print(f"{env_path} already contains the unbound configuration.")
        return 0

    env_path.write_text("\n".join(update_lines(lines, updates)) + "\n")
    print(f"Wrote {env_path}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
