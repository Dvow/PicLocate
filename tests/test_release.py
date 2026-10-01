"""Offline release-gate tests. No GitHub writes or credentials required."""
import importlib.util
import json
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse

spec = importlib.util.spec_from_file_location("release", Path(__file__).parents[1] / "scripts/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class GitHub:
    """Model published-only tag lookup and authenticated draft lookup by ID."""
    def __init__(self, existing=None, latest=None, corrupt=None):
        self.existing, self.latest, self.corrupt = existing, latest, corrupt

    def __call__(self, *args, **kwargs):
        if args[0] != "api":
            raise AssertionError("Draft operations must use the release ID, not a tag-based CLI command")
        if args[1].startswith("https://uploads.github.com/"):
            assets = {a["name"]: a for a in self.existing["assets"]}
            path = Path(args[args.index("--input") + 1])
            if "Content-Type: application/octet-stream" not in args or f"Content-Length: {path.stat().st_size}" not in args:
                raise AssertionError("Upload requires raw binary content with its exact length")
            name = parse_qs(urlparse(args[1]).query)["name"][0]
            if name in assets:
                raise AssertionError("An existing asset must be removed before replacement")
            assets[name] = {"id": len(assets) + 100, "name": name, "state": "uploaded", "size": path.stat().st_size,
                            "digest": f"sha256:{release.checksum(path)}"}
            self.existing["assets"] = list(assets.values())
            return deepcopy(assets[name])
        endpoint = next(arg for arg in args if arg.startswith("repos/"))
        fields = dict(arg.split("=", 1) for arg in args if "=" in arg and not arg.startswith("repos/"))
        if "DELETE" in args:
            asset_id = int(endpoint.rsplit("/", 1)[1])
            self.existing["assets"] = [a for a in self.existing["assets"] if a["id"] != asset_id]
            return ""
        elif "POST" in args:
            self.existing = {"id": 77, "tag_name": fields["tag_name"], "target_commitish": fields["target_commitish"],
                             "name": fields["name"], "draft": True, "author": {"login": "github-actions[bot]"}, "assets": []}
        elif "PATCH" in args:
            if "target_commitish" in fields:
                self.existing["target_commitish"] = fields["target_commitish"]
            if fields.get("draft") == "false":
                self.existing["draft"] = False
        elif "/tags/" in endpoint:
            if not self.existing or self.existing["draft"]:
                if kwargs.get("missing_ok"):
                    return None
                raise RuntimeError("gh: Not Found (HTTP 404)")
        elif endpoint.endswith("/latest"):
            return self.latest
        elif "?per_page=" in endpoint:
            return [[], [self.existing] if self.existing else []]
        elif not endpoint.endswith("/77"):
            raise AssertionError(f"Unexpected API endpoint: {endpoint}")
        response = deepcopy(self.existing)
        for asset in response["assets"]:
            if asset["name"] == self.corrupt:
                asset["digest"] = "sha256:" + "0" * 64
        return response


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.version, self.repo, self.commit = "1.8.0", "example/piclocate", "a" * 40
        for name in release.package_names(self.version):
            (self.directory / name).write_bytes(b"test package")

    def draft(self, **changes):
        return {"id": 77, "tag_name": "v1.8.0", "draft": True, "target_commitish": self.commit,
                "name": "PicLocate 1.8.0", "author": {"login": "github-actions[bot]"}, "assets": [], **changes}

    def publish(self):
        release.publish(self.directory, self.repo, self.version, self.commit, "refs/heads/main")

    def test_complete_manifest(self):
        payload = release.manifest(self.directory, self.repo, self.version)
        self.assertEqual(len(payload["assets"]), 11)
        self.assertEqual(payload["tag_name"], "v1.8.0")
        self.assertFalse(payload["draft"])
        self.assertTrue(all(a["digest"].startswith("sha256:") and a["size"] == 12 for a in payload["assets"]))

    def test_missing_package_blocks_release(self):
        (self.directory / release.package_names(self.version)[0]).unlink()
        with patch.object(release, "gh") as gh, self.assertRaises(ValueError):
            self.publish()
        gh.assert_not_called()

    def test_tag_mismatch_blocks_release(self):
        with patch.object(release, "gh") as gh, self.assertRaises(ValueError):
            release.publish(self.directory, self.repo, self.version, self.commit, "refs/tags/v1.7.0")
        gh.assert_not_called()

    def test_published_release_is_never_overwritten(self):
        github = GitHub(self.draft(draft=False))
        with patch.object(release, "gh", side_effect=github) as gh:
            self.publish()
        self.assertEqual(gh.call_count, 1)

    def test_bad_package_hash_keeps_draft(self):
        github = GitHub(corrupt=release.package_names(self.version)[0])
        with patch.object(release, "gh", side_effect=github), self.assertRaises(ValueError):
            self.publish()
        self.assertTrue(github.existing["draft"])
        self.assertFalse((self.directory / "update.json").exists())

    def test_bad_metadata_hash_keeps_draft(self):
        github = GitHub(corrupt="update.json")
        with patch.object(release, "gh", side_effect=github), self.assertRaises(ValueError):
            self.publish()
        self.assertTrue(github.existing["draft"])

    def test_new_draft_is_verified_by_id_before_publishing(self):
        github = GitHub()
        with patch.object(release, "gh", side_effect=github) as gh:
            self.publish()
        self.assertFalse(github.existing["draft"])
        self.assertEqual(len(github.existing["assets"]), 13)
        self.assertEqual(gh.call_args_list[-1].args[1], f"repos/{self.repo}/releases/77")
        self.assertIn("draft=false", gh.call_args_list[-1].args)
        self.assertEqual(len(json.loads((self.directory / "update.json").read_text())["assets"]), 11)

    def test_partial_draft_is_resumed_without_duplicate_creation(self):
        github = GitHub(self.draft())
        with patch.object(release, "gh", side_effect=github) as gh:
            self.publish()
        self.assertFalse(github.existing["draft"])
        self.assertFalse(any("POST" in c.args and c.args[1].endswith("/releases") for c in gh.call_args_list))

    def test_partial_upload_is_replaced_and_verified(self):
        stale = {"id": 12, "name": release.package_names(self.version)[0], "size": 3, "digest": "sha256:" + "0" * 64}
        github = GitHub(self.draft(assets=[stale]))
        with patch.object(release, "gh", side_effect=github) as gh:
            self.publish()
        self.assertFalse(github.existing["draft"])
        self.assertEqual(len(github.existing["assets"]), 13)
        self.assertTrue(any("DELETE" in c.args and c.args[1].endswith("/assets/12") for c in gh.call_args_list))

    def test_automation_draft_retargets_to_new_tested_commit(self):
        github = GitHub(self.draft(target_commitish="b" * 40))
        with patch.object(release, "gh", side_effect=github):
            self.publish()
        self.assertEqual(github.existing["target_commitish"], self.commit)
        self.assertFalse(github.existing["draft"])

    def test_downgrades_and_foreign_drafts_are_blocked(self):
        for github in (GitHub(latest={"tag_name": "v2.0.0"}),
                       GitHub(self.draft(target_commitish="b" * 40, author={"login": "someone-else"}))):
            with patch.object(release, "gh", side_effect=github) as gh, self.assertRaises(ValueError):
                self.publish()
            self.assertFalse(any("POST" in c.args or "PATCH" in c.args for c in gh.call_args_list))


if __name__ == "__main__":
    unittest.main()
