#!/usr/bin/env python3
"""Validate the GitHub release ref and provide one version to every packager."""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path

RELEASE_TAG = re.compile(r"v(\d{1,3})\.(\d{1,3})\.(\d{1,3})\Z")


def release_context(
    *, event: str, ref_type: str, ref_name: str, input_tag: str, publish: bool
) -> tuple[str, str, bool]:
    if event not in {"push", "workflow_dispatch"}:
        raise ValueError(f"unsupported release event: {event}")
    tagged = ref_type == "tag"
    if event == "push" and not tagged:
        raise ValueError("push release must run from a tag")
    if publish and not tagged:
        raise ValueError("publishing requires dispatching the workflow at the release tag ref")
    if input_tag and input_tag != ref_name:
        raise ValueError(f"requested tag {input_tag} differs from workflow ref {ref_name}")
    if tagged:
        match = RELEASE_TAG.fullmatch(ref_name)
        if match is None:
            raise ValueError(f"release tag must be vMAJOR.MINOR.PATCH: {ref_name}")
        if all(int(part) == 0 for part in match.groups()):
            raise ValueError("release version 0.0.0 cannot be published")
        return ref_name, ref_name[1:], event == "push" or publish
    return "", "0.0.0", False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--github-output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        tag, version, release = release_context(
            event=os.environ["GITHUB_EVENT_NAME"],
            ref_type=os.environ["GITHUB_REF_TYPE"],
            ref_name=os.environ["GITHUB_REF_NAME"],
            input_tag=os.environ.get("INPUT_TAG", ""),
            publish=os.environ.get("INPUT_PUBLISH", "false").lower() == "true",
        )
    except (KeyError, ValueError) as error:
        parser.error(str(error))
    with arguments.github_output.open("a", encoding="utf-8") as output:
        output.write(f"tag={tag}\nversion={version}\nrelease={'true' if release else 'false'}\n")
    print(f"release context: {'tag ' + tag if release else 'package smoke build'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
