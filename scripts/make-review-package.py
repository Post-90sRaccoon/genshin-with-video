"""Prepare a source-only review bundle. Does not read quarantined binaries."""
from pathlib import Path
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parent.parent
review = root / "docs" / "defender-review"
output = root / "review-materials"
output.mkdir(exist_ok=True)

summary = (
    "Build verification before the Defender quarantine\n"
    "Final release build: exit code 0.\n"
    "PASS 21 core assertions\n"
    "PASS storage and process assertions\n"
    "PASS controller modal, visibility and warning regressions\n"
    "Runtime observed: Bilibili sample BV1BMgj6MEz4, P2/31, 720p; F8 changed status to paused.\n"
    "Two runs were interrupted by Defender quarantine. Long-run RAM, F9 and actual-game testing remain incomplete.\n"
    "The executable sample is NOT included in this source/material bundle.\n"
)
(review / "build-verification.txt").write_text(summary, encoding="utf-8")
message = (review / "submission-en.txt").read_text(encoding="utf-8")
if len(message) > 1900:
    raise RuntimeError("Submission text exceeds the portal limit")
json.loads((review / "detection-summary.json").read_text(encoding="utf-8"))

files = []
for name in ["README.md", "THIRD_PARTY.md", "LICENSE", "deps.lock.json", ".gitignore"]:
    files.append(root / name)
for directory in ["src", "tests", "scripts", "licenses", "third_party/include", "third_party/mpv/include", "docs/defender-review"]:
    for path in (root / directory).rglob("*"):
        if path.is_file() and path.name != "source-manifest.sha256":
            files.append(path)

allowed_suffixes = {".cpp", ".h", ".hpp", ".rc", ".manifest", ".ps1", ".py", ".md", ".txt", ".json", ".gpl", ".lgpl", ".mit"}
private_markers = ["荆纬宸", "LAPTOP-OJ35BNK0", "71603169f970964daefacd895dbbda89"]
payloads = {}
for path in sorted(set(files)):
    relative = path.relative_to(root).as_posix()
    if path.suffix.lower() not in allowed_suffixes and path.name not in {"LICENSE", ".gitignore", "mpv-Copyright", "yt-dlp-LICENSE"}:
        raise RuntimeError("Unexpected attachment type: " + relative)
    data = path.read_bytes()
    text = data.decode("utf-8-sig")
    # This packaging script is kept out of the bundle because it contains
    # literal local identifiers solely to verify they are absent elsewhere.
    if path.resolve() == Path(__file__).resolve():
        continue
    if any(marker in text for marker in private_markers):
        raise RuntimeError("Local identity found in attachment: " + relative)
    payloads[relative] = data

manifest = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n" for name, data in sorted(payloads.items()))
(review / "source-manifest.sha256").write_text(manifest, encoding="utf-8")
payloads["docs/defender-review/source-manifest.sha256"] = manifest.encode("utf-8")
archive = output / "GenshinVideo-review-materials-2026-10-31.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
    for name, data in sorted(payloads.items()):
        bundle.writestr(name, data)

with zipfile.ZipFile(archive) as bundle:
    if bundle.testzip() is not None:
        raise RuntimeError("Archive integrity failure")
    names = bundle.namelist()
    if any(name.lower().endswith((".exe", ".dll", ".pdb", ".info.json")) or "/data/" in name.lower() for name in names):
        raise RuntimeError("Runtime artifact found in source-only bundle")
    for line in manifest.splitlines():
        digest, name = line.split("  ", 1)
        if hashlib.sha256(bundle.read(name)).hexdigest() != digest:
            raise RuntimeError("Manifest mismatch: " + name)

digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix(".zip.sha256").write_text(digest + "  " + archive.name + "\n", encoding="utf-8")
print(json.dumps({"archive": str(archive), "files": len(payloads), "bytes": archive.stat().st_size, "sha256": digest, "submission_characters": len(message), "executables_included": False, "uploaded": False}, ensure_ascii=True))
