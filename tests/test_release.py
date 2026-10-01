"""Offline release-gate tests. No GitHub writes or credentials required."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("release", Path(__file__).parents[1] / "scripts/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.version, self.repo, self.commit = "1.8.0", "example/piclocate", "a" * 40
        for name in release.package_names(self.version):
            (self.directory / name).write_bytes(b"test package")

    def test_complete_manifest(self):
        payload = release.manifest(self.directory, self.repo, self.version)
        self.assertEqual(len(payload["assets"]), 8)
        self.assertEqual(payload["tag_name"], "v1.8.0")
        self.assertFalse(payload["draft"])
        self.assertTrue(all(a["digest"].startswith("sha256:") and a["size"] == 12 for a in payload["assets"]))

    def test_missing_package_blocks_release(self):
        (self.directory / release.package_names(self.version)[0]).unlink()
        with patch.object(release, "gh") as gh, self.assertRaises(ValueError):
            release.publish(self.directory, self.repo, self.version, self.commit, "refs/heads/main")
        gh.assert_not_called()

    def test_tag_mismatch_blocks_release(self):
        with patch.object(release, "gh") as gh, self.assertRaises(ValueError):
            release.publish(self.directory, self.repo, self.version, self.commit, "refs/tags/v1.7.0")
        gh.assert_not_called()

    def test_published_release_is_never_overwritten(self):
        with patch.object(release, "gh", return_value={"draft": False}) as gh:
            release.publish(self.directory, self.repo, self.version, self.commit, "refs/heads/main")
        self.assertEqual(gh.call_count, 1)

    def test_failed_upload_keeps_draft(self):
        def github(*args, **kwargs):
            if args[:1] == ("api",):
                if kwargs.get("missing_ok"):
                    return None
                return {"assets": []}
            return ""
        with patch.object(release, "gh", side_effect=github) as gh, self.assertRaises(ValueError):
            release.publish(self.directory, self.repo, self.version, self.commit, "refs/heads/main")
        self.assertFalse(any(c.args[:2] == ("release", "edit") for c in gh.call_args_list))

    def test_publish_only_after_hashes_and_metadata_verify(self):
        def github(*args, **kwargs):
            if args[:1] == ("api",):
                if kwargs.get("missing_ok"):
                    return None
                assets = release.manifest(self.directory, self.repo, self.version)["assets"]
                for name in ("SHA256SUMS", "update.json"):
                    path = self.directory / name
                    if path.exists():
                        assets.append({"name": name, "size": path.stat().st_size, "digest": f"sha256:{release.checksum(path)}"})
                return {"assets": assets}
            return ""
        with patch.object(release, "gh", side_effect=github) as gh:
            release.publish(self.directory, self.repo, self.version, self.commit, "refs/heads/main")
        self.assertEqual(gh.call_args_list[-1].args[:2], ("release", "edit"))
        self.assertEqual(len(json.loads((self.directory / "update.json").read_text())["assets"]), 8)

    def test_downgrades_and_foreign_drafts_are_blocked(self):
        for replies in ((None, {"tag_name": "v2.0.0"}), ({"draft": True, "target_commitish": "b" * 40}, None)):
            with patch.object(release, "gh", side_effect=replies) as gh, self.assertRaises(ValueError):
                release.publish(self.directory, self.repo, self.version, self.commit, "refs/heads/main")
            self.assertTrue(all(c.args[:1] == ("api",) for c in gh.call_args_list))


if __name__ == "__main__":
    unittest.main()
