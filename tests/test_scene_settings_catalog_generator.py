import importlib.util
import math
import re
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).parents[1]
GENERATOR_PATH = REPO_ROOT / "cmake" / "generate_scene_settings_catalog.py"
SPEC = importlib.util.spec_from_file_location("scene_catalog_generator", GENERATOR_PATH)
GENERATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(GENERATOR)


def _cmake_catalog_floors():
    """The floors CMake passes the generator, so the gate cannot drift from what CI runs."""
    cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    return tuple(
        int(re.search(rf"--{flag}\s+(\d+)", cmake).group(1))
        for flag in ("min-entries", "min-controllable", "min-controllable-features"))


CMAKE_CATALOG_FLOORS = _cmake_catalog_floors()


def make_control_binding(owner, path, label, control_kind):
    return GENERATOR.ControlBinding(
        owner,
        tuple(path),
        GENERATOR.LocalizedText(label),
        GENERATOR.LocalizedText(),
        control_kind)


def make_control_index(*bindings):
    return GENERATOR.ControlIndex(
        {(binding.owner, binding.path): binding for binding in bindings},
        {}, {}, {}, set())


class SceneSettingsCatalogGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = GENERATOR.build_entries(Path(__file__).parents[1])
        cls.entries_by_id = {
            (entry["feature"], entry["path"], entry["key"]): entry
            for entry in cls.entries
        }

    def test_inline_comment_does_not_hide_following_field(self):
        fields = GENERATOR.parse_struct_fields(
            "uint mode = 0; // explanation; (with parentheses)\n"
            "bool enabled = Runtime::IsEnabled() ? true : false;")
        self.assertEqual(fields["mode"], "uint")
        self.assertEqual(fields["enabled"], "bool")

    def test_multiline_and_cast_initializers_preserve_field_types(self):
        fields = GENERATOR.parse_struct_fields("""
            uint mode = (uint)Mode::Default;
            std::vector<int> values = {
                MakeValue(1),
                MakeValue(2)
            };
        """)
        self.assertEqual(fields["mode"], "uint")
        self.assertEqual(fields["values"], "std::vector<int>")

    def test_feature_matching_is_exact_and_inherited_fields_are_merged(self):
        feature_source = """
        struct VR : Feature {
            struct PerFrame { float strength = 1.0f; };
            struct Settings : PerFrame { bool enabled = true; } settings;
        };
        """
        prefixed_source = """
        struct VRStereoOptimizations {
            struct Settings { float wrong = 0.0f; } settings;
        };
        """
        with tempfile.TemporaryDirectory() as directory:
            feature_path = Path(directory) / "VR.h"
            prefixed_path = Path(directory) / "VRStereoOptimizations.h"
            feature_path.write_text(feature_source, encoding="utf-8")
            prefixed_path.write_text(prefixed_source, encoding="utf-8")
            features = {"VR": {"short": "VR", "name": "VR", "source": str(feature_path)}}
            paths = [feature_path, prefixed_path]
            fields = GENERATOR.collect_feature_struct_fields(paths, features)
            members = GENERATOR.collect_feature_member_fields(paths, features)
        self.assertEqual(fields["VR"]["Settings"], {
            "strength": "float",
            "enabled": "bool",
        })
        self.assertEqual(members["VR"]["settings"], "Settings")

    def test_feature_names_resolve_constants_wrappers_and_helpers(self):
        source = """
        struct Example : Feature {
            static constexpr std::string_view kShortName = "Example";
            static constexpr auto kDisplayName = "Example Feature";
            static std::string_view NameValue() { return kDisplayName; }
            std::string GetShortName() override { return std::string(kShortName); }
            std::string GetName() override { return std::string(NameValue()); }
        };
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.h"
            path.write_text(source, encoding="utf-8")
            features = GENERATOR.collect_features([path])
        self.assertEqual(features["Example"]["short"], "Example")
        self.assertEqual(features["Example"]["name"], "Example Feature")

    def test_catalog_validation_rejects_too_few_entries(self):
        with self.assertRaises(ValueError):
            GENERATOR.validate_entries([], 1)
        with self.assertRaises(ValueError):
            GENERATOR.validate_entries([{"feature": "One"}], 2)

    def test_i18n_keys_include_file_prefix(self):
        translated = GENERATOR.extract_i18n_call(
            'ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &settings.strength)',
            "feature.example.")
        self.assertEqual(translated, ("feature.example.strength", "Strength"))

    def test_ui_labels_support_named_settings_objects(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &bendSettings.Strength, 0.0f, 1.0f);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        binding = index.bindings[("Example", ("Strength",))]
        self.assertEqual(binding.label, GENERATOR.LocalizedText(
            "Strength", "feature.example.strength"))
        self.assertEqual(binding.category, GENERATOR.LocalizedText())
        self.assertEqual(
            (binding.control_kind, binding.minimum, binding.maximum,
             binding.display_scale),
            ("SliderFloat", 0.0, 1.0, 1.0))

    def test_feature_owned_ui_helpers_discover_direct_controls(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            DrawPanel();
        }
        void Example::DrawPanel()
        {
            ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &settings.enabled);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])

        binding = index.bindings[("Example", ("enabled",))]
        self.assertEqual(binding.control_kind, "Checkbox")
        self.assertEqual(binding.label, GENERATOR.LocalizedText(
            "Enabled", "feature.example.enabled"))

    def test_diagnostic_text_does_not_replace_control_label(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &settings.Strength, 0.0f, 1.0f);
            ImGui::Text(T(TKEY("strength_debug"), "Strength: %.2f"), settings.Strength);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        binding = index.bindings[("Example", ("Strength",))]
        self.assertEqual(binding.label, GENERATOR.LocalizedText(
            "Strength", "feature.example.strength"))
        self.assertEqual(binding.control_kind, "SliderFloat")

    def test_selector_paths_include_tabs_from_draw_helpers(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            DrawPageSettings();
        }
        void Example::DrawPageSettings()
        {
            if (ImGui::BeginTabItem(T(TKEY("page"), "Page"))) {
                DrawNestedSettings(settings.Nested);
                ImGui::EndTabItem();
            }
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            roots = GENERATOR.collect_tab_selector_roots([path])
        self.assertEqual(
            roots[("Example", ("Nested",))],
            (("Page",), ("feature.example.page",)))

    def test_catalog_paths_escape_display_separators(self):
        self.assertEqual(
            GENERATOR.join_catalog_path(("Group/Heading", "Value~Name")),
            "Group~1Heading/Value~0Name")

    def test_indexed_control_paths_match_concrete_array_elements(self):
        self.assertEqual(
            GENERATOR.normalize_setting_path("Ghosts[i].Color.data()"),
            ("Ghosts", "*", "Color"))
        color = make_control_binding(
            "Example", ("Ghosts", "*", "Color"), "Color", "ColorEdit3")
        match = make_control_index(color).match(
            ("Example",), ("Ghosts", "3", "Color"))
        self.assertIsNotNone(match)
        self.assertEqual(match.binding, color)

    def test_vector_controls_preserve_control_slices(self):
        index = make_control_index(
            make_control_binding("Example", ("mixed", "x"), "Color", "ColorEdit3"),
            make_control_binding("Example", ("mixed", "w"), "Distance", "SliderFloat"),
            make_control_binding("Example", ("split", "x"), "First Pair", "InputFloat2"),
            make_control_binding("Example", ("split", "z"), "Second Pair", "InputFloat2"))
        components = ("x", "y", "z", "w")

        mixed = [
            GENERATOR.resolve_vector_binding(
                index, ("Example",), ("mixed",), components, component)[1:]
            for component in range(4)
        ]
        self.assertEqual(mixed, [
            (0, 3, "Color"),
            (0, 3, "Color"),
            (0, 3, "Color"),
            (3, 1, "None"),
        ])

        split = [
            GENERATOR.resolve_vector_binding(
                index, ("Example",), ("split",), components, component)[1:]
            for component in range(4)
        ]
        self.assertEqual(split, [
            (0, 2, "Numeric"),
            (0, 2, "Numeric"),
            (2, 2, "Numeric"),
            (2, 2, "Numeric"),
        ])

    def test_projected_numeric_helper_projects_local_component_labels(self):
        helper_source = """
        constexpr int TupleSize = 3;
        bool EditTuple(const char* label, float* values,
            const std::array<const char*, 3>& componentLabels,
            float minimum, float maximum)
        {
            ImGui::PushID(label);
            for (int component = 0; component < TupleSize; component++) {
                const std::string format = std::string("%.2f ") + componentLabels[component];
                ImGui::SliderFloat("##Value", &values[component], minimum, maximum, format.c_str());
            }
            ImGui::TextUnformatted(label);
            return true;
        }
        """
        feature_source = """
        #define I18N_KEY_PREFIX "feature.example."
        struct Settings { float3 Tuple; };
        void Example::DrawSettings()
        {
            ImGui::SeparatorText(T(TKEY("group"), "Group"));
            const std::array componentLabels = {
                T(TKEY("first"), "First"),
                T(TKEY("second"), "Second"),
                T(TKEY("third"), "Third")
            };
            Widgets::EditTuple(
                T(TKEY("tuple"), "Tuple"),
                &settings.Tuple.x,
                componentLabels,
                -1.f,
                2.f);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            helper_path = Path(directory) / "Widgets.cpp"
            feature_path = Path(directory) / "Example.cpp"
            helper_path.write_text(helper_source, encoding="utf-8")
            feature_path.write_text(feature_source, encoding="utf-8")
            index = GENERATOR.collect_control_index(
                [helper_path, feature_path])

        identity = ("Example", ("Tuple", "x"))
        binding = index.bindings[identity]
        self.assertEqual(binding.label, GENERATOR.LocalizedText(
            "Tuple", "feature.example.tuple"))
        self.assertEqual(binding.category, GENERATOR.LocalizedText(
            "Group", "feature.example.group"))
        self.assertEqual(
            (binding.control_kind, binding.minimum, binding.maximum,
             binding.display_scale),
            ("ProjectedNumeric3", -1.0, 2.0, 1.0))
        self.assertEqual(
            binding.component_labels,
            (GENERATOR.LocalizedText("First", "feature.example.first"),
             GENERATOR.LocalizedText("Second", "feature.example.second"),
             GENERATOR.LocalizedText("Third", "feature.example.third")))
        self.assertEqual([
            GENERATOR.resolve_vector_binding(
                index, ("Example",), ("Tuple",), ("x", "y", "z"), component)[1:]
            for component in range(3)
        ], [(0, 3, "Numeric")] * 3)

    def test_projected_numeric_helper_rejects_transformed_storage(self):
        source = """
        bool EditTuple(const char* label, float* values,
            const std::array<const char*, 3>& componentLabels)
        {
            ImGui::PushID(label);
            for (int component = 0; component < 3; component++) {
                const std::string format = componentLabels[component];
                ImGui::SliderFloat(
                    "##Value", &values[Remap(component)], 0.f, 1.f, format.c_str());
            }
            ImGui::TextUnformatted(label);
            return true;
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Widgets.cpp"
            path.write_text(source, encoding="utf-8")
            helpers = GENERATOR.collect_projected_numeric_helpers([path])
        self.assertEqual(helpers, {})

    def test_projected_numeric_call_rejects_component_count_mismatch(self):
        helper_source = """
        bool EditTuple(const char* label, float* values,
            const std::array<const char*, 3>& componentLabels)
        {
            ImGui::PushID(label);
            for (int component = 0; component < 3; component++) {
                const std::string format = componentLabels[component];
                ImGui::SliderFloat(
                    "##Value", &values[component], 0.f, 1.f, format.c_str());
            }
            ImGui::TextUnformatted(label);
            return true;
        }
        """
        feature_source = """
        struct Settings { float3 Tuple; };
        void Example::DrawSettings()
        {
            const std::array componentLabels = { "First", "Second" };
            EditTuple("Tuple", &settings.Tuple.x, componentLabels);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            helper_path = Path(directory) / "Widgets.cpp"
            feature_path = Path(directory) / "Example.cpp"
            helper_path.write_text(helper_source, encoding="utf-8")
            feature_path.write_text(feature_source, encoding="utf-8")
            index = GENERATOR.collect_control_index(
                [helper_path, feature_path])
        self.assertNotIn(("Example", ("Tuple", "x")), index.bindings)

    def test_projected_numeric_helper_rejects_overloads(self):
        function = """
        bool EditTuple({extra} const char* label, float* values,
            const std::array<const char*, 2>& componentLabels)
        {{
            ImGui::PushID(label);
            for (int component = 0; component < 2; component++) {{
                const std::string format = componentLabels[component];
                ImGui::SliderFloat(
                    "##Value", &values[component], 0.f, 1.f, format.c_str());
            }}
            ImGui::TextUnformatted(label);
            return true;
        }}
        """
        source = function.format(extra="") + function.format(extra="int mode,")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Widgets.cpp"
            path.write_text(source, encoding="utf-8")
            helpers = GENERATOR.collect_projected_numeric_helpers([path])
        self.assertNotIn("EditTuple", helpers)

    def test_indirect_tables_project_only_concrete_numeric_control_flows(self):
        helper_source = r'''
        void EditThreeValues(const char* label, float* values, float speed,
            float min, float max, const char* format) {
            for (int component = 0; component < 3; ++component) {
                ImGui::SetNextItemColorMarker(markers[component]);
                ImGui::DragFloat(label, &values[component], speed, min, max, format);
            }
        }
        '''
        feature_source = r'''
        #define I18N_KEY_PREFIX "feature.example."
        struct ExampleValues { float4 derived; float4 stored; };
        struct ControlRow {
            const char* id;
            const char* label;
            float4 ExampleValues::* value;
            float min;
            float max;
            const char* format;
        };
        std::array<ControlRow, 1> GetDerivedControls() {
            return { ControlRow{ "DerivedId", T(TKEY("derived"), "Derived"),
                &ExampleValues::derived, -1.f, 2.f, "%.2f" } };
        }
        std::array<ControlRow, 1> GetStoredControls() {
            return { ControlRow{ "StoredId", T(TKEY("stored"), "Stored"),
                &ExampleValues::stored, 0.f, 3.f, "%.2f" } };
        }
        template <class Callback>
        void DrawComposite(const ControlRow& control, float& allValue,
            float* sliceValues, Callback callback) {
            ImGui::PushID(control.id);
            if (ImGui::SliderFloat("##All", &allValue, control.min, control.max, control.format))
                callback(allValue);
            EditThreeValues("##Slice", sliceValues, 0.001f,
                control.min, control.max, control.format);
            ImGui::TextUnformatted(control.label);
            ImGui::PopID();
        }
        void DrawDerived(ExampleValues& settings, const ControlRow& control) {
            auto& value = settings.*control.value;
            float allValue = Average(value);
            DrawComposite(control, allValue, &value.x, [&](float changed) { SetAll(value, changed); });
        }
        void DrawStored(ExampleValues& settings, const ControlRow& control) {
            auto& value = settings.*control.value;
            DrawComposite(control, value.x, &value.y, [](float) {});
        }
        template <size_t N>
        void DrawDerivedSet(ExampleValues& settings, const std::array<ControlRow, N>& controls) {
            for (const auto& control : controls) DrawDerived(settings, control);
        }
        template <size_t N>
        void DrawStoredSet(ExampleValues& settings, const std::array<ControlRow, N>& controls) {
            for (const auto& control : controls) DrawStored(settings, control);
        }
        void Example::DrawSettings() {
            if (ImGui::TreeNode(T(TKEY("derived_group"), "Derived Group"))) {
                DrawDerivedSet(settings, GetDerivedControls());
            }
            if (ImGui::TreeNode(T(TKEY("stored_group"), "Stored Group"))) {
                DrawStoredSet(settings, GetStoredControls());
            }
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            helper_path = Path(directory) / "Widgets.cpp"
            feature_path = Path(directory) / "Example.cpp"
            helper_path.write_text(helper_source, encoding="utf-8")
            feature_path.write_text(feature_source, encoding="utf-8")
            labels, components, aggregate_all = (
                GENERATOR.collect_indirect_numeric_projections(
                    [feature_path, helper_path]))
            opaque_projection = GENERATOR.collect_indirect_numeric_projections(
                [feature_path])

        self.assertEqual(labels[("Example", ("derived", "x"))], (
            "Derived", "Derived Group", "feature.example.derived",
            "feature.example.derived_group", "ProjectedColor3", -1.0, 2.0, 1.0,
            "DragFloat", False, False))
        self.assertEqual(components, {})

        self.assertEqual(labels[("Example", ("stored", "x"))][4], "ProjectedColor4")
        self.assertTrue(aggregate_all[("Example", ("stored", "x"))])
        self.assertEqual(opaque_projection, ({}, {}, {}))

    def test_indirect_numeric_projection_supports_each_vector_arity(self):
        template = r'''
        struct Values { float@N@ value; };
        struct Row { const char* token; const char* text;
            float@N@ Values::* member; float low; float high; };
        std::array<Row, 1> ProvideRows() { return {
            Row{ "Token", "Value", &Values::value, -2.f, 4.f } }; }
        void EditSlice(const Row& row, float& combined, float* slice) {
            ImGui::PushID(row.token);
            ImGui::SliderFloat("##Combined", &combined, row.low, row.high);
            ImGui::DragFloat@N@("##Slice", slice, 0.1f, row.low, row.high);
            ImGui::TextUnformatted(row.text);
        }
        void DrawValue(Values& state, const Row& row) {
            auto& bound = state.*row.member;
            float combined = Fold(bound);
            EditSlice(row, combined, &bound.x);
        }
        template <size_t N>
        void DrawRows(Values& state, const std::array<Row, N>& rows) {
            for (const auto& row : rows) DrawValue(state, row);
        }
        void Sample::DrawSettings() { DrawRows(settings, ProvideRows()); }
        '''
        for arity in (2, 3, 4):
            with self.subTest(arity=arity), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "Sample.cpp"
                path.write_text(template.replace("@N@", str(arity)), encoding="utf-8")
                labels, components, aggregate_all = (
                    GENERATOR.collect_indirect_numeric_projections([path]))
            self.assertEqual(labels[("Sample", ("value", "x"))][4],
                             f"ProjectedNumeric{arity}")
            self.assertEqual(components, {})
            self.assertEqual(aggregate_all, {})

    def test_indirect_shift_slider_projects_vector_numeric(self):
        source = r'''
        struct Values { float3 value; };
        struct Row { const char* token; const char* text;
            float3 Values::* member; float low; float high; };
        std::array<Row, 1> ProvideRows() { return {
            Row{ "Token", "Value", &Values::value, -2.f, 4.f } }; }
        void EditSlice(const Row& row, float& combined, float* slice) {
            ImGui::PushID(row.token);
            ImGui::SliderFloat("##Combined", &combined, row.low, row.high);
            Util::ShiftSlider<3>("##Slice", slice, row.low, row.high);
            ImGui::TextUnformatted(row.text);
        }
        void DrawValue(Values& state, const Row& row) {
            auto& bound = state.*row.member;
            float combined = Fold(bound);
            EditSlice(row, combined, &bound.x);
        }
        template <size_t N>
        void DrawRows(Values& state, const std::array<Row, N>& rows) {
            for (const auto& row : rows) DrawValue(state, row);
        }
        void Sample::DrawSettings() { DrawRows(settings, ProvideRows()); }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Sample.cpp"
            path.write_text(source, encoding="utf-8")
            labels, _, _ = GENERATOR.collect_indirect_numeric_projections([path])

        self.assertEqual(labels[("Sample", ("value", "x"))][4], "ProjectedNumeric3")

    def test_indirect_numeric_projection_rejects_conflicting_rows(self):
        source = r'''
        #define I18N_KEY_PREFIX "feature.example."
        struct ExampleValues { float4 value; };
        struct ControlRow { const char* id; const char* label;
            float4 ExampleValues::* value; float min; float max; const char* format; };
        std::array<ControlRow, 1> GetFirst() { return {
            ControlRow{ "First", T(TKEY("first"), "First"),
                &ExampleValues::value, 0.f, 1.f, "%.2f" } }; }
        std::array<ControlRow, 1> GetSecond() { return {
            ControlRow{ "Second", T(TKEY("second"), "Second"),
                &ExampleValues::value, 0.f, 1.f, "%.2f" } }; }
        template <class Callback>
        void DrawComposite(const ControlRow& control, float& allValue,
            float* sliceValues, Callback callback) {
            ImGui::PushID(control.id);
            ImGui::SliderFloat("##All", &allValue, control.min, control.max, control.format);
            ImGui::DragFloat3("##Slice", sliceValues, 0.001f,
                control.min, control.max, control.format);
            ImGui::TextUnformatted(control.label);
            ImGui::PopID();
        }
        void DrawValue(ExampleValues& settings, const ControlRow& control) {
            auto& value = settings.*control.value;
            float allValue = Average(value);
            DrawComposite(control, allValue, &value.x, [&](float changed) { SetAll(value, changed); });
        }
        template <size_t N>
        void DrawSet(ExampleValues& settings, const std::array<ControlRow, N>& controls) {
            for (const auto& control : controls) DrawValue(settings, control);
        }
        void Example::DrawSettings() { DrawSet(settings, GetFirst()); DrawSet(settings, GetSecond()); }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "ambiguous indirect numeric projection"):
                GENERATOR.collect_indirect_numeric_projections([path])

    def test_local_boolean_alias_and_button_choices_are_discovered(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings(Settings& settings)
        {
            bool enabled = settings.Enabled != 0;
            ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &enabled);
            settings.Enabled = enabled;
            if (ImGui::Button(T(TKEY("mode_a"), "Mode A")))
                settings.Mode = 0;
            if (ImGui::Button(T(TKEY("mode_b"), "Mode B")))
                settings.Mode = 1;
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertEqual(
            index.bindings[("Example", ("Enabled",))].control_kind,
            "Checkbox")
        self.assertEqual(
            index.bindings[("Example", ("Mode",))].choices,
            ((0, "Mode A", "feature.example.mode_a"),
             (1, "Mode B", "feature.example.mode_b")))

    def test_typed_draw_helper_controls_are_discovered_by_settings_type(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        using PanelSettings = Example::PanelSettings;
        void DrawPanel(PanelSettings& panel)
        {
            ImGui::SliderFloat(T(TKEY("amount"), "Amount"), &panel.amount, 0.f, 2.f);
            ImGui::Checkbox(T(TKEY("enabled"), "Enabled"), &panel.enabled);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        amount = index.bindings[("PanelSettings", ("amount",))]
        enabled = index.bindings[("PanelSettings", ("enabled",))]
        self.assertEqual(amount.label, GENERATOR.LocalizedText(
            "Amount", "feature.example.amount"))
        self.assertEqual(amount.category, GENERATOR.LocalizedText())
        self.assertEqual(amount.control_kind, "SliderFloat")
        self.assertEqual(enabled.control_kind, "Checkbox")

    def test_typed_helper_resolves_static_boolean_ternary_labels(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        using PanelSettings = Example::PanelSettings;
        void DrawPanel(PanelSettings& panel, const bool isInterior)
        {
            ImGui::SliderFloat(
                isInterior ? T(TKEY("interior"), "Interior") :
                             T(TKEY("exterior"), "Exterior"),
                &panel.amount, 0.f, 2.f);
        }
        void Example::DrawSettings()
        {
            DrawPanel(settings.Exterior, false);
            DrawPanel(settings.Interior, true);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        exterior = index.bindings[("Example", ("Exterior", "amount"))]
        interior = index.bindings[("Example", ("Interior", "amount"))]
        self.assertEqual(exterior.label, GENERATOR.LocalizedText(
            "Exterior", "feature.example.exterior"))
        self.assertEqual(interior.label, GENERATOR.LocalizedText(
            "Interior", "feature.example.interior"))

    def test_labeled_draw_helper_projects_heading_to_setting_root(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        struct PanelSettings { float amount; };
        void DrawPanel(const char* label, PanelSettings& panel)
        {
            if (!ImGui::TreeNodeEx(label))
                return;
            ImGui::SliderFloat("Amount", &panel.amount, 0.f, 1.f);
            ImGui::TreePop();
        }
        void Example::DrawAdvanced()
        {
            DrawPanel(T(TKEY("first"), "First"), settings.first);
            DrawPanel(T(TKEY("second"), "Second"), settings.second);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            headings = GENERATOR.collect_labeled_helper_roots([path])
        self.assertEqual(headings[("Example", ("first",))], (
            "First", "feature.example.first"))
        self.assertEqual(headings[("Example", ("second",))], (
            "Second", "feature.example.second"))

    def test_transformed_local_alias_is_not_a_control_binding(self):
        source = """
        void Example::DrawSettings()
        {
            bool disabled = !settings.Enabled;
            ImGui::Checkbox("Disabled", &disabled);
            settings.Enabled = !disabled;
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertNotIn(("Example", ("Enabled",)), index.bindings)

    def test_conflicting_control_labels_fail_closed(self):
        source = """
        void Example::DrawSettings()
        {
            ImGui::SliderFloat("First", &settings.amount, 0.f, 1.f);
            ImGui::SliderFloat("Second", &settings.amount, 0.f, 1.f);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertNotIn(("Example", ("amount",)), index.bindings)

    def test_control_setting_inference_uses_only_the_storage_argument(self):
        self.assertIsNone(GENERATOR.extract_control_setting_path(
            "SliderFloat",
            ['"Value"', '&temporary', 'settings.Minimum', 'settings.Maximum'],
            {}))
        self.assertEqual(
            GENERATOR.extract_control_setting_path(
                "SliderScalar",
                ['"Value"', 'ImGuiDataType_U32', '&settings.Value', '&minimum', '&maximum'],
                {}),
            ("Value",))
        aliases = GENERATOR.collect_local_setting_aliases(
            "bool enabled = settings.Primary || settings.Secondary;")
        self.assertNotIn("enabled", aliases)

    def test_suffix_metadata_requires_an_unambiguous_type_owner(self):
        first = make_control_binding(
            "FirstSettings", ("value",), "First", "SliderFloat")
        second = make_control_binding(
            "SecondSettings", ("value",), "Second", "SliderFloat")
        nested = make_control_binding(
            "ContainerSettings", ("profile", "amount"), "Nested", "SliderFloat")
        index = make_control_index(first, second, nested)
        address = ("nested", "value")
        self.assertIsNone(index.match(("Feature",), address))
        self.assertEqual(
            index.match(("Feature",), address, ("FirstSettings",)).binding,
            first)
        self.assertIsNone(index.match(
            ("Feature",), address, ("FirstSettings", "SecondSettings")))
        self.assertEqual(index.match(
            ("Feature", "LeafRecord"),
            ("profiles", "0", "profile", "amount"),
            ("ContainerSettings", "LeafRecord")).binding, nested)

    def test_feature_owner_precedes_generic_settings_fallback(self):
        feature = make_control_binding(
            "Example", ("amount",), "Feature Amount", "SliderFloat")
        nested = make_control_binding(
            "PanelSettings", ("amount",), "Nested Amount", "InputFloat")
        generic = make_control_binding(
            "Settings", ("amount",), "Generic Amount", "DragFloat")
        index = make_control_index(feature, nested, generic)

        self.assertEqual(
            index.match(
                ("Example", "PanelSettings", "Settings"),
                ("amount",)).binding,
            feature)
        self.assertEqual(
            index.match(("Other", "PanelSettings", "Settings"),
                        ("amount",)).binding,
            nested)
        self.assertEqual(
            index.match(("Other", "Settings"), ("amount",)).binding,
            generic)

    def test_shift_slider_projects_metadata_through_helper(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        using PanelSettings = Example::PanelSettings;
        bool EditVector(const char* label, float* values)
        {
            return Util::ShiftSlider<3>(label, values, -2.f, 4.f);
        }
        void DrawPanel(PanelSettings& panel)
        {
            EditVector(T(TKEY("vector"), "Vector"), &panel.vector.x);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])

        binding = index.bindings[("PanelSettings", ("vector", "x"))]
        self.assertEqual(binding.control_kind, "ShiftSliderFloat3")
        self.assertEqual((binding.minimum, binding.maximum), (-2.0, 4.0))

    def test_member_selector_choices_label_returned_member_contexts(self):
        source = r'''
        #define I18N_KEY_PREFIX "feature.sample."
        struct Page { float amount; };
        struct Config { int selected; Page First; Page Second; };
        Page& SelectPage(Config& config) {
            switch (config.selected) {
            case 1: return config.Second;
            default: return config.First;
            }
        }
        void Panel::DrawSettings(Config& settings) {
            if (ImGui::Button(T(TKEY("first"), "First"))) settings.selected = 0;
            if (ImGui::Button(T(TKEY("second"), "Second"))) settings.selected = 1;
            Page& page = SelectPage(settings);
            ImGui::SliderFloat("Amount", &page.amount, 0.f, 1.f);
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Panel.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertEqual(index.contexts[("Config", ("First",))],
                         GENERATOR.LocalizedText(
                             "First", "feature.sample.first"))
        self.assertEqual(index.contexts[("Config", ("Second",))],
                         GENERATOR.LocalizedText(
                             "Second", "feature.sample.second"))

    def test_index_selector_labels_repeated_array_contexts(self):
        source = r'''
        #define I18N_KEY_PREFIX "feature.sample."
        struct Option { const char* label; float marker; };
        std::array<Option, 3> ProvideOptions() { return {
            Option{ T(TKEY("first"), "First"), 0.f },
            Option{ T(TKEY("second"), "Second"), 1.f },
            Option{ T(TKEY("third"), "Third"), 2.f },
        }; }
        void DrawOption(const Option& option, size_t index, int& selected) {
            if (ImGui::Button("##Pick")) selected = static_cast<int>(index);
            ImGui::TextUnformatted(option.label);
        }
        void DrawSelector(int& selected) {
            const auto options = ProvideOptions();
            for (size_t index = 0; index < options.size(); ++index)
                DrawOption(options[index], index, selected);
        }
        void Panel::DrawSettings() {
            static int selected = 0;
            DrawSelector(selected);
            ImGui::SliderFloat("Amount", &settings.values[selected].amount, 0.f, 1.f);
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Panel.cpp"
            path.write_text(source, encoding="utf-8")
            contexts = GENERATOR.collect_index_selector_contexts([path])
        self.assertEqual(contexts[("Panel", ("values", "0"))],
                         ("First", "feature.sample.first"))
        self.assertEqual(contexts[("Panel", ("values", "1"))],
                         ("Second", "feature.sample.second"))
        self.assertEqual(contexts[("Panel", ("values", "2"))],
                         ("Third", "feature.sample.third"))

    def test_choices_require_two_values_and_reject_conflicts(self):
        identity = ("Example", ("Mode",))
        self.assertEqual(GENERATOR.finalize_ui_choices({
            identity: [(0, "Reset", "")]
        }), {})
        self.assertEqual(GENERATOR.finalize_ui_choices({
            identity: [(0, "Mode A", ""), (0, "Other A", ""),
                       (1, "Mode B", "")]
        }), {})
        self.assertEqual(GENERATOR.finalize_ui_choices({
            identity: [(0, "Mode A", ""), (0, "Mode A", ""),
                       (1, "Mode B", "")]
        })[identity], ((0, "Mode A", ""), (1, "Mode B", "")))

    def test_one_hop_combo_helper_binds_to_typed_setting_member(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        using PageSettings = Example::NestedSettings;

        const char* GetModeLabel(uint32_t value)
        {
            switch (value) {
            case 0: return T(TKEY("mode_a"), "Mode A");
            case 1: return T(TKEY("mode_b"), "Mode B");
            default: return T(TKEY("mode_a"), "Mode A");
            }
        }

        bool DrawModeCombo(uint32_t& storedMode)
        {
            const char* labels[] = { GetModeLabel(0), GetModeLabel(1), };
            int currentMode = static_cast<int>(storedMode);
            const bool changed = ImGui::Combo(
                T(TKEY("mode"), "Mode"), &currentMode, labels, 2);
            if (changed)
                storedMode = static_cast<uint32_t>(currentMode);
            return changed;
        }

        void DrawPageSettings(PageSettings& pageSettings)
        {
            DrawModeCombo(pageSettings.mode);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        identity = ("NestedSettings", ("mode",))
        binding = index.bindings[identity]
        self.assertEqual(binding.label, GENERATOR.LocalizedText(
            "Mode", "feature.example.mode"))
        self.assertEqual(binding.category, GENERATOR.LocalizedText())
        self.assertEqual(binding.control_kind, "Combo")
        self.assertEqual(binding.choices, (
            (0, "Mode A", "feature.example.mode_a"),
            (1, "Mode B", "feature.example.mode_b")))

    def test_one_hop_combo_helper_rejects_transformed_proxy_values(self):
        source = r"""
        using PageSettings = Example::NestedSettings;

        bool DrawModeCombo(uint32_t& storedMode)
        {
            int currentMode = static_cast<int>(storedMode / 10);
            const bool changed = ImGui::Combo("Mode", &currentMode, "Mode A\0Mode B\0");
            if (changed)
                storedMode = static_cast<uint32_t>(currentMode * 10);
            return changed;
        }

        void DrawPageSettings(PageSettings& pageSettings)
        {
            DrawModeCombo(pageSettings.mode);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertNotIn(("NestedSettings", ("mode",)), index.bindings)

    def test_one_hop_combo_helper_rejects_overloaded_helper_names(self):
        source = r"""
        using FirstSettings = Example::FirstSettings;
        using SecondSettings = Example::SecondSettings;

        bool DrawModeCombo(uint32_t& mode)
        {
            return ImGui::Combo("First Mode", (int*)&mode, "First A\0First B\0");
        }

        bool DrawModeCombo(int& mode)
        {
            return ImGui::Combo("Second Mode", &mode, "Second A\0Second B\0");
        }

        void DrawFirstSettings(FirstSettings& settings)
        {
            DrawModeCombo(settings.mode);
        }

        void DrawSecondSettings(SecondSettings& settings)
        {
            DrawModeCombo(settings.mode);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertNotIn(("FirstSettings", ("mode",)), index.bindings)
        self.assertNotIn(("SecondSettings", ("mode",)), index.bindings)

    def test_mapped_combo_helper_uses_stored_values_across_sources(self):
        helper_source = """
        constexpr std::array<const char*, 4> labels = { "128", "256", "512", "1024" };
        constexpr std::array<int, labels.size()> values = { 128, 256, 512, 1024 };

        bool ResolutionCombo(const char* label, int& resolution)
        {
            int index = 0;
            if (!ImGui::Combo(label, &index, labels.data(), static_cast<int>(labels.size())))
                return false;
            resolution = values[index];
            return true;
        }
        """
        feature_source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            Widgets::ResolutionCombo(
                T(TKEY("resolution"), "Resolution"), settings.Resolution);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            helper_path = Path(directory) / "Widgets.cpp"
            feature_path = Path(directory) / "Example.cpp"
            helper_path.write_text(helper_source, encoding="utf-8")
            feature_path.write_text(feature_source, encoding="utf-8")
            index = GENERATOR.collect_control_index(
                [helper_path, feature_path])
        identity = ("Example", ("Resolution",))
        binding = index.bindings[identity]
        self.assertEqual(binding.label, GENERATOR.LocalizedText(
            "Resolution", "feature.example.resolution"))
        self.assertEqual(binding.category, GENERATOR.LocalizedText())
        self.assertEqual(binding.control_kind, "Combo")
        self.assertEqual(binding.choices, (
            (128, "128", ""), (256, "256", ""),
            (512, "512", ""), (1024, "1024", "")))

    def test_mapped_combo_helper_rejects_incomplete_mapping(self):
        source = """
        constexpr std::array<const char*, 2> labels = { "Low", "High" };
        constexpr std::array<int, 3> values = { 128, 256, 512 };
        bool ResolutionCombo(const char* label, int& resolution)
        {
            int index = 0;
            if (!ImGui::Combo(label, &index, labels.data(), 2))
                return false;
            resolution = values[index];
            return true;
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Widgets.cpp"
            path.write_text(source, encoding="utf-8")
            helpers = GENERATOR.collect_mapped_combo_helpers([path])
        self.assertEqual(helpers, {})

    def test_combo_choices_resolve_zero_argument_array_provider(self):
        provider_source = """
        inline const auto& AvailableModes()
        {
            static auto modes = std::array{ "First", "Second", "Third" };
            return modes;
        }
        """
        feature_source = """
        void Example::DrawSettings()
        {
            auto& modes = AvailableModes();
            ImGui::Combo("Mode", &settings.Mode, modes.data(), (int)modes.size());
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            provider_path = Path(directory) / "Modes.h"
            feature_path = Path(directory) / "Example.cpp"
            provider_path.write_text(provider_source, encoding="utf-8")
            feature_path.write_text(feature_source, encoding="utf-8")
            index = GENERATOR.collect_control_index(
                [feature_path], [provider_path, feature_path])
        self.assertEqual(index.bindings[("Example", ("Mode",))].choices, (
            (0, "First", ""), (1, "Second", ""), (2, "Third", "")))

    def test_array_provider_rejects_ambiguous_definitions(self):
        provider_source = """
        inline const auto& AvailableModes()
        {
            static auto modes = std::array{ "First", "Second" };
            return modes;
        }
        """
        feature_source = """
        void Example::DrawSettings()
        {
            auto& modes = AvailableModes();
            ImGui::Combo("Mode", &settings.Mode, modes.data(), (int)modes.size());
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "First.h"
            second = Path(directory) / "Second.h"
            feature = Path(directory) / "Example.cpp"
            first.write_text(provider_source, encoding="utf-8")
            second.write_text(provider_source, encoding="utf-8")
            feature.write_text(feature_source, encoding="utf-8")
            index = GENERATOR.collect_control_index(
                [feature], [first, second, feature])
        self.assertEqual(
            index.bindings[("Example", ("Mode",))].choices,
            ())

    def test_combo_choice_array_accepts_a_trailing_comma(self):
        body = 'const char* labels[] = { "First", "Second", };'
        self.assertEqual(
            GENERATOR.resolve_combo_choices(body, len(body), "labels", ""),
            [(0, "First", ""), (1, "Second", "")])

    def test_checkbox_control_is_detected(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            ImGui::Checkbox(T(TKEY("enable"), "Enable"),
                            (bool*)&settings.Enable);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertEqual(
            index.bindings[("Example", ("Enable",))].control_kind,
            "Checkbox")

    def test_only_draw_settings_body_is_scanned(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        void Example::DrawSettings()
        {
            ImGui::SliderFloat(T(TKEY("inside"), "Inside"), &settings.Inside, 0.0f, 1.0f);
        }
        void Example::DrawDebug()
        {
            ImGui::SliderFloat(T(TKEY("outside"), "Outside"), &settings.Outside, 0.0f, 1.0f);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])
        self.assertIn(("Example", ("Inside",)), index.bindings)
        self.assertNotIn(("Example", ("Outside",)), index.bindings)

    def test_brace_scoped_categories_match_sss(self):
        base = self.entries_by_id[("SubsurfaceScattering", "BaseProfile", "BlurRadius")]
        human = self.entries_by_id[("SubsurfaceScattering", "HumanProfile", "BlurRadius")]
        samples = self.entries_by_id[("SubsurfaceScattering", "", "BurleySamples")]
        self.assertEqual(base["displayPath"], "Base Profile")
        self.assertEqual(human["displayPath"], "Human Profile")
        self.assertEqual(samples["displayPath"], "Settings")

    def test_brace_scoped_categories_match_exponential_height_fog(self):
        root = self.entries_by_id[("ExponentialHeightFog", "", "fogDensity")]
        volumetric = self.entries_by_id[
            ("ExponentialHeightFog", "", "volumetricFogDistance")]
        debug = self.entries_by_id[
            ("ExponentialHeightFog", "", "volumetricGridSizeZ")]
        self.assertEqual(root["displayPath"], "")
        self.assertEqual(volumetric["displayPath"], "Volumetric Fog")
        self.assertEqual(debug["displayPath"], "Debug")
        self.assertIn("SceneControllable", debug["flags"])

    def test_safe_editor_metadata(self):
        ripple_lifetime = self.entries_by_id[
            ("WetnessEffects", "", "RippleLifetime")]
        resolution_mode = self.entries_by_id[
            ("ScreenSpaceGI", "", "ResolutionMode")]
        fog_density = self.entries_by_id[
            ("ExponentialHeightFog", "", "fogDensity")]
        self.assertEqual(ripple_lifetime["editorSemantic"], "Numeric")
        self.assertFalse(ripple_lifetime["hasNumericBounds"])
        self.assertIn("SceneControllable", ripple_lifetime["flags"])
        self.assertEqual(resolution_mode["editorSemantic"], "Choice")
        self.assertEqual([choice[0] for choice in resolution_mode["choices"]], [0, 1, 2])
        self.assertEqual((fog_density["minimum"], fog_density["maximum"]), (0.0, 1.0))

    def test_numeric_metadata_uses_raw_bounds_and_display_scale(self):
        percentage = self.entries_by_id[("ScreenSpaceGI", "", "GISaturation")]
        angle = self.entries_by_id[("Skylighting", "", "MaxZenith")]

        self.assertEqual((percentage["minimum"], percentage["maximum"]), (0.0, 1.0))
        self.assertEqual(percentage["displayScale"], 100.0)
        self.assertAlmostEqual(angle["minimum"], 0.0)
        self.assertAlmostEqual(angle["maximum"], math.pi / 2.0)
        self.assertAlmostEqual(angle["displayScale"], 180.0 / math.pi)

    def test_transformed_shift_slider_helper_preserves_numeric_transform(self):
        source = """
        #define I18N_KEY_PREFIX "feature.example."
        bool EditExposure(float* values)
        {
            float displayed[3];
            for (int index = 0; index < 3; ++index)
                displayed[index] = log2(values[index]);
            const bool changed = Util::ShiftSlider<3>(
                T(TKEY("exposure"), "Exposure"), displayed, -4.f, 4.f);
            for (int index = 0; index < 3; ++index)
                values[index] = exp2(displayed[index]);
            return changed;
        }
        void Example::DrawSettings()
        {
            EditExposure(&settings.exposure.x);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Example.cpp"
            path.write_text(source, encoding="utf-8")
            index = GENERATOR.collect_control_index([path])

        binding = index.bindings[("Example", ("exposure", "x"))]
        self.assertEqual(binding.control_kind, "ShiftSliderFloat3")
        self.assertEqual(binding.numeric_transform, "Log2")
        self.assertEqual((binding.minimum, binding.maximum), (0.0625, 16.0))

    def test_catalog_satisfies_the_build_time_minimum(self):
        GENERATOR.validate_entries(self.entries, *CMAKE_CATALOG_FLOORS)

    def test_catalog_validation_rejects_lost_scene_bindings(self):
        # Persisted-but-hidden entries alone must not satisfy the gate.
        persisted_only = [
            dict(entry, flags="SceneSettingsCatalog::SettingFlag::Persisted")
            for entry in self.entries]
        with self.assertRaises(ValueError):
            GENERATOR.validate_entries(persisted_only, *CMAKE_CATALOG_FLOORS)

    def test_catalog_validation_rejects_a_feature_losing_every_binding(self):
        # One feature's bindings vanishing must fail even while the totals stay healthy.
        largest = max(
            {entry["feature"] for entry in self.entries},
            key=lambda feature: sum(1 for entry in self.entries if entry["feature"] == feature))
        survivors = [entry for entry in self.entries if entry["feature"] != largest]
        padding = [
            dict(entry, key=f"{entry['key']}Pad{index}")
            for index, entry in enumerate(survivors[:len(self.entries) - len(survivors)])]
        with self.assertRaises(ValueError):
            GENERATOR.validate_entries(survivors + padding, *CMAKE_CATALOG_FLOORS)

    def test_settings_component_discovery_is_feature_agnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            feature_header = root / "CompositeFeature.h"
            feature_source = root / "CompositeFeature.cpp"
            component_header = root / "ExampleComponent.h"
            feature_header.write_text(
                "struct CompositeFeature : Feature {};", encoding="utf-8")
            feature_source.write_text(
                "components[0] = std::make_unique<ExampleComponent>();",
                encoding="utf-8")
            component_header.write_text(
                'struct ExampleComponent { '
                'std::string GetType() const { return "Example"; } '
                'std::string GetDisplayName() const { '
                'return T("feature.example.name", "Example Display"); } };',
                encoding="utf-8")
            features = {
                "CompositeFeature": {
                    "short": "CompositeFeature",
                    "name": "Composite Feature",
                    "source": str(feature_header),
                }
            }
            components = GENERATOR.collect_settings_components(
                features, [feature_header, feature_source, component_header])
        self.assertEqual(
            components["CompositeFeature"],
            [("ExampleComponent", "Example", "components",
              "Example Display", "feature.example.name")])

    def test_component_manual_boolean_persistence_requires_matching_ui(self):
        source = r'''
        components[0] = std::make_unique<ExampleComponent>();
        void CompositeFeature::DrawSettings() {
            for (auto& item : components)
                ImGui::Checkbox(T("feature.sample.enabled", "Enabled"), &item->enabled);
        }
        void CompositeFeature::SaveSettings(json& output) {
            for (auto& item : components)
                output[item->GetType()] = { { "enabled", item->enabled } };
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            feature_header = root / "CompositeFeature.h"
            feature_source = root / "CompositeFeature.cpp"
            component_header = root / "ExampleComponent.h"
            feature_header.write_text("struct CompositeFeature : Feature {};",
                                      encoding="utf-8")
            feature_source.write_text(source, encoding="utf-8")
            component_header.write_text(
                'struct ExampleComponent { '
                'std::string GetType() const { return "Example"; } };',
                encoding="utf-8")
            features = {"CompositeFeature": {
                "short": "CompositeFeature", "name": "Composite Feature",
                "source": str(feature_header)}}
            paths = [feature_header, feature_source, component_header]
            components = GENERATOR.collect_settings_components(features, paths)
            controls = GENERATOR.collect_component_persisted_controls(
                features, components)
        self.assertEqual(controls["CompositeFeature"], [
            ("components", "enabled", "enabled",
             ("Enabled", "feature.sample.enabled"))])

    def test_discovered_unconverted_control_uses_generic_fallback(self):
        binding = make_control_binding(
            "Example", ("flags",), "Flags", "CheckboxFlags")
        self.assertEqual(
            GENERATOR.resolve_editor_semantic(binding, "Integer"), "Generic")
        self.assertEqual(
            GENERATOR.resolve_editor_semantic(None, "Integer"), "None")
        self.assertEqual(
            GENERATOR.resolve_editor_semantic(binding, "Integer", True), "None")

    def test_unmatched_aggregate_components_stay_hidden(self):
        unmatched = [
            entry for entry in self.entries
            if entry["serializedComponent"] >= 0 and
            entry["editorSemantic"] == "None"
        ]
        self.assertTrue(unmatched)
        self.assertTrue(all("Hidden" in entry["flags"] for entry in unmatched))

    def test_generated_metadata_contains_display_scale(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            GENERATOR.write_catalog(self.entries, output)
            header = (output / "SceneSettingsCatalog.generated.h").read_text(encoding="utf-8")
            source = (output / "SceneSettingsCatalog.generated.cpp").read_text(encoding="utf-8")
        self.assertIn("double displayScale;", header)
        self.assertIn("NumericTransform numericTransform;", header)
        self.assertIn("std::string_view displayPathKeys;", header)
        self.assertIn("std::string_view componentDisplayName;", header)
        self.assertIn("bool aggregateAll;", header)
        self.assertIn("Generic,", header)
        self.assertIn(repr(180.0 / math.pi), source)

    def test_generated_find_setting_uses_sorted_catalog(self):
        entries = sorted(
            self.entries[:3],
            key=lambda entry: (entry["feature"], entry["path"], entry["key"]),
            reverse=True,
        )
        expected = sorted(
            entries,
            key=lambda entry: (entry["feature"], entry["path"], entry["key"]),
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            GENERATOR.write_catalog(entries, output)
            source = (output / "SceneSettingsCatalog.generated.cpp").read_text(
                encoding="utf-8")
        positions = [source.index(
            f'"{entry["feature"]}", "{entry["featureName"]}", '
            f'"{entry["path"]}", "{entry["key"]}"')
            for entry in expected]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("std::lower_bound(", source)
        self.assertNotIn("kSceneSettingLookupIndices", source)
        self.assertNotIn("for (const auto& setting : kSceneSettings)", source)

    def test_feature_adapters_are_separate_from_catalog_policy(self):
        component_entry = {
            "featureClass": "CompositeFeature",
            "feature": "CompositeFeature",
            "featureName": "Composite Feature",
            "include": "CompositeFeature.h",
            "path": "Example/settings",
            "key": "amount",
            "displayName": "Amount",
            "displayNameKey": "",
            "displayPath": "Example",
            "selectorPath": "Example",
            "selectorPathKeys": "-",
            "serializedPath": "Example/settings",
            "serializedKey": "amount",
            "serializedComponent": -1,
            "aggregateSemantic": "None",
            "aggregateStart": -1,
            "aggregateCount": 0,
            "type": "Float",
            "flags": "SceneSettingsCatalog::SettingFlag::Persisted",
            "editorSemantic": "Numeric",
            "minimum": 0.0,
            "maximum": 1.0,
            "displayScale": 1.0,
            "hasNumericBounds": True,
            "invertedDisplay": False,
            "choices": (),
            "access": "settings.amount",
            "componentClass": "ExampleComponent",
            "componentType": "Example",
            "componentContainer": "components",
        }
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            GENERATOR.write_catalog([component_entry], output)
            source = (output / "SceneSettingsCatalog.generated.cpp").read_text(encoding="utf-8")
            adapters = (output / "FeatureSceneSettingsAdapters.generated.cpp").read_text(
                encoding="utf-8")
        self.assertNotIn('#include "Features/', source)
        self.assertNotIn("static_cast<", source)
        self.assertIn("RegisterControlResolver", adapters)
        self.assertIn("typedFeature->components", adapters)
        self.assertIn('candidate->GetType() == "Example"', adapters)
        self.assertIn("static_cast<ExampleComponent*>", adapters)
        self.assertNotIn("FindSettingsComponent", adapters)

    def test_persisted_vectors_and_arrays_are_cataloged_as_scalar_components(self):
        vector_entries = [
            entry for entry in self.entries
            if entry["serializedComponent"] >= 0
        ]
        self.assertTrue(vector_entries)
        self.assertTrue(all(entry["type"] == "Float" for entry in vector_entries))
        self.assertTrue(all(
            entry["path"].startswith(entry["serializedPath"]) and
            entry["serializedKey"] and
            entry["serializedComponent"] < 4
            for entry in vector_entries))

    def test_choice_exceptions_are_complete(self):
        expected_choices = {
            ("ScreenSpaceGI", "ResolutionMode"): [0, 1, 2],
            ("SubsurfaceScattering", "SSMode"): [0, 1],
            ("SubsurfaceScattering", "ScatterMode"): [0, 1, 2],
            ("ImageBasedLighting", "DALCMode"): [0, 1, 2, 3],
        }
        for (feature, key), expected_values in expected_choices.items():
            entry = self.entries_by_id[(feature, "", key)]
            self.assertEqual(entry["editorSemantic"], "Choice")
            self.assertEqual(
                [choice[0] for choice in entry["choices"]],
                expected_values)

    def test_ui_bound_numeric_without_static_bounds_remains_controllable(self):
        ripple_lifetime = self.entries_by_id[
            ("WetnessEffects", "", "RippleLifetime")]
        self.assertEqual(ripple_lifetime["editorSemantic"], "Numeric")
        self.assertFalse(ripple_lifetime["hasNumericBounds"])
        self.assertIn("SceneControllable", ripple_lifetime["flags"])

    def test_hidden_persisted_values_have_no_editor(self):
        hidden = [
            entry for entry in self.entries
            if "Persisted" in entry["flags"] and "Hidden" in entry["flags"]
        ]
        self.assertTrue(hidden)
        self.assertTrue(all(entry["editorSemantic"] == "None" for entry in hidden))

    def test_allow_and_deny_policy_are_explicit_and_exclusive(self):
        for entry in self.entries:
            allowed = "SceneControllable" in entry["flags"]
            denied = "Hidden" in entry["flags"]
            self.assertNotEqual(allowed, denied)
            self.assertEqual(allowed, entry["editorSemantic"] != "None")

    def test_unmapped_source_widget_fails_validation(self):
        entries = [{
            "feature": "Foo", "path": "", "key": "Bar",
            "flags": "SceneSettingsCatalog::SettingFlag::SceneControllable",
            "editorSemantic": "Scalar", "sourceWidget": "KnobWidget",
        }]
        with self.assertRaises(ValueError) as caught:
            GENERATOR.validate_entries(entries, 1, 1, 1)
        self.assertIn("KnobWidget", str(caught.exception))

    def test_percentage_slider_maps_onto_slider_float(self):
        self.assertEqual(
            GENERATOR.SOURCE_WIDGET_ENTRY_POINTS["PercentageSlider"], ("SliderFloat",))
        entries = [{
            "feature": "Foo", "path": "", "key": "Bar",
            "flags": "SceneSettingsCatalog::SettingFlag::SceneControllable",
            "editorSemantic": "Scalar", "sourceWidget": "PercentageSlider",
        }]
        GENERATOR.validate_entries(entries, 1, 1, 1)
        self.assertEqual(GENERATOR.required_entry_points(entries), ["SliderFloat"])


if __name__ == "__main__":
    unittest.main()
