#!/usr/bin/env python3
import json
import os
import pathlib
import re
import subprocess
import tempfile
import textwrap
import urllib.request

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
OPENROUTER_BASE_URL = os.environ.get("OPENROUTER_BASE_URL", "https://openrouter.ai/api/v1")

MODELS = [
    {
        "name": "deepseek/deepseek-r1-0528",
        "secret_env": "DEEPSEEK_API_KEY",
        "responsibility": "language grammar, syntax rules, parser/compiler reasoning",
    },
    {
        "name": "solar/solar-3-pro",
        "secret_env": "SOLAR_API_KEY",
        "responsibility": "refactoring, performance optimization, cleanup",
    },
    {
        "name": "nousresearch/hermes-3-405b-instruct",
        "secret_env": "HERMES_API_KEY",
        "responsibility": "documentation, README, examples, comments",
    },
    {
        "name": "arcee-ai/trinity-large-preview",
        "secret_env": "TRINITY_API_KEY",
        "responsibility": "new language features, roadmap, creative design",
    },
]


def run(cmd, check=True):
    return subprocess.run(cmd, cwd=REPO_ROOT, check=check, text=True, capture_output=True)


def list_repo_files():
    result = run(["git", "ls-files"])
    return [line for line in result.stdout.splitlines() if line.strip()]


def read_snippet(path, limit=2000):
    try:
        content = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return ""
    if len(content) > limit:
        return content[:limit] + "\n... (truncated)"
    return content


def gather_context():
    files = list_repo_files()
    todo_notes = run(["rg", "-n", "TODO|FIXME|BUG", "-S", "."], check=False).stdout
    important_files = [
        "README.md",
        "CMakeLists.txt",
        "Makefile",
        "src",
        "docs",
        "examples",
        "tests",
    ]
    snippets = []
    for entry in important_files:
        path = REPO_ROOT / entry
        if path.is_file():
            snippets.append(f"# {entry}\n{read_snippet(path)}")
    return {
        "file_list": files,
        "todo_notes": todo_notes.strip(),
        "snippets": "\n\n".join(snippets),
    }


def build_prompt(responsibility, context):
    file_list = "\n".join(context["file_list"])
    todo_notes = context["todo_notes"] or "(none found)"
    snippets = context["snippets"] or "(no snippets)"
    return textwrap.dedent(
        f"""
        You are an expert engineer focused on {responsibility}.
        Analyze the repository and propose improvements.
        Return ONLY a unified diff that applies cleanly with git apply.
        Do not wrap the diff in markdown fences.
        Avoid removing license/authorship.
        Do not introduce malicious or breaking changes.

        Repository files:\n{file_list}

        TODO/FIXME/BUG notes:\n{todo_notes}

        Key snippets:\n{snippets}
        """
    ).strip()


def openrouter_request(api_key, model, prompt):
    url = f"{OPENROUTER_BASE_URL}/chat/completions"
    payload = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": "You are a meticulous software engineer.",
            },
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.2,
    }
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "HTTP-Referer": "https://github.com/subhobhai943/sub-lang",
            "X-Title": "sub-lang opencode workflow",
        },
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.loads(response.read().decode("utf-8"))


def extract_diff(content):
    match = re.search(r"```diff\n(.*?)```", content, re.DOTALL)
    if match:
        return match.group(1).strip() + "\n"
    return content.strip() + "\n"


def apply_diff(diff_text, model_name):
    if not diff_text.strip():
        return False
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as handle:
        handle.write(diff_text)
        patch_path = handle.name
    result = subprocess.run(
        ["git", "apply", "--whitespace=fix", patch_path],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        failure_dir = REPO_ROOT / ".opencode" / "failed_patches"
        failure_dir.mkdir(parents=True, exist_ok=True)
        failure_file = failure_dir / f"{model_name.replace('/', '_')}.diff"
        failure_file.write_text(diff_text, encoding="utf-8")
        return False
    return True


def main():
    context = gather_context()
    applied_any = False

    for model in MODELS:
        api_key = os.environ.get(model["secret_env"])
        if not api_key:
            print(f"Skipping {model['name']} (missing {model['secret_env']})")
            continue
        prompt = build_prompt(model["responsibility"], context)
        response = openrouter_request(api_key, model["name"], prompt)
        content = response.get("choices", [{}])[0].get("message", {}).get("content", "")
        diff_text = extract_diff(content)
        if apply_diff(diff_text, model["name"]):
            applied_any = True
            print(f"Applied changes from {model['name']}")
        else:
            print(f"No changes applied for {model['name']}")

    if applied_any:
        run(["git", "status", "--short"], check=False)


if __name__ == "__main__":
    main()
