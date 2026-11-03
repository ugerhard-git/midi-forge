#!/usr/bin/env python3
"""
Joins the wrapped lines of a Markdown text, so every paragraph and every list item is one line.

CHANGELOG.md is wrapped at 120 characters for reading in an editor, but GitHub shows every line break of a
release text as a real break. The release workflow pipes the version's section through this script.

Usage:
    unwrap-markdown.py < wrapped.md > unwrapped.md
    unwrap-markdown.py --self-test     (checks the rules below; run by CI)

Rules: a line is joined to the one before it when both have text and the line does not start something new
(a heading "#", a list item "- ", "* " or "1. ", a quote ">", a table row "|", a code fence "```").
Blank lines, headings, tables and code blocks (between ``` fences) stay exactly as they are.
"""
import re
import sys

STARTS_SOMETHING_NEW = re.compile(r"^\s*(#|[-*+] |\d+\. |>|\||```)")


def unwrap(text: str) -> str:
    """Returns the text with each paragraph and list item on one line."""
    lines_out = []
    in_code_block = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            in_code_block = not in_code_block
            lines_out.append(line)
            continue
        previous = lines_out[-1] if lines_out else ""
        # Nothing is joined onto a heading, a table row or a code fence; list items and paragraphs continue.
        previous_takes_continuation = not previous.lstrip().startswith(("#", "|", "```"))
        joins_previous = (
            not in_code_block
            and line.strip() != ""
            and previous.strip() != ""
            and previous_takes_continuation
            and not STARTS_SOMETHING_NEW.match(line)
        )
        if joins_previous:
            lines_out[-1] = previous.rstrip() + " " + line.strip()
        else:
            lines_out.append(line.rstrip())
    return "\n".join(lines_out) + ("\n" if text.endswith("\n") else "")


def self_test() -> int:
    """Checks the rules on a small example. Returns 0 if they hold, 1 otherwise (and prints the difference)."""
    wrapped = (
        "Intro text that is\n"
        "wrapped over two lines.\n"
        "\n"
        "### New\n"
        "\n"
        "- **First** item that\n"
        "  continues here – and\n"
        "  here.\n"
        "- Second item.\n"
        "1. Numbered item\n"
        "   continued.\n"
        "\n"
        "| Table | Row |\n"
        "|-------|-----|\n"
        "| stays | as is |\n"
        "\n"
        "```sh\n"
        "code line one\n"
        "code line two\n"
        "```\n"
        "Last paragraph,\n"
        "two lines.\n"
    )
    expected = (
        "Intro text that is wrapped over two lines.\n"
        "\n"
        "### New\n"
        "\n"
        "- **First** item that continues here – and here.\n"
        "- Second item.\n"
        "1. Numbered item continued.\n"
        "\n"
        "| Table | Row |\n"
        "|-------|-----|\n"
        "| stays | as is |\n"
        "\n"
        "```sh\n"
        "code line one\n"
        "code line two\n"
        "```\n"
        "Last paragraph, two lines.\n"
    )
    result = unwrap(wrapped)
    if result == expected:
        print("unwrap-markdown self-test passed")
        return 0
    print("unwrap-markdown self-test FAILED. Expected:\n" + expected + "\nGot:\n" + result)
    return 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.stdout.write(unwrap(sys.stdin.read()))
