from pathlib import Path
import argparse
import hashlib
import sys

root = Path(__file__).resolve().parents[1]
manifest = root / "MANIFEST.sha256"

exclude_parts = {
    "build",
    ".deps",
    ".git",
    "__pycache__",
}

exclude_files = {
    "MANIFEST.sha256",
}


def manifest_sort_key(path: Path) -> tuple[str, ...]:
    relative = path.relative_to(root)
    return tuple(relative.parts)


def rows() -> list[str]:
    output: list[str] = []
    paths = sorted(root.rglob("*"), key=manifest_sort_key)

    for path in paths:
        relative = path.relative_to(root)

        if not path.is_file():
            continue
        if any(part in exclude_parts for part in relative.parts):
            continue
        if path.name in exclude_files:
            continue
        if path.suffix == ".pyc":
            continue

        relative_text = relative.as_posix()
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        output.append(f"{digest}  {relative_text}")

    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    content = ("\n".join(rows()) + "\n").encode("utf-8")

    if args.check:
        if not manifest.exists() or manifest.read_bytes() != content:
            print("MANIFEST.sha256 ausente ou divergente")
            return 1
        print("OK: MANIFEST.sha256 confere")
        return 0

    manifest.write_bytes(content)
    print(f"gravado: {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
