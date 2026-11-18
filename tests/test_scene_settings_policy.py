import importlib.util
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
SCENE_DIR = ROOT / "src" / "CSEditor" / "SceneManager"
POLICY_PATH = SCENE_DIR / "SceneSettingsPolicy.h"
GENERATOR_PATH = ROOT / "cmake" / "generate_scene_settings_catalog.py"

SPEC = importlib.util.spec_from_file_location("scene_catalog_generator", GENERATOR_PATH)
GENERATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(GENERATOR)


def scene_sources(pattern: str) -> list[Path]:
    """The manager is split across several translation units, so match them all."""
    return sorted(SCENE_DIR.glob(pattern))


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
        cls.manager = "\n".join(
            path.read_text(encoding="utf-8")
            for path in scene_sources("SceneSettings*.cpp"))
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

    def test_physical_sky_appearance_supports_scene_blending(self):
        entries = {
            (entry["path"], entry["key"]): entry
            for entry in self.entries if entry["feature"] == "PhysicalSky"
        }
        blacklist = [normalize_path(path) for path in self.blacklist]
        location = [normalize_path(path) for path in self.location_paths]
        time = [normalize_path(path) for path in self.time_paths]
        appearance = [
            ("", "vanillaMix"), ("", "trMix"), ("", "apLumMix"), ("", "apTrMix"),
            ("", "skyStaticsBrightness"), ("", "sunDiskRad"),
            ("sunlightColor", "x"), ("masserColor", "y"), ("secundaColor", "z"),
            ("rayleighScatter", "x"), ("rayleighScatterAP1", "x"),
            ("aerosolScatter", "x"), ("aerosolScatter", "y"), ("aerosolScatter", "z"),
            ("aerosolAbsorption", "x"), ("aerosolAbsorption", "y"), ("aerosolAbsorption", "z"),
            ("", "aerosolPhaseG"),
            ("ozoneAbsorption", "x"), ("ozoneAbsorptionAP1", "x"),
            ("cloudMap", "localCloud"),
            ("cloudLayer/low", "densityScale"), ("cloudLayer/low", "ndfAltitudeOffset"),
            ("cloudLayer/low", "bottomSpreadScale"), ("cloudLayer/low", "bottomSpreadHeight"),
            ("cloudLayer/low", "bottomSoftness"), ("cloudLayer/low", "bottomSoftnessHeight"),
            ("cloudLayer/low", "topExpansionScale"), ("cloudLayer/low", "curvatureRadius"),
            ("cloudLayer/low", "ndfAltitudeScale"),
            ("cloudLayer/middle", "densityScale"), ("cloudLayer/middle", "altitude"),
            ("cloudLayer/cirrus", "densityScale"), ("cloudLayer/cirrus", "altitude"),
            ("cloudLayer/lighting", "brightness"), ("cloudLayer/lighting", "sunExtinction"),
            ("cloudLayer/lighting", "viewExtinction"), ("cloudLayer/lighting", "dropletDiameter"),
            ("cloudLayer/lighting", "phaseForwardG"), ("cloudLayer/lighting", "phaseBackwardG"),
            ("cloudLayer/lighting", "msStrength"), ("cloudLayer/lighting", "msAlbedo"),
            ("cloudLayer/lighting", "msBoundaryWidth"), ("cloudLayer/lighting", "ambientStrength"),
            ("cloudLayer/lighting", "scatterDensityThreshold"), ("cloudLayer/lighting", "scatterDensityPower"),
            ("cloudLayer/lighting", "cirrusLightThickness"),
            ("cloudLayer/wind/lowVelocity", "x"), ("cloudLayer/wind/lowVelocity", "y"),
            ("cloudLayer/wind/highVelocity", "x"), ("cloudLayer/wind/highVelocity", "y"),
            ("cloudLayer/wind", "development"), ("cloudLayer/wind", "disturbance"),
            ("cloudMap/procedural/parameters/primary/range", "w"),
            ("cloudMap/procedural/parameters/secondary/range", "w"),
            ("cloudMap/procedural/parameters/modeling/range", "w"),
            ("cloudMap/procedural/parameters/modeling/offset", "x"),
            ("cloudMap/procedural/parameters/heightVariation", "exponent"),
            ("cloudMap/procedural/parameters/baseHeight", "y"),
            ("cloudMap/procedural/parameters/bottomTypeRange", "z"),
            ("cloudMap/procedural/parameters/windOffset", "x"),
            ("cloudMap/procedural/parameters", "localModelingWeight"),
            ("cloudMap/procedural/parameters", "localHeightWeight"),
            ("cloudMap/procedural/parameters", "localWindScale"),
        ]
        for identity in appearance:
            with self.subTest(setting=identity):
                entry = entries[identity]
                address = normalize_path(catalog_address(entry))
                self.assertIn("SceneControllable", entry["flags"])
                self.assertIn("Transitionable", entry["flags"])
                self.assertFalse(any(is_prefix(prefix, address) for prefix in blacklist))
                self.assertTrue(any(is_prefix(prefix, address) for prefix in location))
                self.assertTrue(any(is_prefix(prefix, address) for prefix in time))

    def test_physical_sky_resources_and_quality_stay_global(self):
        global_paths = [
            ("enableAllExteriorCells",), ("forceEnableAllInteriorCells",),
            ("fallbackZBottom",), ("planetRadius",), ("atmosphereRadius",),
            ("halfResApShadow",), ("rayMarchRange",), ("shadowVolumeRange",),
            ("marchStepScale",), ("cloudNoise",),
            # The aerosol mixture, loading and humidity are authoring inputs: the coefficients they
            # derive are what a scene stores, so the inputs themselves must stay out of the layer.
            ("aerosolType",), ("aerosolLoading",), ("aerosolHumidity",),
            ("cloudRelightMix",), ("cloudOriginalMix",),
            ("silverLiningMix",), ("silverLiningSpread",), ("cloudShadowRemapRange",),
            ("cloudMap", "type"), ("cloudMap", "texture"),
            ("cloudMap", "procedural", "noise"), ("cloudMap", "procedural", "local"),
            ("cloudMap", "procedural", "localMaskPath"),
            ("cloudMap", "procedural", "parameters", "heightFromCoverage"),
            ("cloudMap", "procedural", "parameters", "localBlendMode"),
            ("cloudLayer", "cirrus", "uvSize"),
        ]
        global_paths.extend(("cloudMap", "procedural", "parameters", layer, "noise")
                            for layer in ("primary", "secondary", "coverageGain", "modeling", "modelingGain", "heightVariation"))
        blacklist = [normalize_path(path) for path in self.blacklist]
        for path in global_paths:
            prefix = normalize_path(("PhysicalSky", *path))
            matches = [address for address in self.addresses if is_prefix(prefix, address)]
            with self.subTest(path=path):
                self.assertTrue(matches)
                self.assertTrue(all(any(is_prefix(blocked, address) for blocked in blacklist)
                                    for address in matches))

    def test_physical_sky_aerosol_mixture_is_an_authoring_input(self):
        """A discrete mixture cannot be interpolated, so the coefficients it derives are the only
        aerosol values a scene may hold: the knobs stay out of the layer, the optics stay blendable."""
        entries = {
            (entry["path"], entry["key"]): entry
            for entry in self.entries if entry["feature"] == "PhysicalSky"
        }
        coefficients = [
            ("aerosolScatter", "x"), ("aerosolScatter", "y"), ("aerosolScatter", "z"),
            ("aerosolAbsorption", "x"), ("aerosolAbsorption", "y"), ("aerosolAbsorption", "z"),
            ("", "aerosolPhaseG"),
        ]
        for identity in coefficients:
            with self.subTest(setting=identity):
                entry = entries[identity]
                self.assertIn("Transitionable", entry["flags"])
                self.assertIn("SceneControllable", entry["flags"])

        blacklist = [normalize_path(path) for path in self.blacklist]
        for key in ("aerosolType", "aerosolLoading", "aerosolHumidity"):
            with self.subTest(setting=key):
                address = normalize_path(catalog_address(entries[("", key)]))
                self.assertTrue(any(is_prefix(prefix, address) for prefix in blacklist))

        # The floating-point knobs are transitionable in the catalog, but blacklisting is what keeps
        # them out of the layer; only the mixture enum may never claim a midpoint to interpolate to.
        self.assertNotIn("Transitionable", entries[("", "aerosolType")]["flags"])

    def test_physical_sky_switches_are_not_transitionable(self):
        switches = [entry for entry in self.entries
                    if entry["feature"] == "PhysicalSky" and entry["type"] == "Boolean"]
        self.assertTrue(switches)
        for entry in switches:
            with self.subTest(setting=catalog_address(entry)):
                self.assertNotIn("Transitionable", entry["flags"])

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
            path for path in scene_sources("SceneSettings*.h")
            if path != POLICY_PATH
        ] + scene_sources("SceneSettings*.cpp") + [GENERATOR_PATH]
        for path in implementation_paths:
            source = path.read_text(encoding="utf-8")
            for feature in named_features:
                with self.subTest(path=path, feature=feature):
                    self.assertNotIn(f'"{feature}"', source)


if __name__ == "__main__":
    unittest.main()
