import importlib.util
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
POLICY_PATH = ROOT / "src" / "SceneSettingsPolicy.h"
MANAGER_PATH = ROOT / "src" / "SceneSettingsManager.cpp"
GENERATOR_PATH = ROOT / "cmake" / "generate_scene_settings_catalog.py"

SPEC = importlib.util.spec_from_file_location("scene_catalog_generator", GENERATOR_PATH)
GENERATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(GENERATOR)


def extract_initializer(source: str, name: str) -> str:
    declaration = re.search(rf"\b{re.escape(name)}\s*=", source)
    if not declaration:
        raise AssertionError(f"Could not find {name}")
    start = source.find("{", declaration.end())
    end = GENERATOR.find_matching_brace(source, start)
    if start < 0 or end < 0:
        raise AssertionError(f"Could not parse {name}")
    return source[start + 1:end]


def extract_braced_rows(source: str) -> list[str]:
    rows = []
    position = 0
    while position < len(source):
        start = source.find("{", position)
        if start < 0:
            break
        end = GENERATOR.find_matching_brace(source, start)
        if end < 0:
            raise AssertionError("Unbalanced policy initializer")
        rows.append(source[start + 1:end])
        position = end + 1
    return rows


def extract_paths(source: str, name: str) -> list[tuple[str, ...]]:
    return [
        tuple(re.findall(r'"([^"]+)"', row))
        for row in extract_braced_rows(extract_initializer(source, name))
    ]


def normalize_address_token(token: str) -> str:
    return "".join(GENERATOR.prettify(token).split()).casefold()


def decode_catalog_path(path: str) -> list[str]:
    return [
        part.replace("~1", "/").replace("~0", "~")
        for part in path.split("/")
        if part
    ]


def catalog_address(entry: dict[str, object]) -> tuple[str, ...]:
    path = [
        part for part in decode_catalog_path(str(entry["path"]))
        if part.casefold() != "settings"
    ]
    return (str(entry["feature"]), *path, str(entry["key"]))


def normalize_path(path: tuple[str, ...]) -> tuple[str, ...]:
    return tuple(normalize_address_token(token) for token in path)


def is_prefix(prefix: tuple[str, ...], address: tuple[str, ...]) -> bool:
    return len(prefix) <= len(address) and address[:len(prefix)] == prefix


class SceneSettingsPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = POLICY_PATH.read_text(encoding="utf-8")
        cls.manager = MANAGER_PATH.read_text(encoding="utf-8")
        cls.entries = GENERATOR.build_entries(ROOT)
        cls.addresses = [normalize_path(catalog_address(entry)) for entry in cls.entries]
        cls.blacklist = extract_paths(cls.policy, "kSettingBlacklist")
        cls.location_paths = extract_paths(
            cls.policy, "kLocationFeatureWhitelist")
        cls.location_features = {path[0] for path in cls.location_paths}
        cls.time_paths = extract_paths(
            cls.policy, "kTimeOfDayFeatureWhitelist")
        cls.time_features = {path[0] for path in cls.time_paths}

    def test_policy_collections_are_nonempty_and_unique(self):
        # The blacklist may legitimately be empty when no shipped setting needs excluding.
        self.assertTrue(self.location_paths)
        self.assertTrue(self.time_paths)
        self.assertTrue(self.location_features)
        self.assertTrue(self.time_features)
        self.assertEqual(len(self.blacklist), len(set(self.blacklist)))
        self.assertEqual(len(self.location_paths), len(set(self.location_paths)))
        self.assertEqual(len(self.time_paths), len(set(self.time_paths)))
        self.assertTrue(all(self.blacklist))
        self.assertTrue(all(self.location_paths))
        self.assertTrue(all(self.time_paths))
        # Address normalization strips these, so a blacklist carrying one could never match.
        self.assertTrue(all(
            token.casefold() not in {"settings", "ppsettings"}
            for path in self.blacklist
            for token in path))

    def test_every_policy_feature_is_discovered(self):
        discovered = {entry["feature"] for entry in self.entries}
        policy_features = {
            *self.location_features,
            *self.time_features,
        }
        self.assertLessEqual(policy_features, discovered)

    def test_every_blacklist_prefix_matches_catalogued_settings(self):
        for path in self.blacklist:
            prefix = normalize_path(path)
            with self.subTest(path=path):
                self.assertTrue(any(is_prefix(prefix, address)
                                    for address in self.addresses))

    def test_location_whitelist_prefixes_resolve_and_restrict(self):
        for feature in self.location_features:
            normalized_feature = normalize_address_token(feature)
            feature_addresses = [
                address for address in self.addresses
                if address[0] == normalized_feature
            ]
            self.assertTrue(feature_addresses)

            prefixes = [
                normalize_path(path) for path in self.location_paths
                if path[0] == feature
            ]
            for prefix in prefixes:
                with self.subTest(feature=feature, prefix=prefix):
                    self.assertTrue(any(is_prefix(prefix, address)
                                        for address in feature_addresses))
                    if len(prefix) > 1:
                        # A narrowing prefix has to exclude something, or it narrows nothing.
                        self.assertTrue(any(not is_prefix(prefix, address)
                                            for address in feature_addresses))
                    else:
                        # A bare feature name grants the feature's whole surface.
                        self.assertTrue(all(is_prefix(prefix, address)
                                            for address in feature_addresses))

    def test_time_of_day_whitelist_prefixes_resolve(self):
        for path in self.time_paths:
            prefix = normalize_path(path)
            with self.subTest(path=path):
                self.assertTrue(any(is_prefix(prefix, address)
                                    for address in self.addresses))

    def test_literal_debug_sections_are_blacklisted(self):
        blacklist = [normalize_path(path) for path in self.blacklist]
        debug_entries = [
            entry for entry in self.entries
            if any(normalize_address_token(part) == "debug"
                   for part in decode_catalog_path(str(entry["displayPath"])))
        ]
        self.assertTrue(debug_entries)
        for entry in debug_entries:
            address = normalize_path(catalog_address(entry))
            with self.subTest(address=address):
                self.assertTrue(any(is_prefix(prefix, address)
                                    for prefix in blacklist))

    def test_manager_consumes_every_policy_collection(self):
        for name in (
                "kSettingBlacklist",
                "kLocationFeatureWhitelist",
                "kTimeOfDayFeatureWhitelist"):
            self.assertIn(f"SceneSettingsPolicy::{name}", self.manager)

    def test_named_feature_policy_does_not_leak_into_scene_manager_code(self):
        named_features = {
            *(path[0] for path in self.blacklist),
            *self.location_features,
            *self.time_features,
        }
        implementation_paths = [
            path for path in (ROOT / "src").glob("SceneSettings*.h")
            if path != POLICY_PATH
        ] + list((ROOT / "src").glob("SceneSettings*.cpp")) + [
            ROOT / "src" / "CSEditor" / "SceneSettingsUI.h",
            ROOT / "src" / "CSEditor" / "SceneSettingsUI.cpp",
            GENERATOR_PATH,
        ]
        for path in implementation_paths:
            source = path.read_text(encoding="utf-8")
            for feature in named_features:
                with self.subTest(path=path, feature=feature):
                    self.assertNotIn(f'"{feature}"', source)


if __name__ == "__main__":
    unittest.main()
