#!/usr/bin/env python3
"""Validate eight release packages; publish a draft only after all uploads verify."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
VERSION = r"(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})"


def project_version():
    match = re.search(r"project\(PicLocate VERSION (\S+) LANGUAGES", (ROOT / "CMakeLists.txt").read_text())
    if not match or not re.fullmatch(VERSION, match[1]):
        raise ValueError("CMake project version must be a stable major.minor.patch")
    return match[1]


def package_names(version):
    return [name for arch in ("x64", "arm64") for name in (
        f"PicLocate-{version}-Setup-{arch}.exe",
        f"PicLocate-{version}-windows-{arch}.zip",
        f"PicLocate-{version}-linux-{arch}.tar.gz",
        f"PicLocate-{version}-macos-{arch}.dmg",
    )]


def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def manifest(directory, repo, version):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Expected a public GitHub owner/repository")
    assets = []
    for name in package_names(version):
        path = directory / name
        if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= 2 * 1024**3:
            raise ValueError(f"Missing or invalid release package: {name}")
        assets.append({"name": name, "state": "uploaded", "size": path.stat().st_size,
                       "digest": f"sha256:{checksum(path)}",
                       "browser_download_url": f"https://github.com/{repo}/releases/download/v{version}/{name}"})
    return {"schema": 1, "tag_name": f"v{version}", "draft": False, "prerelease": False, "assets": assets}


def gh(*args, missing_ok=False):
    result = subprocess.run(["gh", *args], text=True, capture_output=True, check=False)
    if result.returncode:
        if missing_ok and "HTTP 404" in result.stderr:
            return None
        raise RuntimeError(result.stderr.strip() or "GitHub command failed")
    return json.loads(result.stdout) if result.stdout.strip().startswith(("{", "[")) else result.stdout


def upload(repo, release_id, paths):
    remote = {a["name"]: a for a in gh("api", f"repos/{repo}/releases/{release_id}")["assets"]}
    for path in paths:
        if path.name in remote:
            gh("api", f"repos/{repo}/releases/assets/{remote[path.name]['id']}", "--method", "DELETE")
        gh("api", f"https://uploads.github.com/repos/{repo}/releases/{release_id}/assets?name={quote(path.name)}",
           "--method", "POST", "--header", "Content-Type: application/octet-stream",
           "--header", f"Content-Length: {path.stat().st_size}", "--input", str(path))


def publish(directory, repo, version, commit, ref):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Release target must be a full commit SHA")
    tag = f"v{version}"
    if ref.startswith("refs/tags/") and ref != f"refs/tags/{tag}":
        raise ValueError("Tag does not match the CMake version")
    payload = manifest(directory, repo, version)
    existing = gh("api", f"repos/{repo}/releases/tags/{tag}", missing_ok=True)
    if not existing:
        # The tag endpoint returns published releases; drafts are listed separately.
        pages = gh("api", "--paginate", "--slurp", f"repos/{repo}/releases?per_page=100")
        existing = next((item for page in pages for item in page if item["tag_name"] == tag), None)
    if existing and not existing["draft"]:
        print(f"{tag} is already published; assets are unchanged.")
        return
    latest = gh("api", f"repos/{repo}/releases/latest", missing_ok=True)
    if latest:
        current = latest["tag_name"].removeprefix("v")
        if not re.fullmatch(VERSION, current) or tuple(map(int, version.split("."))) <= tuple(map(int, current.split("."))):
            raise ValueError("A release must be newer than the latest stable version")
    if existing and existing["target_commitish"] != commit:
        if existing.get("author", {}).get("login") != "github-actions[bot]" or existing.get("name") != f"PicLocate {version}":
            raise ValueError("An existing draft belongs to a different commit")
        existing = gh("api", f"repos/{repo}/releases/{existing['id']}", "--method", "PATCH",
                      "-f", f"target_commitish={commit}")
    if not existing:
        existing = gh("api", f"repos/{repo}/releases", "--method", "POST",
                      "-f", f"tag_name={tag}", "-f", f"target_commitish={commit}",
                      "-f", f"name=PicLocate {version}", "-F", "draft=true", "-F", "generate_release_notes=true")
    release_api = f"repos/{repo}/releases/{existing['id']}"
    checksums = directory / "SHA256SUMS"
    checksums.write_text("".join(f"{asset['digest'][7:]}  {asset['name']}\n" for asset in payload["assets"]), encoding="utf-8")
    upload(repo, existing["id"], [*[directory / a["name"] for a in payload["assets"]], checksums])
    uploaded = gh("api", release_api)
    remote = {a["name"]: a for a in uploaded["assets"]}
    for asset in payload["assets"]:
        actual = remote.get(asset["name"], {})
        if any(actual.get(key) != asset[key] for key in ("state", "size", "digest")):
            raise ValueError(f"GitHub asset verification failed: {asset['name']}")
    update = directory / "update.json"
    update.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    upload(repo, existing["id"], [update])
    metadata = gh("api", release_api)
    metadata_assets = {a["name"]: a for a in metadata["assets"]}
    for path in (checksums, update):
        actual = metadata_assets.get(path.name, {})
        if actual.get("digest") != f"sha256:{checksum(path)}" or actual.get("size") != path.stat().st_size:
            raise ValueError(f"GitHub metadata verification failed: {path.name}")
    gh("api", release_api, "--method", "PATCH", "-F", "draft=false", "-f", "make_latest=true")
    print(f"Published {tag} with eight packages and verified update metadata.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="?")
    parser.add_argument("--version", action="store_true")
    parser.add_argument("--publish", action="store_true")
    parser.add_argument("--repository")
    parser.add_argument("--commit")
    parser.add_argument("--ref", default="refs/heads/main")
    args = parser.parse_args()
    version = project_version()
    if args.version:
        print(version)
        return
    if not args.directory or not args.repository:
        parser.error("directory and --repository are required")
    if args.publish:
        publish(args.directory, args.repository, version, args.commit or "", args.ref)
    else:
        print(json.dumps(manifest(args.directory, args.repository, version), indent=2))


if __name__ == "__main__":
    main()
