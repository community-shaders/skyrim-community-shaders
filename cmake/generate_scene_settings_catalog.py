
#!/usr/bin/env python3

"""Generates the scene settings catalog from the feature sources at build time.

Every scene-controllable setting is discovered by statically parsing C++: `SaveSettings`
bodies give the serialized address a scene override writes to, and `DrawSettings` bodies
give the editor metadata (label, category, bounds, choices) to present it with. There is no
runtime reflection and no hand-maintained list, so a setting that stops being parsed simply
disappears; the floors in `validate_entries` exist to make that loud instead of silent.

Two invariants the parsing rests on:

- `mask_cpp_source` is length-preserving, so an offset found in the masked view indexes the
  raw text unchanged. Match on the masked view (a commented-out line must never win over the
  live one), then slice the raw text to recover literals.
- A missing binding must degrade to "not scene-controllable", never to a wrong binding.
  `merge_unique` and the `add_metadata` priority ladder are both fail-closed for that reason.
"""

import argparse
import ast
import functools
import math
import re
from dataclasses import dataclass
from pathlib import Path


# Struct types that are debug state, never scene-controllable. Anything else non-primitive in a
# SaveSettings body fails the build, so an aggregate can never drop out of the catalog silently.
# Keyed by declared type rather than serialized key: a feature persisting some other struct under
# the name "DebugSettings" must still trip the guard, and naming features here would put policy in
# the generator instead of SceneSettingsPolicy.h.
UNCATALOGED_PERSISTED_AGGREGATE_TYPES = {
    "DebugSettings",
}

PRIMITIVE_TYPES = {
    "bool": "Boolean",
    "float": "Float",
    "double": "Float",
    "int": "Integer",
    "int8_t": "Integer",
    "int16_t": "Integer",
    "int32_t": "Integer",
    "int64_t": "Integer",
    "uint": "Integer",
    "uint8_t": "Integer",
    "uint16_t": "Integer",
    "uint32_t": "Integer",
    "uint64_t": "Integer",
    "std::int8_t": "Integer",
    "std::int16_t": "Integer",
    "std::int32_t": "Integer",
    "std::int64_t": "Integer",
    "std::uint8_t": "Integer",
    "std::uint16_t": "Integer",
    "std::uint32_t": "Integer",
    "std::uint64_t": "Integer",
    # Multi-word builtin spellings: without these a plain `unsigned int` member drops
    # out of the catalog instead of being cataloged as an integer.
    "unsigned": "Integer",
    "unsigned int": "Integer",
    "signed": "Integer",
    "signed int": "Integer",
    "short": "Integer",
    "unsigned short": "Integer",
    "long": "Integer",
    "unsigned long": "Integer",
    "long long": "Integer",
    "unsigned long long": "Integer",
    "size_t": "Integer",
    "std::size_t": "Integer",
    "std::string": "String",
    "string": "String",
}

INTEGRAL_TYPE_PATTERN = (
    r"(?:signed|unsigned|int|uint|long|short|size_t|std::size_t|"
    r"u?int(?:8|16|32|64)_t|std::u?int(?:8|16|32|64)_t)")

VECTOR_COMPONENTS = {
    "float2": ("x", "y"),
    "float3": ("x", "y", "z"),
    "float4": ("x", "y", "z", "w"),
    "DirectX::XMFLOAT2": ("x", "y"),
    "DirectX::XMFLOAT3": ("x", "y", "z"),
    "DirectX::XMFLOAT4": ("x", "y", "z", "w"),
    "ImVec2": ("x", "y"),
}
VECTOR_COMPONENT_NAMES = tuple(dict.fromkeys(
    component for components in VECTOR_COMPONENTS.values() for component in components))
MAX_VECTOR_COMPONENT_COUNT = max(len(components) for components in VECTOR_COMPONENTS.values())


@dataclass(frozen=True)
class CatalogContext:
    feature_class: str
    field_class: str
    json_path_prefix: tuple[str, ...] = ()
    display_path_prefix: tuple[str, ...] = ()
    selector_path_prefix: tuple[str, ...] = ()
    selector_key_prefix: tuple[str, ...] = ()
    component_class: str = ""
    component_type: str = ""
    component_container: str = ""


@dataclass(frozen=True)
class MappedComboHelper:
    storage_parameter: int
    label_parameter: int | None
    label: str
    label_key: str
    choices: tuple[tuple[int, str, str], ...]


@dataclass(frozen=True)
class ProjectedNumericHelper:
    parameters: tuple[tuple[str, str | None], ...]
    storage_parameter: int
    label_parameter: int
    component_labels_parameter: int
    component_count: int
    control_kind: str
    control_args: tuple[str, ...]


@dataclass(frozen=True)
class NumericControlFlow:
    storage_parameter: int
    storage_offset: int
    component_count: int
    item_label: str
    minimum_expression: str
    maximum_expression: str
    semantic: str
    presentation: str
    source_widget: str
    clamp_numeric_input: bool
    hdr_color: bool


@dataclass(frozen=True)
class LocalizedText:
    text: str = ""
    key: str = ""


@dataclass(frozen=True)
class SourceParameter:
    type_name: str
    name: str
    default: str | None = None


@dataclass(frozen=True)
class SourceFunction:
    name: str
    owner: str
    qualifier: tuple[str, ...]
    parameters: tuple[SourceParameter, ...]
    body: str
    masked_body: str
    prefix: str
    source: Path
    parameter_text: str


@dataclass(frozen=True)
class ControlBinding:
    owner: str
    path: tuple[str, ...]
    label: LocalizedText | None
    category: LocalizedText
    control_kind: str
    minimum: float | None = None
    maximum: float | None = None
    display_scale: float = 1.0
    numeric_transform: str = "Identity"
    choices: tuple[tuple[int, str, str], ...] = ()
    component_labels: tuple[LocalizedText, ...] = ()
    aggregate_all: bool = False
    source_widget: str = ""
    clamp_numeric_input: bool = False
    hdr_color: bool = False


@dataclass(frozen=True)
class BindingMatch:
    binding: ControlBinding
    matched_offset: int


@dataclass(frozen=True)
class ControlTemplate:
    storage_parameter: int
    storage_path: tuple[str, ...]
    label_expression: str
    category: LocalizedText
    control_kind: str
    control_args: tuple[str, ...]
    choices: tuple[tuple[int, str, str], ...] = ()


@dataclass
class ControlIndex:
    bindings: dict[tuple[str, tuple[str, ...]], ControlBinding]
    selectors: dict[tuple[str, tuple[str, ...]], tuple[tuple[str, ...], tuple[str, ...]]]
    contexts: dict[tuple[str, tuple[str, ...]], LocalizedText]
    headings: dict[tuple[str, tuple[str, ...]], LocalizedText]
    conflicts: set[tuple[str, tuple[str, ...]]]

    @staticmethod
    def _match_mapping(
            mapping,
            owners: tuple[str, ...],
            address: tuple[str, ...],
            suffix_owners: tuple[str, ...] = ()):
        addresses = (address, tuple("*" if part.isdigit() else part for part in address))

        def resolve_candidates(candidate_owners, candidate_addresses, offset):
            candidates = [
                (mapping[(owner, candidate)], offset)
                for candidate in dict.fromkeys(candidate_addresses)
                for owner in candidate_owners
                if (owner, candidate) in mapping
            ]
            if not candidates:
                return False, None
            resolved = (candidates[0]
                        if all(item[0] == candidates[0][0] for item in candidates[1:])
                        else None)
            return True, resolved

        for owner in owners:
            found, resolved = resolve_candidates((owner,), addresses, 0)
            if found:
                return resolved

        for offset in range(1, len(address)):
            sliced_addresses = tuple(candidate[offset:] for candidate in addresses)
            found, resolved = resolve_candidates(
                suffix_owners, sliced_addresses, offset)
            if found:
                return resolved
        return None

    def match(
            self,
            owners: tuple[str, ...],
            address: tuple[str, ...],
            suffix_owners: tuple[str, ...] = ()) -> BindingMatch | None:
        matched = self._match_mapping(
            self.bindings, owners, address, suffix_owners)
        return BindingMatch(matched[0], matched[1]) if matched else None

    def match_context(
            self,
            owners: tuple[str, ...],
            address: tuple[str, ...],
            suffix_owners: tuple[str, ...] = ()) -> tuple[LocalizedText, int] | None:
        return self._match_mapping(
            self.contexts, owners, address, suffix_owners)


def resolve_vector_binding(
        control_index: ControlIndex,
        owners: tuple[str, ...],
        base_address: tuple[str, ...],
        components: tuple[str, ...],
        component_index: int,
        suffix_owners: tuple[str, ...] = ()) -> tuple[BindingMatch | None, int, int, str]:
    addresses = [(component_index, (*base_address, components[component_index]))]
    addresses.extend(
        (start, (*base_address, components[start]))
        for start in range(component_index - 1, -1, -1))
    addresses.append((0, base_address))
    seen = set()
    for start, address in addresses:
        if address in seen:
            continue
        seen.add(address)
        match = control_index.match(owners, address, suffix_owners)
        if not match:
            continue
        count = min(
            control_component_count(match.binding.control_kind),
            len(components) - start)
        if start <= component_index < start + count:
            return match, start, count, control_group_semantic(match.binding.control_kind)
    return None, -1, 0, "None"


STRUCT_DECL_RE = r"\bstruct\s+(?:alignas\s*\([^)]*\)\s+)?(\w+)([^;{]*)\{"

CATEGORY_CONTROL_NAMES = {
    "CollapsingHeader",
    "TreeNode",
    "TreeNodeEx",
}

PERSISTENT_CATEGORY_CONTROL_NAMES = {
    "DrawSectionHeader",
    "SeparatorText",
}

DIRECT_UI_CONTROL_RE = re.compile(
    r"(?:ImGui|Util)::(Checkbox|InvertedCheckbox|CheckboxFlags|RadioButton|Combo|BeginCombo|"
    r"Drag(?:Float[234]?|Int[234]?|ScalarN?)|"
    r"Slider(?:Float[234]?|Int[234]?|ScalarN?|Angle)|"
    r"Input(?:Float[234]?|Int[234]?|ScalarN?)|ColorEdit[34]|PercentageSlider)\s*\(")

SHIFT_UNIFIED_CONTROL_RE = re.compile(
    r"\bUtil::ShiftSlider\s*<\s*([234])\s*>\s*\(")




@functools.lru_cache(maxsize=None)
def read_text(path: Path) -> str:
    """Source of a file. Cached: every collector re-reads the same tree."""
    return path.read_text(encoding="utf-8", errors="ignore")


@functools.lru_cache(maxsize=None)
def masked_text(path: Path) -> str:
    """Comment/string-masked view of a file. Length-preserving, so indices are
    interchangeable with read_text(path) -- match on this, slice the raw text."""
    return mask_cpp_source(read_text(path))


def cpp_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def prettify(identifier: str) -> str:
    if not identifier:
        return identifier
    text = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", identifier)
    text = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1 \2", text)
    text = text.replace("_", " ")
    return text[:1].upper() + text[1:]


def normalize_display_text(value: str) -> str:
    return "".join(character.casefold() for character in value if character.isalnum())


def encode_catalog_segment(value: str) -> str:
    return value.replace("~", "~0").replace("/", "~1")


def join_catalog_path(parts) -> str:
    return "/".join(encode_catalog_segment(str(part)) for part in parts)


def split_args(arg_text: str) -> list[str]:
    args = []
    current = []
    depth = 0
    in_string = False
    escaped = False
    for ch in arg_text:
        if in_string:
            current.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            current.append(ch)
        elif ch in "([{<":
            depth += 1
            current.append(ch)
        elif ch in ")]}>":
            depth = max(0, depth - 1)
            current.append(ch)
        elif ch == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    if current:
        args.append("".join(current).strip())
    return args


@functools.lru_cache(maxsize=None)
def _bracket_scanner(open_ch: str, close_ch: str) -> re.Pattern[str]:
    """Matches only the characters that can change bracket-scanning state."""
    return re.compile("[" + re.escape(open_ch + close_ch) + '"/]')


def find_matching_bracket(text: str, open_index: int, open_ch: str, close_ch: str) -> int:
    """Index of the bracket closing the one at open_index, or -1.

    Skips string literals and comments. Callers pass raw source as often as masked
    source, and an unbalanced bracket inside a comment would otherwise silently
    truncate a function body and drop every control in it.
    """
    depth = 0
    i = open_index
    length = len(text)
    # Jump between state-changing characters in C rather than walking every character in Python.
    search = _bracket_scanner(open_ch, close_ch).search
    while i < length:
        match = search(text, i)
        if not match:
            return -1
        i = match.start()
        ch = text[i]
        if ch == '"':
            i += 1
            while i < length:
                quoted = text[i]
                if quoted == "\\":
                    i += 2
                    continue
                if quoted == '"':
                    break
                i += 1
            i += 1
            continue
        if ch == "/":
            if text.startswith("//", i):
                newline = text.find("\n", i)
                i = length if newline < 0 else newline
            elif text.startswith("/*", i):
                end = text.find("*/", i + 2)
                i = length if end < 0 else end + 2
            else:
                i += 1
            continue
        if ch == open_ch:
            depth += 1
        else:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def find_matching_paren(text: str, open_index: int) -> int:
    return find_matching_bracket(text, open_index, "(", ")")


def find_matching_brace(text: str, open_index: int) -> int:
    return find_matching_bracket(text, open_index, "{", "}")


def collect_nlohmann_macros(paths: list[Path]) -> dict[str, list[str]]:
    macros: dict[str, list[str]] = {}
    conflicts: list[str] = []
    token = "NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT"
    for path in paths:
        # Type names and field names survive masking, so the masked view is enough here.
        text = masked_text(path)
        pos = 0
        while True:
            start = text.find(token, pos)
            if start < 0:
                break
            open_index = text.find("(", start + len(token))
            if open_index < 0:
                break
            close_index = find_matching_paren(text, open_index)
            if close_index < 0:
                break
            args = split_args(text[open_index + 1:close_index])
            if len(args) >= 2:
                type_name = args[0].strip()
                fields = [a.strip().rstrip(";") for a in args[1:] if a.strip()]
                if macros.get(type_name, fields) != fields:
                    conflicts.append(
                        f"{type_name} is declared with conflicting field lists "
                        f"({macros[type_name]} vs {fields})")
                macros[type_name] = fields
            pos = close_index + 1
    if conflicts:
        raise SystemExit("conflicting nlohmann type definitions:\n  " + "\n  ".join(conflicts))
    return macros


def collect_struct_bodies(paths: list[Path]) -> dict[str, str]:
    bodies: dict[str, str] = {}
    for path in paths:
        text = read_text(path)
        for match in re.finditer(STRUCT_DECL_RE, text):
            name = match.group(1)
            body_start = match.end()
            depth = 1
            i = body_start
            in_string = False
            escaped = False
            while i < len(text):
                ch = text[i]
                if in_string:
                    if escaped:
                        escaped = False
                    elif ch == "\\":
                        escaped = True
                    elif ch == '"':
                        in_string = False
                elif ch == '"':
                    in_string = True
                elif ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        bodies.setdefault(name, text[body_start:i])
                        break
                i += 1
    return bodies


def clean_type(type_name: str) -> str:
    type_name = re.sub(r"\b(const|volatile|mutable|static|inline|constexpr)\b", "", type_name)
    type_name = type_name.replace("&", "").replace("*", "").strip()
    type_name = re.sub(r"\s+", " ", type_name)
    return type_name


def parse_struct_fields(body: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for raw_stmt in mask_cpp_source(body).split(";"):
        parsed = parse_field_statement(raw_stmt)
        if parsed:
            fields[parsed[0]] = parsed[1]
    return fields


def parse_field_statement(raw_statement: str) -> tuple[str, str] | None:
    statement = raw_statement.strip()
    statement = re.sub(r"\b(public|private|protected)\s*:\s*", "", statement).strip()
    if not statement or statement.startswith(
            ("using ", "enum ", "static ", "static_assert", "STATIC_ASSERT", "return ")):
        return None
    statement = re.sub(r"=\s*.*$", "", statement, flags=re.DOTALL).strip()
    statement = re.sub(r"\s*\{.*\}\s*$", "", statement, flags=re.DOTALL).strip()
    statement = re.sub(r"\[[^\]]*\]", "", statement).strip()
    if not statement or "(" in statement or ")" in statement:
        return None
    match = re.match(r"(.+?)\s+([A-Za-z_]\w*)$", statement)
    if not match:
        return None
    return match.group(2), clean_type(match.group(1))


def parse_top_level_fields(body: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    masked = mask_cpp_source(body)
    statement_start = 0
    index = 0
    while index < len(masked):
        if masked[index] == "{":
            end = find_matching_brace(masked, index)
            if end < 0:
                break
            prefix = masked[statement_start:index]
            if not re.search(r"\b(struct|class|enum)\b", prefix):
                parsed = parse_field_statement(prefix)
                if parsed:
                    fields[parsed[0]] = parsed[1]
            statement_start = end + 1
            index = end + 1
            continue
        if masked[index] == ";":
            parsed = parse_field_statement(masked[statement_start:index])
            if parsed:
                fields[parsed[0]] = parsed[1]
            statement_start = index + 1
        index += 1
    return fields


def parse_struct_bases(declaration_tail: str) -> list[str]:
    if ":" not in declaration_tail:
        return []
    bases = []
    for base in split_args(declaration_tail.split(":", 1)[1]):
        base = re.sub(r"\b(public|private|protected|virtual)\b", "", base)
        base = clean_type(base)
        if base:
            bases.append(base)
    return bases


def collect_feature_struct_fields(paths: list[Path], features: dict[str, dict[str, str]]) -> dict[str, dict[str, dict[str, str]]]:
    raw_feature_fields: dict[str, dict[str, dict[str, str]]] = {}
    feature_bases: dict[str, dict[str, list[str]]] = {}
    for path in paths:
        text = read_text(path)
        for feature_class in features:
            feature_match = re.search(rf"\b(?:struct|class)\s+{re.escape(feature_class)}\b[^\{{;]*\{{", text)
            if not feature_match:
                continue
            feature_end = find_matching_brace(text, feature_match.end() - 1)
            if feature_end < 0:
                continue

            feature_body = text[feature_match.end():feature_end]
            for struct_match in re.finditer(STRUCT_DECL_RE, feature_body):
                body_start = struct_match.end()
                body_end = find_matching_brace(feature_body, body_start - 1)
                if body_end < 0:
                    continue
                fields = parse_struct_fields(feature_body[body_start:body_end])
                struct_name = struct_match.group(1)
                raw_feature_fields.setdefault(feature_class, {})[struct_name] = fields
                feature_bases.setdefault(feature_class, {})[struct_name] = parse_struct_bases(struct_match.group(2))

    feature_fields: dict[str, dict[str, dict[str, str]]] = {}
    for feature_class, structs in raw_feature_fields.items():
        resolved: dict[str, dict[str, str]] = {}

        def resolve_fields(struct_name: str, active: set[str] | None = None) -> dict[str, str]:
            if struct_name in resolved:
                return resolved[struct_name]
            active = set() if active is None else active
            if struct_name in active:
                return dict(structs.get(struct_name, {}))
            merged: dict[str, str] = {}
            for base in feature_bases.get(feature_class, {}).get(struct_name, []):
                base_name = base.split("::")[-1]
                if base_name in structs:
                    merged.update(resolve_fields(base_name, active | {struct_name}))
            merged.update(structs.get(struct_name, {}))
            resolved[struct_name] = merged
            return merged

        for struct_name in structs:
            resolve_fields(struct_name)
        feature_fields[feature_class] = resolved
    return feature_fields


def collect_feature_member_fields(paths: list[Path], features: dict[str, dict[str, str]]) -> dict[str, dict[str, str]]:
    feature_members: dict[str, dict[str, str]] = {}
    for path in paths:
        text = read_text(path)
        for feature_class in features:
            feature_match = re.search(rf"\b(?:struct|class)\s+{re.escape(feature_class)}\b[^\{{;]*\{{", text)
            if not feature_match:
                continue
            feature_end = find_matching_brace(text, feature_match.end() - 1)
            if feature_end < 0:
                continue

            raw_feature_body = text[feature_match.end():feature_end]
            members = parse_top_level_fields(raw_feature_body)
            for struct_match in re.finditer(STRUCT_DECL_RE, raw_feature_body):
                body_end = find_matching_brace(raw_feature_body, struct_match.end() - 1)
                if body_end < 0:
                    continue
                tail = raw_feature_body[body_end + 1:raw_feature_body.find(";", body_end)]
                for declarator in split_args(tail):
                    declarator = re.sub(r"[={].*$", "", declarator).strip().lstrip("*&")
                    member_match = re.match(r"([A-Za-z_]\w*)", declarator)
                    if member_match:
                        members[member_match.group(1)] = struct_match.group(1)
            feature_members[feature_class] = members
    return feature_members


def collect_save_roots(paths: list[Path]) -> dict[str, str]:
    roots: dict[str, str] = {}
    for path in paths:
        # Masked: a commented-out assignment must not win over the live one.
        text = masked_text(path)
        for match in re.finditer(r"\bvoid\s+(\w+)::SaveSettings\s*\([^)]*\)\s*\{", text):
            feature_class = match.group(1)
            body_end = find_matching_brace(text, match.end() - 1)
            if body_end < 0:
                continue
            body = text[match.end():body_end]
            assignment = re.search(r"\b(?:\w+|this->\w+)\s*=\s*(?:this->)?([A-Za-z_]\w*)\s*;", body)
            if assignment:
                roots[feature_class] = assignment.group(1)
    return roots


def collect_direct_persisted_fields(
        paths: list[Path],
        feature_members: dict[str, dict[str, str]]) -> dict[str, list[tuple[str, str, str]]]:
    persisted_fields: dict[str, list[tuple[str, str, str]]] = {}
    uncataloged: list[str] = []
    for path in paths:
        # Masked for matching so commented-out persistence cannot register a phantom key;
        # the JSON key itself is blanked by masking, so read it back out of the raw text.
        raw = read_text(path)
        text = masked_text(path)
        for match in re.finditer(r"\bvoid\s+(\w+)::SaveSettings\s*\([^)]*\)\s*\{", text):
            feature_class = match.group(1)
            body_end = find_matching_brace(text, match.end() - 1)
            if body_end < 0:
                continue
            offset = match.end()
            body = text[offset:body_end]
            for assignment in re.finditer(
                    r'\b[A-Za-z_]\w*\s*\[\s*"([^"]+)"\s*\]\s*=\s*(?:this->)?([A-Za-z_]\w*)\s*;',
                    body):
                key = raw[offset + assignment.start(1):offset + assignment.end(1)]
                member = assignment.group(2)
                declared_type = feature_members.get(feature_class, {}).get(member, "")
                value_type = type_to_value_type(declared_type)
                if value_type:
                    persisted_fields.setdefault(feature_class, []).append(
                        (key, value_type, member))
                elif declared_type and declared_type not in UNCATALOGED_PERSISTED_AGGREGATE_TYPES:
                    uncataloged.append(
                        f"{feature_class}::SaveSettings persists '{key}' as non-primitive "
                        f"'{declared_type}'; catalog it or list it in "
                        f"UNCATALOGED_PERSISTED_AGGREGATE_TYPES")
    if uncataloged:
        raise SystemExit("uncataloged persisted aggregates:\n  " + "\n  ".join(uncataloged))
    return persisted_fields


def collect_string_constants(text: str) -> dict[str, str]:
    constants: dict[str, str] = {}
    pattern = re.compile(
        r'\bconstexpr\b[^;=\n]*?\b([A-Za-z_]\w*)\s*=\s*"([^"]*)"(?:sv)?\s*;')
    for match in pattern.finditer(text):
        constants[match.group(1)] = match.group(2)
    return constants


def get_function_return_expression(text: str, function_name: str) -> str | None:
    match = re.search(rf'\b{re.escape(function_name)}\s*\([^)]*\)[^{{;]*\{{', text)
    if not match:
        return None
    body_end = find_matching_brace(text, match.end() - 1)
    if body_end < 0:
        return None
    body = text[match.end():body_end]
    return_match = re.search(r'\breturn\s+(.+?)\s*;', body, flags=re.DOTALL)
    return return_match.group(1).strip() if return_match else None


def resolve_string_expression(expression: str, text: str, constants: dict[str, str],
                              visited: set[str] | None = None) -> str | None:
    visited = set() if visited is None else visited
    expression = expression.strip()
    literal = re.fullmatch(r'"([^"]*)"(?:sv)?', expression)
    if literal:
        return literal.group(1)

    wrapper = re.fullmatch(r'(?:std::)?(?:string|string_view)\s*[({]\s*(.+?)\s*[)}]', expression, flags=re.DOTALL)
    if wrapper:
        return resolve_string_expression(wrapper.group(1), text, constants, visited)

    identifier = re.fullmatch(r'(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)', expression)
    if identifier:
        name = identifier.group(1)
        return constants.get(name)

    helper = re.fullmatch(r'(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\s*\(\s*\)', expression)
    if helper:
        name = helper.group(1)
        if name in visited:
            return None
        helper_expression = get_function_return_expression(text, name)
        if helper_expression:
            return resolve_string_expression(helper_expression, text, constants, visited | {name})
    return None


def collect_features(paths: list[Path]) -> dict[str, dict[str, str]]:
    features: dict[str, dict[str, str]] = {}
    for path in paths:
        text = read_text(path)
        class_match = re.search(r"\b(?:struct|class)\s+(\w+)[^{:;]*(?::[^{]+Feature)", text)
        if not class_match:
            continue
        class_name = class_match.group(1)
        constants = collect_string_constants(text)
        short_expression = get_function_return_expression(text, "GetShortName")
        short_name = resolve_string_expression(short_expression, text, constants) if short_expression else None
        if not short_name:
            continue
        name_expression = get_function_return_expression(text, "GetName")
        display_name = resolve_string_expression(name_expression, text, constants) if name_expression else None
        features[class_name] = {
            "short": short_name,
            "name": display_name or short_name,
            "source": str(path),
        }
    return features


def collect_settings_components(
        features: dict[str, dict[str, str]], paths: list[Path]) -> dict[
            str, list[tuple[str, str, str, str, str]]]:
    class_source_candidates: dict[str, set[Path]] = {}
    for path in (path for path in paths if path.suffix == ".h"):
        text = read_text(path)
        for match in re.finditer(STRUCT_DECL_RE, text):
            class_name = match.group(1)
            class_source_candidates.setdefault(class_name, set()).add(path)
    class_sources = {
        class_name: next(iter(candidates))
        for class_name, candidates in class_source_candidates.items()
        if len(candidates) == 1
    }

    components: dict[str, list[tuple[str, str, str]]] = {}
    for feature_class, feature in features.items():
        source_path = Path(feature["source"]).with_suffix(".cpp")
        if not source_path.exists():
            continue
        text = read_text(source_path)
        feature_components = []
        seen_classes = set()
        for container_name, child_type in re.findall(
                r"\b([A-Za-z_]\w*)\s*\[[^\]]+\]\s*=\s*"
                r"std::make_unique\s*<\s*([A-Za-z_]\w*(?:::\w+)*)\s*>", text):
            child_name = child_type.split("::")[-1]
            if child_name in seen_classes:
                continue
            child_source = class_sources.get(child_name)
            if not child_source:
                continue
            child_text = read_text(child_source)
            type_expression = get_function_return_expression(child_text, "GetType")
            type_name = resolve_string_expression(
                type_expression, child_text, collect_string_constants(child_text)) if type_expression else None
            if type_name:
                display_expression = get_function_return_expression(
                    child_text, "GetDisplayName")
                prefix_match = re.search(
                    r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', child_text)
                translated = extract_i18n_call(
                    display_expression or "",
                    prefix_match.group(1) if prefix_match else "")
                display_key, display_name = translated if translated else (
                    "", resolve_string_expression(
                        display_expression, child_text,
                        collect_string_constants(child_text))
                    if display_expression else type_name)
                feature_components.append((
                    child_name, type_name, container_name,
                    display_name or type_name, display_key))
                seen_classes.add(child_name)
        if feature_components:
            components[feature_class] = feature_components
    return components


def collect_component_persisted_controls(
        features: dict[str, dict[str, str]], settings_components):
    result = {}
    alias_pattern = re.compile(
        r"\b(?:auto|[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*&\s*([A-Za-z_]\w*)\s*"
        r"(?:\:\s*([A-Za-z_]\w*)|=\s*([A-Za-z_]\w*)\s*\[)")
    for feature_class, components in settings_components.items():
        source = Path(features[feature_class]["source"]).with_suffix(".cpp")
        if not source.exists():
            continue
        text = read_text(source)
        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        method_bodies = {}
        for method_name in ("DrawSettings", "SaveSettings"):
            method = re.search(
                rf"\bvoid\s+{re.escape(feature_class)}::{method_name}\s*\([^)]*\)\s*\{{",
                mask_cpp_source(text))
            body_end = find_matching_brace(text, method.end() - 1) if method else -1
            if method and body_end >= 0:
                method_bodies[method_name] = text[method.end():body_end]
        if len(method_bodies) != 2:
            continue

        aliases = {
            alias.group(1): alias.group(2) or alias.group(3)
            for body in method_bodies.values()
            for alias in alias_pattern.finditer(mask_cpp_source(body))
        }
        containers = {component[2] for component in components}
        persisted = {}
        for pair in re.finditer(
                r'\{\s*"([^"]+)"\s*,\s*([A-Za-z_]\w*)\s*->\s*'
                r'([A-Za-z_]\w*)\s*\}', method_bodies["SaveSettings"]):
            container = aliases.get(pair.group(2))
            if container in containers:
                persisted.setdefault((container, pair.group(3)), set()).add(pair.group(1))

        controls = {}
        draw_body = method_bodies["DrawSettings"]
        masked_draw = mask_cpp_source(draw_body)
        for control in re.finditer(r"\b(?:ImGui|Util)::Checkbox\s*\(", masked_draw):
            close = find_matching_paren(draw_body, control.end() - 1)
            if close < 0:
                continue
            arguments = split_args(draw_body[control.end():close])
            if len(arguments) < 2:
                continue
            storage = re.fullmatch(
                r"\s*&\s*([A-Za-z_]\w*)\s*->\s*([A-Za-z_]\w*)\s*",
                arguments[1])
            if not storage or aliases.get(storage.group(1)) not in containers:
                continue
            translated = extract_i18n_call(arguments[0], prefix)
            literal = parse_cpp_string_expression(arguments[0])
            label_key, label = translated if translated else ("", literal or "")
            label = label.split("##", 1)[0]
            if not label:
                continue
            controls.setdefault(
                (aliases[storage.group(1)], storage.group(2)), set()).add(
                    (label, label_key))

        entries = []
        for identity, keys in persisted.items():
            labels = controls.get(identity, set())
            if len(keys) == 1 and len(labels) == 1:
                entries.append((*identity, next(iter(keys)), next(iter(labels))))
        if entries:
            result[feature_class] = entries
    return result


def extract_i18n_calls(text: str, prefix: str) -> list[tuple[str, str]]:
    patterns = (
        (r'T\(\s*TKEY\(\s*"([^"]+)"\s*\)\s*,\s*"([^"]*)"', True),
        (r'T\(\s*"([^"]+)"\s*,\s*"([^"]*)"', False),
    )
    matches = []
    for pattern, uses_prefix in patterns:
        for match in re.finditer(pattern, text):
            key = f"{prefix}{match.group(1)}" if uses_prefix else match.group(1)
            matches.append((match.start(), key, match.group(2)))
    return [(key, fallback) for _, key, fallback in sorted(matches)]


def extract_i18n_call(text: str, prefix: str) -> tuple[str, str] | None:
    matches = extract_i18n_calls(text, prefix)
    return matches[0] if matches else None


def resolve_control_translation(
        body: str,
        control_position: int,
        label_expression: str,
        prefix: str,
        setting_key: str) -> tuple[str, str] | None:
    translated = extract_i18n_call(label_expression, prefix)
    if translated:
        return translated

    label_variable = re.fullmatch(r"[A-Za-z_]\w*", label_expression.strip())
    if not label_variable:
        return None

    variable = label_variable.group(0)
    assignment_pattern = re.compile(
        rf"(?:\b(?:const\s+)?char\s*\*\s+)?\b{re.escape(variable)}\s*=")
    assignments = list(assignment_pattern.finditer(body, 0, control_position))
    if not assignments:
        return None
    assignment = assignments[-1]
    end = body.find(";", assignment.end())
    if end < 0 or end > control_position:
        return None
    candidates = extract_i18n_calls(body[assignment.end():end], prefix)
    if not candidates:
        return None

    fallback = prettify(setting_key)
    return min(
        candidates,
        key=lambda item: (item[1] != fallback, len(item[1]), item[1]))


_MASK_SCANNER = re.compile("[\"'/]")
_MASK_NON_NEWLINE = re.compile(r"[^\r\n]")


def _blank_keeping_newlines(segment: str) -> str:
    """Blanks a segment while preserving line structure, so line numbers still line up."""
    return _MASK_NON_NEWLINE.sub(" ", segment)


@functools.lru_cache(maxsize=None)
def mask_cpp_source(text: str) -> str:
    length = len(text)
    # Copy whole runs between literals instead of rewriting the source character by character.
    chunks: list[str] = []
    copied = 0
    i = 0
    search = _MASK_SCANNER.search
    while i < length:
        match = search(text, i)
        if not match:
            break
        i = match.start()
        if text[i] == "/":
            if text.startswith("//", i):
                end = text.find("\n", i)
                end = length if end < 0 else end
                # A line comment runs to the newline, so blanking it cannot lose one.
                chunks.append(text[copied:i])
                chunks.append(" " * (end - i))
            elif text.startswith("/*", i):
                end = text.find("*/", i + 2)
                end = length if end < 0 else end + 2
                chunks.append(text[copied:i])
                chunks.append(_blank_keeping_newlines(text[i:end]))
            else:
                i += 1
                continue
            copied = i = end
            continue

        # The quotes themselves survive; only the body is blanked.
        quote = text[i]
        chunks.append(text[copied:i + 1])
        i += 1
        body: list[str] = []
        while i < length:
            ch = text[i]
            if ch == "\\":
                # An escape pair is blanked whole, even when it spans a line continuation.
                body.append("  " if i + 1 < length else " ")
                i += 2
                continue
            if ch == quote:
                break
            body.append(ch if ch in "\r\n" else " ")
            i += 1
        chunks.append("".join(body))
        copied = min(i, length)
        i = copied + 1
    chunks.append(text[copied:])
    return "".join(chunks)


def extract_draw_settings_body(text: str) -> tuple[str, str] | None:
    match = re.search(r"\b(\w+)::DrawSettings\s*\([^)]*\)[^{;]*\{", text)
    if not match:
        return None
    end = find_matching_brace(text, match.end() - 1)
    if end < 0:
        return None
    return match.group(1), text[match.end():end]


def collect_tab_selector_roots(
        paths: list[Path]) -> dict[tuple[str, tuple[str, ...]], tuple[tuple[str, ...], tuple[str, ...]]]:
    candidates = {}
    method_pattern = re.compile(
        r"\b(?:bool|void)\s+([A-Za-z_]\w*)::Draw[A-Za-z_]\w*\s*\([^;{]*\)\s*(?:const\s*)?\{")
    tab_pattern = re.compile(r"\b(?:ImGui|Util)::BeginTabItem\s*\(")
    setting_pattern = re.compile(
        r"\b(?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
        r"([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)")

    for path in paths:
        text = read_text(path)
        masked = mask_cpp_source(text)
        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        for method in method_pattern.finditer(masked):
            body_end = find_matching_brace(text, method.end() - 1)
            if body_end < 0:
                continue
            body = text[method.end():body_end]
            masked_body = masked[method.end():body_end]
            tabs = []
            for tab in tab_pattern.finditer(masked_body):
                close = find_matching_paren(body, tab.end() - 1)
                if close < 0:
                    continue
                block_start = masked_body.find("{", close + 1)
                statement_end = masked_body.find(";", close + 1)
                if block_start < 0 or (statement_end >= 0 and statement_end < block_start):
                    continue
                block_end = find_matching_brace(body, block_start)
                translated = extract_i18n_call(body[tab.start():close + 1], prefix)
                if block_end >= 0 and translated:
                    tabs.append((block_start, block_end, translated[1], translated[0]))

            for setting in setting_pattern.finditer(masked_body):
                selectors = sorted(
                    (tab for tab in tabs if tab[0] < setting.start() < tab[1]),
                    key=lambda tab: (tab[0], -tab[1]))
                if not selectors:
                    continue
                identity = (method.group(1), tuple(setting.group(1).split(".")))
                candidates.setdefault(identity, set()).add((
                    tuple(tab[2] for tab in selectors),
                    tuple(tab[3] for tab in selectors)))
    return {
        identity: next(iter(values))
        for identity, values in candidates.items()
        if len(values) == 1
    }


@functools.lru_cache(maxsize=None)
def collect_numeric_constants(text: str) -> dict[str, float]:
    expressions: dict[str, str] = {}
    pattern = re.compile(
        r"\b(?:constexpr|const)\b[^;=\n]*?\b([A-Za-z_]\w*)\s*=\s*([^;]+);")
    for match in pattern.finditer(text):
        declarators = split_args(match.group(2))
        if not declarators:
            continue
        expressions[match.group(1)] = declarators[0]
        for declarator in declarators[1:]:
            additional = re.match(r"(?:[A-Za-z_]\w*\s+)*([A-Za-z_]\w*)\s*=\s*(.+)", declarator)
            if additional:
                expressions[additional.group(1)] = additional.group(2).strip()

    enum_pattern = re.compile(r"\benum(?:\s+class)?(?:\s+\w+)?(?:\s*:\s*[^{]+)?\s*\{([^}]*)\}")
    for enum_match in enum_pattern.finditer(mask_cpp_source(text)):
        previous_name = ""
        for enumerator in split_args(enum_match.group(1)):
            declaration = enumerator.strip()
            if not declaration:
                continue
            name, separator, expression = declaration.partition("=")
            name = name.strip()
            if not re.fullmatch(r"[A-Za-z_]\w*", name):
                continue
            if separator:
                expressions[name] = expression.strip()
            elif previous_name:
                expressions[name] = f"{previous_name} + 1"
            else:
                expressions[name] = "0"
            previous_name = name

    constants: dict[str, float] = {}
    for _ in range(len(expressions) + 1):
        changed = False
        for name, expression in expressions.items():
            if name in constants:
                continue
            value = resolve_numeric_expression(expression, constants)
            if value is not None:
                constants[name] = value
                changed = True
        if not changed:
            break
    return constants


@functools.lru_cache(maxsize=None)
def source_numeric_constants(source: Path) -> dict[str, float]:
    """Numeric constants a translation unit can see, including its companion header.

    Callers only read the result, so the cached dict is shared rather than copied.
    """
    header = source.with_suffix(".h")
    return collect_numeric_constants(
        read_text(source) + (read_text(header) if header.exists() else ""))


def resolve_numeric_expression(expression: str, constants: dict[str, float]) -> float | None:
    expression = expression.strip()
    expression = re.sub(r"^&", "", expression).strip()
    expression = re.sub(r"\b(static_cast|reinterpret_cast|const_cast)\s*<[^>]+>\s*\((.*)\)$", r"\2", expression)
    expression = re.sub(rf"\(\s*{INTEGRAL_TYPE_PATTERN}\s*\)", "", expression)
    expression = re.sub(
        r"(?<![A-Za-z_])((?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(?:[fFuUlL]+)\b",
        r"\1", expression)

    identifiers = set(re.findall(r"\b[A-Za-z_]\w*\b", expression))
    if any(identifier not in constants for identifier in identifiers):
        return None
    for identifier in sorted(identifiers, key=len, reverse=True):
        expression = re.sub(rf"\b{re.escape(identifier)}\b", repr(constants[identifier]), expression)

    try:
        tree = ast.parse(expression, mode="eval")
    except SyntaxError:
        return None

    def evaluate(node: ast.AST) -> float:
        if isinstance(node, ast.Expression):
            return evaluate(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return float(node.value)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = evaluate(node.operand)
            return value if isinstance(node.op, ast.UAdd) else -value
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult, ast.Div)):
            lhs = evaluate(node.left)
            rhs = evaluate(node.right)
            if isinstance(node.op, ast.Add):
                return lhs + rhs
            if isinstance(node.op, ast.Sub):
                return lhs - rhs
            if isinstance(node.op, ast.Mult):
                return lhs * rhs
            return lhs / rhs
        raise ValueError

    try:
        value = evaluate(tree)
    except (ValueError, ZeroDivisionError, OverflowError):
        return None
    return value if math.isfinite(value) else None


def parse_helper_parameters(
        parameter_text: str) -> tuple[tuple[str, str, str | None], ...]:
    parameters = []
    for parameter in split_args(parameter_text):
        declaration, separator, default = parameter.partition("=")
        stripped = declaration.strip()
        name_match = re.search(
            r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)+$", stripped)
        if not name_match:
            name_match = re.search(r"([A-Za-z_]\w*)\s*$", stripped)
        if name_match:
            parameters.append((
                clean_type(stripped[:name_match.start()]),
                name_match.group(1),
                default.strip() if separator else None))
    return tuple(parameters)






def substitute_helper_parameters(
        expression: str,
        arguments: dict[str, str]) -> str:
    for name in sorted(arguments, key=len, reverse=True):
        expression = re.sub(
            rf"\b{re.escape(name)}\b",
            lambda _: f"({arguments[name]})",
            expression)
    return expression




def control_source_widget(control_kind: str) -> str:
    if re.fullmatch(r"ShiftSliderFloat[234]", control_kind):
        return "ShiftSlider"
    return "" if control_kind.startswith("Projected") else control_kind


def control_flags_argument_index(control_kind: str) -> int | None:
    if re.fullmatch(r"ColorEdit[34]", control_kind):
        return 2
    if re.fullmatch(r"ShiftSliderFloat[234]", control_kind):
        return 5
    if control_kind == "SliderScalar":
        return 6
    if control_kind == "SliderScalarN":
        return 7
    if control_kind.startswith("Slider"):
        return 5
    if control_kind == "DragScalar":
        return 7
    if control_kind == "DragScalarN":
        return 8
    if control_kind.startswith("Drag"):
        return 6
    return None


def resolve_control_flags_expression(
        body: str, position: int, control_kind: str, args: list[str]) -> str:
    flags_index = control_flags_argument_index(control_kind)
    if flags_index is None or flags_index >= len(args):
        return ""

    expression = args[flags_index].strip()
    visited = set()
    prefix = body[:position]
    masked_prefix = mask_cpp_source(prefix)
    while re.fullmatch(r"[A-Za-z_]\w*", expression) and expression not in visited:
        visited.add(expression)
        assignment_pattern = re.compile(
            rf"\b(?:const(?:expr)?\s+)?(?:auto|ImGui(?:Slider|ColorEdit)Flags)\s+"
            rf"{re.escape(expression)}\s*=\s*([^;]+);")
        assignments = list(assignment_pattern.finditer(masked_prefix))
        if not assignments:
            break
        match = assignments[-1]
        resolved = prefix[match.start(1):match.end(1)].strip()
        additions = [
            prefix[addition.start(1):addition.end(1)].strip()
            for addition in re.finditer(
                rf"\b{re.escape(expression)}\s*\|=\s*([^;]+);",
                masked_prefix)
            if addition.start() > match.end()
        ]
        expression = " | ".join((resolved, *additions))
    return expression


def control_is_hdr_color(control_kind: str, flags_expression: str) -> bool:
    return (re.fullmatch(r"ColorEdit[34]", control_kind) is not None and
            re.search(r"\bImGuiColorEditFlags_HDR\b", flags_expression) is not None)


def control_clamps_numeric_input(
        control_kind: str, flags_expression: str) -> bool:
    if re.fullmatch(r"ColorEdit[34]", control_kind):
        return not control_is_hdr_color(control_kind, flags_expression)
    return re.search(
        r"\bImGuiSliderFlags_AlwaysClamp\b", flags_expression) is not None


def control_bounds_argument_indices(control_kind: str) -> tuple[int, int] | None:
    if control_kind == "SliderScalar":
        return 3, 4
    if control_kind == "SliderScalarN":
        return 4, 5
    if control_kind.startswith(("Slider", "ShiftSlider")):
        return 2, 3
    if control_kind == "DragScalar":
        return 4, 5
    if control_kind == "DragScalarN":
        return 5, 6
    if control_kind.startswith("Drag"):
        return 3, 4
    return None


def get_control_numeric_metadata(control_kind: str, args: list[str],
                                 constants: dict[str, float],
                                 flags_expression: str = "") -> tuple[float, float, float] | None:
    bounds: tuple[str, str] | None = None
    if re.fullmatch(r"ColorEdit[34]", control_kind):
        if control_is_hdr_color(control_kind, flags_expression):
            return None
        bounds = ("0.0", "1.0")
    elif (indices := control_bounds_argument_indices(control_kind)) and len(args) > indices[1]:
        bounds = args[indices[0]], args[indices[1]]
    elif control_kind == "PercentageSlider":
        bounds = (args[2] if len(args) >= 3 else "0.0", args[3] if len(args) >= 4 else "100.0")
    if not bounds:
        return None
    minimum = resolve_numeric_expression(bounds[0], constants)
    maximum = resolve_numeric_expression(bounds[1], constants)
    if minimum is None or maximum is None or minimum >= maximum:
        return None
    display_scale = 1.0
    if control_kind == "PercentageSlider":
        minimum /= 100.0
        maximum /= 100.0
        display_scale = 100.0
    elif control_kind == "SliderAngle":
        minimum = math.radians(minimum)
        maximum = math.radians(maximum)
        display_scale = 180.0 / math.pi
    return minimum, maximum, display_scale


def normalize_setting_path(value: str) -> tuple[str, ...]:
    value = re.sub(r"\[\s*(\d+)\s*\]", r".\1", value)
    value = re.sub(r"\[[^\]]+\]", ".*", value)
    parts = [part.removesuffix("()") for part in value.split(".") if part]
    if parts and parts[-1] == "data":
        parts.pop()
    return tuple(parts)


def collect_local_setting_aliases(body: str) -> dict[str, tuple[str, ...]]:
    aliases: dict[str, tuple[str, ...]] = {}
    pattern = re.compile(
        r"\b(?:bool|int|unsigned|uint(?:32_t)?|float|double|auto)\s*(&?)\s*"
        r"([A-Za-z_]\w*)\s*=\s*"
        r"([^;]+)")
    for match in pattern.finditer(mask_cpp_source(body)):
        expression = strip_integral_casts(
            unwrap_combo_proxy(re.sub(r"\s+", " ", match.group(3)), True))
        setting_pattern = (
            r"((?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
            r"([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*))")
        origin = re.fullmatch(setting_pattern, expression)
        normalized_boolean = False
        if not origin:
            origin = re.fullmatch(
                setting_pattern + r"\s*!=\s*(?:0|false)", expression)
            normalized_boolean = origin is not None
        if not origin:
            continue
        setting_expression = origin.group(1)
        setting_path = normalize_setting_path(origin.group(2))
        if match.group(1) and not normalized_boolean:
            aliases[match.group(2)] = setting_path
            continue

        writes = re.findall(
            rf"\b{re.escape(setting_expression)}\s*=\s*(?!=)([^;]+);",
            mask_cpp_source(body[match.end():]))
        local_name = match.group(2)

        def is_proxy_write(write: str) -> bool:
            value = strip_integral_casts(unwrap_combo_proxy(
                re.sub(r"\s+", " ", write), True))
            if value == local_name:
                return True
            if normalized_boolean and re.fullmatch(
                    rf"{re.escape(local_name)}\s*\?\s*"
                    r"(?:true|1(?:[uUlL]+)?)\s*:\s*(?:false|0(?:[uUlL]+)?)",
                    value):
                return True
            clamp = re.fullmatch(
                r"(?:(?:std|[A-Za-z_]\w*)::)?clamp\s*\((.*)\)", value)
            return bool(clamp and len(arguments := split_args(clamp.group(1))) == 3 and
                        strip_integral_casts(arguments[0]) == local_name)

        if writes and all(is_proxy_write(write) for write in writes):
            aliases[match.group(2)] = setting_path
    return aliases


def control_storage_argument_index(control_kind: str) -> int:
    return 2 if re.fullmatch(r"(?:Drag|Slider|Input)ScalarN?", control_kind) else 1


def extract_control_setting_path(
        control_kind: str,
        args: list[str],
        aliases: dict[str, tuple[str, ...]]) -> tuple[str, ...] | None:
    storage_index = control_storage_argument_index(control_kind)
    if len(args) <= storage_index:
        return None
    storage_argument = args[storage_index]
    setting_match = re.search(
        r"&?(?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
        r"([A-Za-z_]\w*(?:(?:\[[^\]]+\])|(?:\.[A-Za-z_]\w*(?:\(\))?))*)",
        storage_argument)
    if setting_match:
        return normalize_setting_path(setting_match.group(1))

    for alias, setting_path in aliases.items():
        if re.search(rf"(?:&|\b){re.escape(alias)}\b", storage_argument):
            return setting_path

    return None


def control_component_count(control_kind: str) -> int:
    color = re.fullmatch(r"ColorEdit([34])", control_kind)
    if color:
        return int(color.group(1))
    numeric = re.fullmatch(r"(?:Slider|Drag|Input)(?:Float|Int)([234])", control_kind)
    if numeric:
        return int(numeric.group(1))
    unified = re.fullmatch(r"ShiftSliderFloat([234])", control_kind)
    if unified:
        return int(unified.group(1))
    projected = re.fullmatch(
        r"Projected(?:Numeric|ColorEditor|Color)([234])", control_kind)
    return int(projected.group(1)) if projected else 1


def control_group_semantic(control_kind: str) -> str:
    if control_kind.startswith(("ColorEdit", "ProjectedColor")):
        return "Color"
    return "Numeric" if control_component_count(control_kind) > 1 else "None"


def resolve_editor_semantic(
        binding: ControlBinding | None,
        value_type: str,
        force_hidden: bool = False) -> str:
    if force_hidden or binding is None:
        return "None"
    if binding.choices and value_type == "Integer":
        return "Choice"
    if ((binding.control_kind == "Checkbox" or
         binding.control_kind.endswith("Checkbox")) and
            value_type in {"Boolean", "Integer"}):
        return "Toggle"
    if (binding.control_kind.startswith((
            "Slider", "ShiftSlider", "Drag", "Input", "ColorEdit", "Projected")) or
            binding.control_kind == "PercentageSlider"):
        if value_type in {"Float", "Integer"}:
            return "Numeric"
    return "Generic"






def collect_projected_numeric_helpers(paths: list[Path]) -> dict[str, ProjectedNumericHelper]:
    definitions = {}
    for function in collect_source_functions(paths):
        definitions.setdefault(function.name, []).append(function)

    collected = {}
    for name, candidates in definitions.items():
        if len(candidates) != 1:
            continue
        function = candidates[0]
        controls = list(DIRECT_UI_CONTROL_RE.finditer(function.masked_body))
        if len(controls) != 1:
            continue
        control = controls[0]
        close = find_matching_paren(function.body, control.end() - 1)
        args = split_args(function.body[control.end():close]) if close >= 0 else []
        storage_index = control_storage_argument_index(control.group(1))
        if len(args) <= storage_index:
            continue
        storage = re.fullmatch(
            r"\s*&\s*([A-Za-z_]\w*)\s*\[\s*([A-Za-z_]\w*)\s*\]\s*",
            args[storage_index])
        if not storage:
            continue
        parameter_indices = {
            parameter.name: index for index, parameter in enumerate(function.parameters)
        }
        storage_parameter = parameter_indices.get(storage.group(1))
        if storage_parameter is None:
            continue

        constants = source_numeric_constants(function.source)
        loop_counts = set()
        loop_pattern = re.compile(
            rf"\bfor\s*\(\s*(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*\s+)+"
            rf"{re.escape(storage.group(2))}\s*=\s*0\s*;\s*"
            rf"{re.escape(storage.group(2))}\s*<\s*([^;]+)\s*;\s*"
            rf"(?:\+\+{re.escape(storage.group(2))}|{re.escape(storage.group(2))}\+\+)\s*\)")
        for loop in loop_pattern.finditer(function.masked_body):
            block_start = function.masked_body.find("{", loop.end())
            block_end = find_matching_brace(function.body, block_start) if block_start >= 0 else -1
            count = resolve_numeric_expression(loop.group(1), constants)
            if (block_start < control.start() < block_end and count is not None and
                    count.is_integer() and 2 <= count <= MAX_VECTOR_COMPONENT_COUNT):
                loop_counts.add(int(count))
        if len(loop_counts) != 1:
            continue
        component_count = next(iter(loop_counts))

        label_parameters = [
            index for index, parameter in enumerate(function.parameters)
            if re.search(rf"\bImGui::PushID\s*\(\s*{re.escape(parameter.name)}\s*\)",
                         function.masked_body) and
            re.search(rf"\bImGui::Text(?:Unformatted)?\s*\(\s*{re.escape(parameter.name)}\b",
                      function.masked_body)
        ]
        component_parameters = [
            index for index, parameter in enumerate(function.parameters)
            if (array := fixed_array_type(parameter.type_name, constants)) and
            array[1] == component_count and
            re.search(rf"\b{re.escape(parameter.name)}\s*\[\s*"
                      rf"{re.escape(storage.group(2))}\s*\]", function.masked_body)
        ]
        if len(label_parameters) != 1 or len(component_parameters) != 1:
            continue
        collected[name] = ProjectedNumericHelper(
            tuple((parameter.name, parameter.default) for parameter in function.parameters),
            storage_parameter, label_parameters[0], component_parameters[0],
            component_count, control.group(1), tuple(args))
    return collected


def resolve_local_component_labels(
        body: str,
        position: int,
        expression: str,
        prefix: str,
        expected_count: int) -> tuple[tuple[str, str], ...] | None:
    variable = expression.strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", variable):
        return None
    declarations = list(re.finditer(
        rf"\b(?:(?:const|constexpr)\s+)?std::array"
        rf"(?:\s*<[^;{{}}\n]+>)?\s+{re.escape(variable)}\s*=\s*\{{",
        mask_cpp_source(body[:position])))
    if len(declarations) != 1:
        return None
    open_brace = declarations[0].end() - 1
    close_brace = find_matching_brace(body, open_brace)
    if close_brace < 0 or close_brace >= position:
        return None
    labels = parse_string_array_items(body[open_brace + 1:close_brace], prefix)
    return labels if labels and len(labels) == expected_count else None


def resolve_storage_vector_components(
        expression: str, source_text: str) -> tuple[str, ...] | None:
    storage = re.search(
        r"(?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
        r"(?:[A-Za-z_]\w*\.)*([A-Za-z_]\w*)\.([A-Za-z_]\w*)",
        expression)
    if not storage:
        return None
    candidates = {
        VECTOR_COMPONENTS[clean_type(field.group(1))]
        for field in re.finditer(
            rf"\b([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s+"
            rf"{re.escape(storage.group(1))}\b", mask_cpp_source(source_text))
        if clean_type(field.group(1)) in VECTOR_COMPONENTS and
        storage.group(2) in VECTOR_COMPONENTS[clean_type(field.group(1))]
    }
    return next(iter(candidates)) if len(candidates) == 1 else None




def _strip_reference_expression(expression: str) -> str:
    expression = expression.strip()
    while expression.startswith("(") and expression.endswith(")") and find_matching_paren(expression, 0) == len(expression) - 1:
        expression = expression[1:-1].strip()
    return expression.removeprefix("&").strip()


def _source_function_calls(body: str, definitions: dict[str, list[tuple]]):
    pattern = re.compile(r"\b((?:[A-Za-z_]\w*::)*)([A-Za-z_]\w*)\s*\(")
    masked = mask_cpp_source(body)
    for match in pattern.finditer(masked):
        if match.group(1) in {"ImGui::", "Util::"} or match.group(2) not in definitions:
            continue
        close = find_matching_paren(body, match.end() - 1)
        if close >= 0:
            yield match.start(), match.group(2), split_args(body[match.end():close])


def _resolve_source_function(definitions: dict[str, list[tuple]], name: str, argument_count: int):
    candidates = [
        definition for definition in definitions.get(name, [])
        if sum(parameter[2] is None for parameter in definition[1]) <=
        argument_count <= len(definition[1])
    ]
    if len(candidates) == 1:
        return candidates[0]
    if candidates:
        return None
    longer = [definition for definition in definitions.get(name, [])
              if len(definition[1]) > argument_count]
    if not longer:
        return None
    nearest = min(len(definition[1]) for definition in longer)
    candidates = [
        definition for definition in longer
        if len(definition[1]) == nearest
    ]
    return candidates[0] if len(candidates) == 1 else None


def _control_bounds_expressions(control_kind: str, args: list[str]):
    indices = control_bounds_argument_indices(control_kind)
    if indices and len(args) > indices[1]:
        return args[indices[0]], args[indices[1]]
    if control_kind == "PercentageSlider":
        return (args[2] if len(args) >= 3 else "0.0",
                args[3] if len(args) >= 4 else "100.0")
    return None


def _parameter_storage_flow(
        expression: str,
        parameters: tuple[tuple[str, str, str | None], ...],
        body: str,
        position: int,
        component_count: int,
        constants: dict[str, float]) -> tuple[int, int, int] | None:
    expression = _strip_reference_expression(expression)
    for index, (_, name, _) in enumerate(parameters):
        direct = re.fullmatch(rf"{re.escape(name)}(?:\s*\.\s*([A-Za-z_]\w*))?", expression)
        if direct:
            if direct.group(1) and direct.group(1) not in VECTOR_COMPONENT_NAMES:
                continue
            offset = VECTOR_COMPONENT_NAMES.index(direct.group(1)) if direct.group(1) else 0
            return index, offset, component_count

        indexed = re.fullmatch(
            rf"{re.escape(name)}\s*\[\s*([A-Za-z_]\w*|\d+)\s*\]", expression)
        if not indexed:
            continue
        if indexed.group(1).isdigit():
            return index, int(indexed.group(1)), component_count
        if component_count != 1:
            continue

        counts = set()
        loop_pattern = re.compile(
            rf"\bfor\s*\(\s*(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*\s+)+"
            rf"{re.escape(indexed.group(1))}\s*=\s*0\s*;\s*"
            rf"{re.escape(indexed.group(1))}\s*<\s*([^;]+)\s*;\s*"
            rf"(?:\+\+{re.escape(indexed.group(1))}|{re.escape(indexed.group(1))}\+\+)\s*\)")
        masked = mask_cpp_source(body)
        for loop in loop_pattern.finditer(masked):
            block_start = masked.find("{", loop.end())
            if block_start < 0:
                continue
            block_end = find_matching_brace(body, block_start)
            count = resolve_numeric_expression(loop.group(1), constants)
            if (block_start < position < block_end and count is not None and
                    count.is_integer() and 2 <= count <= 4):
                counts.add(int(count))
        if len(counts) == 1:
            return index, 0, counts.pop()
    return None


def _argument_storage_origin(expression: str, parameters: tuple) -> tuple[int, int] | None:
    expression = _strip_reference_expression(expression)
    for index, (_, name, _) in enumerate(parameters):
        match = re.fullmatch(
            rf"{re.escape(name)}(?:\s*\.\s*([A-Za-z_]\w*)|\s*\[\s*(\d+)\s*\])?",
            expression)
        if match:
            if match.group(1) and match.group(1) not in VECTOR_COMPONENT_NAMES:
                continue
            offset = (VECTOR_COMPONENT_NAMES.index(match.group(1)) if match.group(1)
                      else int(match.group(2) or 0))
            return index, offset
    return None


def _summarize_numeric_control_flow(
        name: str,
        definition: tuple,
        definitions: dict[str, list[tuple]],
        cache: dict[tuple, tuple[NumericControlFlow, ...]],
        active: set[tuple] | None = None):
    source_path, parameters, body, constants = definition
    identity = (source_path, name, body)
    if identity in cache:
        return cache[identity]
    active = set() if active is None else active
    if identity in active:
        return ()

    controls = set()
    masked = mask_cpp_source(body)
    for control in DIRECT_UI_CONTROL_RE.finditer(masked):
        control_kind = control.group(1)
        close = find_matching_paren(body, control.end() - 1)
        if close < 0:
            continue
        args = split_args(body[control.end():close])
        flags_expression = resolve_control_flags_expression(
            body, control.start(), control_kind, args)
        bounds = _control_bounds_expressions(control_kind, args)
        if (not bounds and re.fullmatch(r"ColorEdit[34]", control_kind) and
                not control_is_hdr_color(control_kind, flags_expression)):
            bounds = ("0.0", "1.0")
        storage_index = control_storage_argument_index(control_kind)
        if not bounds or len(args) <= storage_index:
            continue
        storage = _parameter_storage_flow(
            args[storage_index], parameters, body, control.start(),
            control_component_count(control_kind), constants)
        if not storage:
            continue
        indexed = re.search(r"\[\s*([A-Za-z_]\w*)\s*\]", args[storage_index])
        color_marked = indexed and re.search(
            rf"\bSetNextItemColorMarker\s*\([^;]*\[\s*{re.escape(indexed.group(1))}\s*\]",
            masked)
        is_color_editor = control_kind.startswith("ColorEdit")
        semantic = "Color" if is_color_editor or color_marked else "Numeric"
        controls.add(NumericControlFlow(
            storage[0], storage[1], storage[2], args[0].strip(),
            bounds[0].strip(), bounds[1].strip(), semantic,
            "ColorPicker" if is_color_editor else "Components",
            control_source_widget(control_kind),
            control_clamps_numeric_input(control_kind, flags_expression),
            control_is_hdr_color(control_kind, flags_expression)))

    for control in SHIFT_UNIFIED_CONTROL_RE.finditer(masked):
        close = find_matching_paren(body, control.end() - 1)
        if close < 0:
            continue
        args = split_args(body[control.end():close])
        if len(args) < 4:
            continue
        component_count = int(control.group(1))
        control_kind = f"ShiftSliderFloat{component_count}"
        flags_expression = resolve_control_flags_expression(
            body, control.start(), control_kind, args)
        storage = _parameter_storage_flow(
            args[1], parameters, body, control.start(),
            component_count, constants)
        if not storage:
            continue
        controls.add(NumericControlFlow(
            storage[0], storage[1], storage[2], args[0].strip(),
            args[2].strip(), args[3].strip(), "Numeric", "Components",
            control_source_widget(control_kind),
            control_clamps_numeric_input(control_kind, flags_expression), False))

    for _, called_name, args in _source_function_calls(body, definitions):
        called = _resolve_source_function(definitions, called_name, len(args))
        if not called:
            continue
        called_parameters = called[1]
        arguments = {
            parameter[1]: args[index] if index < len(args) else parameter[2]
            for index, parameter in enumerate(called_parameters)
            if index < len(args) or parameter[2] is not None
        }
        for flow in _summarize_numeric_control_flow(
                called_name, called, definitions, cache, active | {identity}):
            if flow.storage_parameter >= len(args):
                continue
            origin = _argument_storage_origin(args[flow.storage_parameter], parameters)
            if (not origin or origin[1] + flow.storage_offset +
                    flow.component_count > MAX_VECTOR_COMPONENT_COUNT):
                continue
            controls.add(NumericControlFlow(
                origin[0], origin[1] + flow.storage_offset, flow.component_count,
                substitute_helper_parameters(flow.item_label, arguments),
                substitute_helper_parameters(flow.minimum_expression, arguments),
                substitute_helper_parameters(flow.maximum_expression, arguments),
                flow.semantic, flow.presentation,
                flow.source_widget, flow.clamp_numeric_input, flow.hdr_color))

    cache[identity] = tuple(sorted(controls, key=lambda value: (
        value.storage_parameter, value.storage_offset, value.component_count,
        value.item_label, value.minimum_expression, value.maximum_expression,
        value.semantic, value.source_widget, value.clamp_numeric_input,
        value.hdr_color)))
    return cache[identity]


def _collect_indirect_record_tables(text: str):
    schema_candidates = {}
    masked = mask_cpp_source(text)
    for match in re.finditer(STRUCT_DECL_RE, masked):
        body_end = find_matching_brace(text, match.end() - 1)
        if body_end < 0:
            continue
        fields = []
        pointer_fields = []
        body = text[match.end():body_end]
        for statement in mask_cpp_source(body).split(";"):
            parsed = parse_field_statement(statement)
            if not parsed:
                continue
            fields.append(parsed[0])
            pointer = re.search(
                r"\b([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s+"
                r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*::\s*\*\s*([A-Za-z_]\w*)\s*$",
                statement.strip())
            if pointer and clean_type(pointer.group(1)) in VECTOR_COMPONENTS:
                pointer_fields.append((
                    pointer.group(2), VECTOR_COMPONENTS[clean_type(pointer.group(1))]))
        if len(pointer_fields) == 1:
            schema_candidates.setdefault(match.group(1), set()).add(
                (tuple(fields), *pointer_fields[0]))
    schemas = {
        name: next(iter(candidates))
        for name, candidates in schema_candidates.items()
        if len(candidates) == 1
    }

    provider_candidates = {}
    pattern = re.compile(
        r"\bstd::array\s*<\s*([A-Za-z_]\w*)\s*,[^>]+>\s+"
        r"([A-Za-z_]\w*)\s*\([^)]*\)\s*\{")
    for match in pattern.finditer(masked):
        record_type = match.group(1)
        schema = schemas.get(record_type)
        if not schema:
            continue
        body_end = find_matching_brace(text, match.end() - 1)
        if body_end < 0:
            continue
        body = text[match.end():body_end]
        rows = []
        for row in re.finditer(rf"\b{re.escape(record_type)}\s*\{{", mask_cpp_source(body)):
            row_end = find_matching_brace(body, row.end() - 1)
            if row_end < 0:
                continue
            values = split_args(body[row.end():row_end])
            if len(values) == len(schema[0]):
                rows.append(tuple(zip(schema[0], values)))
        if rows:
            provider_candidates.setdefault(match.group(2), set()).add(
                (record_type, tuple(rows)))
    providers = {
        name: next(iter(candidates))
        for name, candidates in provider_candidates.items()
        if len(candidates) == 1
    }
    return schemas, providers


def _member_reference(expression: str, parameter: str) -> str | None:
    expression = _strip_reference_expression(expression)
    expression = re.sub(
        rf"^\(\s*{re.escape(parameter)}\s*\)\s*\.", f"{parameter}.", expression)
    match = re.fullmatch(
        rf"{re.escape(parameter)}\s*\.\s*([A-Za-z_]\w*)", expression)
    return match.group(1) if match else None


def _value_component_offset(
        expression: str, alias: str, components: tuple[str, ...]) -> int | None:
    expression = _strip_reference_expression(expression)
    match = re.fullmatch(
        rf"{re.escape(alias)}\s*\.\s*([A-Za-z_]\w*)", expression)
    return components.index(match.group(1)) if match and match.group(1) in components else None


def _infer_indirect_numeric_bindings(
        source_path: Path,
        schemas: dict[str, tuple],
        definitions: dict[str, list[tuple]],
        flow_cache: dict[tuple, tuple[NumericControlFlow, ...]]):
    binding_candidates = {}
    for name, candidates in definitions.items():
        for definition in candidates:
            if definition[0] != source_path:
                continue
            parameters, body = definition[1], definition[2]
            aliases = list(re.finditer(
                r"\bauto\s*&\s*([A-Za-z_]\w*)\s*=\s*[A-Za-z_]\w*\s*\.\s*\*\s*"
                r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)\s*;",
                mask_cpp_source(body)))
            if len(aliases) != 1:
                continue
            value_alias, record_name, pointer_field = aliases[0].groups()
            parameter_indices = {
                parameter[1]: index for index, parameter in enumerate(parameters)
            }
            record_parameter = parameter_indices.get(record_name)
            if record_parameter is None:
                continue
            record_type = clean_type(parameters[record_parameter][0]).split("::")[-1]
            schema = schemas.get(record_type)
            if not schema or schema[1] != pointer_field:
                continue
            vector_components = schema[2]

            for position, helper_name, args in _source_function_calls(body, definitions):
                helper = _resolve_source_function(definitions, helper_name, len(args))
                if not helper:
                    continue
                helper_parameters, helper_body = helper[1], helper[2]
                row_parameters = [
                    index for index, argument in enumerate(args)
                    if index < len(helper_parameters) and
                    _strip_reference_expression(argument) == record_name and
                    clean_type(helper_parameters[index][0]).split("::")[-1] == record_type
                ]
                if len(row_parameters) != 1:
                    continue
                helper_row = helper_parameters[row_parameters[0]][1]
                helper_masked = mask_cpp_source(helper_body)
                push_fields = set(re.findall(
                    rf"\bImGui::PushID\s*\(\s*{re.escape(helper_row)}\.([A-Za-z_]\w*)",
                    helper_masked))
                label_fields = set(re.findall(
                    rf"\bImGui::Text(?:Unformatted)?\s*\(\s*{re.escape(helper_row)}\.([A-Za-z_]\w*)",
                    helper_masked))
                if len(push_fields) != 1 or len(label_fields) != 1:
                    continue

                projected = []
                for flow in _summarize_numeric_control_flow(
                        helper_name, helper, definitions, flow_cache):
                    if flow.storage_parameter >= len(args):
                        continue
                    minimum = _member_reference(flow.minimum_expression, helper_row)
                    maximum = _member_reference(flow.maximum_expression, helper_row)
                    if not minimum or not maximum:
                        continue
                    base = _value_component_offset(
                        args[flow.storage_parameter], value_alias, vector_components)
                    projected.append((
                        flow, base + flow.storage_offset if base is not None else None,
                        _strip_reference_expression(args[flow.storage_parameter]),
                        minimum, maximum))

                layouts = set()
                for sliced in (value for value in projected if value[0].component_count > 1):
                    flow, slice_start, _, minimum, maximum = sliced
                    if (slice_start is None or
                            slice_start + flow.component_count > len(vector_components)):
                        continue
                    scalars = [
                        value for value in projected
                        if value[0].component_count == 1 and value[3:] == (minimum, maximum)
                    ]
                    for scalar in scalars:
                        item_label = parse_cpp_string_expression(
                            _strip_reference_expression(scalar[0].item_label))
                        if scalar[1] is None:
                            derived = re.search(
                                rf"\b(?:auto|float|double|int)\s+{re.escape(scalar[2])}\s*=\s*"
                                rf"([^;]*\b{re.escape(value_alias)}\b[^;]*)\s*;",
                                mask_cpp_source(body[:position]))
                            if derived and item_label and item_label.startswith("##"):
                                layouts.add((slice_start, flow.component_count, -1,
                                             flow.semantic, flow.presentation,
                                             minimum, maximum, flow.source_widget,
                                             flow.clamp_numeric_input, flow.hdr_color))
                        elif scalar[1] + 1 == slice_start:
                            layouts.add((scalar[1], flow.component_count + 1, scalar[1],
                                         flow.semantic, flow.presentation,
                                         minimum, maximum, flow.source_widget,
                                         flow.clamp_numeric_input, flow.hdr_color))

                if len(layouts) > 1:
                    raise ValueError(f"ambiguous indirect numeric layout {name}")
                if not layouts:
                    continue
                (aggregate_start, aggregate_count, aggregate_all,
                 semantic, presentation,
                 minimum, maximum, source_widget, clamp_numeric_input,
                 hdr_color) = next(iter(layouts))
                binding_candidates.setdefault(name, set()).add((
                    record_type, record_parameter, pointer_field, aggregate_start,
                    aggregate_count, semantic, presentation, aggregate_all,
                    next(iter(label_fields)), minimum,
                    maximum, vector_components, source_widget,
                    clamp_numeric_input, hdr_color))

    bindings = {}
    for name, candidates in binding_candidates.items():
        if len(candidates) > 1:
            raise ValueError(f"ambiguous indirect numeric binding {name}")
        bindings[name] = next(iter(candidates))
    return bindings


def _collect_indirect_table_wrappers(
        source_path: Path,
        definitions: dict[str, list[tuple]],
        bindings: dict[str, tuple]):
    candidates = {}
    range_pattern = re.compile(
        r"\bfor\s*\(\s*(?:const\s+)?(?:auto|[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)"
        r"\s*&?\s*([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)\s*\)")
    for name, overloads in definitions.items():
        for definition in overloads:
            if definition[0] != source_path:
                continue
            parameters, body = definition[1], definition[2]
            parameter_indices = {
                parameter[1]: index for index, parameter in enumerate(parameters)
            }
            rows = {
                loop.group(1): parameter_indices[loop.group(2)]
                for loop in range_pattern.finditer(mask_cpp_source(body))
                if loop.group(2) in parameter_indices
            }
            for _, called_name, args in _source_function_calls(body, definitions):
                binding = bindings.get(called_name)
                if not binding or binding[1] >= len(args):
                    continue
                row = _strip_reference_expression(args[binding[1]])
                if row in rows:
                    candidates.setdefault(name, set()).add((
                        definition[:3], rows[row], binding))
    wrappers = {}
    for name, values in candidates.items():
        if len(values) > 1:
            raise ValueError(f"ambiguous indirect table wrapper {name}")
        wrappers[name] = next(iter(values))
    return wrappers


def _collect_scoped_categories(body: str, prefix: str) -> list[tuple[int, int, str, str]]:
    categories = []
    pattern = re.compile(
        r"\b(?:ImGui|Util)::(?:" + "|".join(sorted(CATEGORY_CONTROL_NAMES)) + r")\s*\(")
    masked = mask_cpp_source(body)
    for category in pattern.finditer(masked):
        close = find_matching_paren(body, category.end() - 1)
        if close < 0:
            continue
        block_start = masked.find("{", close + 1)
        statement_end = masked.find(";", close + 1)
        if block_start < 0 or (statement_end >= 0 and statement_end < block_start):
            continue
        block_end = find_matching_brace(body, block_start)
        translated = extract_i18n_call(body[category.start():close + 1], prefix)
        if block_end >= 0 and translated:
            categories.append((block_start, block_end, translated[1], translated[0]))
    return categories


def collect_indirect_numeric_projections(paths: list[Path]):
    collected_labels = {}
    collected_components = {}
    collected_aggregate_all = {}

    texts = {path: read_text(path) for path in paths}
    source_functions = collect_source_functions(paths)
    definitions = {}
    for path, text in texts.items():
        constants = source_numeric_constants(path)
        for function in (value for value in source_functions if value.source == path):
            parameters = tuple(
                (value.type_name, value.name, value.default)
                for value in function.parameters)
            definitions.setdefault(function.name, []).append(
                (path, parameters, function.body, constants))
    flow_cache = {}

    for path in paths:
        text = texts[path]
        draw_settings = extract_draw_settings_body(text)
        if not draw_settings:
            continue
        owner, body = draw_settings
        schemas, providers = _collect_indirect_record_tables(text)
        bindings = _infer_indirect_numeric_bindings(
            path, schemas, definitions, flow_cache)
        wrappers = _collect_indirect_table_wrappers(
            path, definitions, bindings)
        if not providers or not wrappers:
            continue

        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        constants = source_numeric_constants(path)
        categories = _collect_scoped_categories(body, prefix)

        for invocation_position, wrapper_name, args in _source_function_calls(body, definitions):
            wrapper_summary = wrappers.get(wrapper_name)
            wrapper = _resolve_source_function(definitions, wrapper_name, len(args))
            if (not wrapper_summary or not wrapper or wrapper[:3] != wrapper_summary[0] or
                    wrapper_summary[1] >= len(args)):
                continue
            provider_call = re.fullmatch(
                r"\s*([A-Za-z_]\w*)\s*\(\s*\)\s*",
                args[wrapper_summary[1]])
            if not provider_call or provider_call.group(1) not in providers:
                continue
            provider_name = provider_call.group(1)
            binding = wrapper_summary[2]
            (record_type, _, pointer_field, aggregate_start, aggregate_count,
             aggregate_semantic, aggregate_presentation, aggregate_all,
             display_label_field, minimum_field, maximum_field,
             components, source_widget, clamp_numeric_input, hdr_color) = binding
            provider_type, rows = providers[provider_name]
            if provider_type != record_type:
                continue
            enclosing = [
                category for category in categories
                if category[0] < invocation_position < category[1]
            ]
            enclosing.sort(key=lambda category: (category[0], -category[1]))
            category_label = enclosing[-1][2] if enclosing else ""
            category_key = enclosing[-1][3] if enclosing else ""

            for row in rows:
                values = dict(row)
                target = re.fullmatch(
                    r"\s*&\s*(?:[A-Za-z_]\w*::)+([A-Za-z_]\w*)\s*",
                    values.get(pointer_field, ""))
                label_expression = values.get(display_label_field, "")
                translated = extract_i18n_call(label_expression, prefix)
                label = translated[1] if translated else parse_cpp_string_expression(label_expression)
                label_key = translated[0] if translated else ""
                minimum = resolve_numeric_expression(
                    values.get(minimum_field, ""), constants)
                maximum = resolve_numeric_expression(
                    values.get(maximum_field, ""), constants)
                if not target or not label or minimum is None or maximum is None or minimum >= maximum:
                    raise ValueError(f"unresolved indirect control row in {provider_name}")

                field = target.group(1)
                control_kind = (
                    f"ProjectedColorEditor{aggregate_count}"
                    if aggregate_presentation == "ColorPicker" else
                    f"Projected{aggregate_semantic}{aggregate_count}")
                label_metadata = (
                    label.split("##", 1)[0], category_label, label_key, category_key,
                    control_kind, minimum, maximum, 1.0, source_widget,
                    clamp_numeric_input, hdr_color)
                label_identity = (owner, (field, components[aggregate_start]))
                collected_labels.setdefault(label_identity, set()).add(label_metadata)
                if aggregate_all >= 0:
                    component_identity = (owner, (field, components[aggregate_all]))
                    collected_aggregate_all.setdefault(component_identity, set()).add(True)

    def finalize(mapping, description):
        result = {}
        for identity, candidates in mapping.items():
            if len(candidates) != 1:
                raise ValueError(f"ambiguous {description} for {identity[0]}.{'.'.join(identity[1])}")
            result[identity] = next(iter(candidates))
        return result

    return (
        finalize(collected_labels, "indirect numeric projection"),
        finalize(collected_components, "indirect numeric component"),
        finalize(collected_aggregate_all, "indirect aggregate component"),
    )


def parse_cpp_string_expression(expression: str) -> str | None:
    token_pattern = re.compile(r'(?:u8|[LuU])?"(?:\\.|[^"\\])*"')
    position = 0
    values = []
    for match in token_pattern.finditer(expression):
        if expression[position:match.start()].strip():
            return None
        token = re.sub(r"^(?:u8|[LuU])", "", match.group(0))
        try:
            values.append(ast.literal_eval(token))
        except (SyntaxError, ValueError):
            return None
        position = match.end()
    if not values or expression[position:].strip():
        return None
    return "".join(values)


def parse_string_array_items(initializer: str, prefix: str) -> tuple[tuple[str, str], ...] | None:
    values = []
    for item in (item for item in split_args(initializer) if item.strip()):
        translated = extract_i18n_call(item, prefix)
        if translated:
            values.append((translated[1], translated[0]))
            continue
        literal = parse_cpp_string_expression(item)
        if literal is None:
            return None
        values.append((literal, ""))
    return tuple(values) if len(values) >= 2 else None


def collect_literal_std_arrays(
        text: str,
        prefix: str,
        constants: dict[str, float]) -> tuple[
            dict[str, tuple[tuple[str, str], ...]], dict[str, tuple[int, ...]]]:
    string_candidates: dict[str, list[tuple[tuple[str, str], ...]]] = {}
    integer_candidates: dict[str, list[tuple[int, ...]]] = {}
    pattern = re.compile(
        r"\b(?:(?:static|inline|constexpr|const)\s+)*std::array\s*<[^;{}\n]+>\s+"
        r"([A-Za-z_]\w*)\s*=\s*\{")
    masked = mask_cpp_source(text)
    for declaration in pattern.finditer(masked):
        close = find_matching_brace(text, declaration.end() - 1)
        if close < 0:
            continue
        initializer = text[declaration.end():close]
        strings = parse_string_array_items(initializer, prefix)
        if strings:
            string_candidates.setdefault(declaration.group(1), []).append(strings)
            continue
        integers = []
        for item in (item for item in split_args(initializer) if item.strip()):
            value = resolve_numeric_expression(item, constants)
            if value is None or not value.is_integer():
                integers = []
                break
            integers.append(int(value))
        if len(integers) >= 2:
            integer_candidates.setdefault(declaration.group(1), []).append(tuple(integers))
    return (
        {name: values[0] for name, values in string_candidates.items() if len(values) == 1},
        {name: values[0] for name, values in integer_candidates.items() if len(values) == 1})


def strip_integral_casts(expression: str) -> str:
    value = expression.strip()
    while value:
        if value.startswith("(") and find_matching_paren(value, 0) == len(value) - 1:
            value = value[1:-1].strip()
            continue
        cast = re.fullmatch(r"static_cast\s*<[^>]+>\s*\((.*)\)", value)
        if cast:
            value = cast.group(1).strip()
            continue
        c_cast = re.fullmatch(
            rf"\(\s*{INTEGRAL_TYPE_PATTERN}\s*\)\s*(.+)", value)
        if c_cast:
            value = c_cast.group(1).strip()
            continue
        break
    return value


def choice_count_matches(
        expression: str,
        array_expression: str,
        count: int,
        constants: dict[str, float]) -> bool:
    value = strip_integral_casts(expression)
    if value in {f"{array_expression}.size()", f"IM_ARRAYSIZE({array_expression})"}:
        return True
    resolved = resolve_numeric_expression(value, constants)
    return resolved is not None and resolved.is_integer() and int(resolved) == count


def collect_string_array_providers(paths: list[Path]) -> dict[str, tuple[tuple[str, str], ...]]:
    candidates: dict[str, list[tuple[tuple[str, str], ...]]] = {}
    function_pattern = re.compile(
        r"\b(?:inline\s+)?(?:static\s+)?const\s+auto\s*&\s+"
        r"((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*\(\s*\)\s*\{")
    array_pattern = re.compile(
        r"\bstatic\s+(?:(?:const|constexpr)\s+)?auto\s+([A-Za-z_]\w*)\s*=\s*"
        r"std::array(?:\s*<[^;{}\n]+>)?\s*\{")
    for path in paths:
        text = read_text(path)
        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        masked = mask_cpp_source(text)
        for function in function_pattern.finditer(masked):
            body_end = find_matching_brace(text, function.end() - 1)
            if body_end < 0:
                continue
            body = text[function.end():body_end]
            masked_body = masked[function.end():body_end]
            arrays = list(array_pattern.finditer(masked_body))
            if len(arrays) != 1 or masked_body[:arrays[0].start()].strip():
                continue
            close = find_matching_brace(body, arrays[0].end() - 1)
            if close < 0:
                continue
            semicolon = masked_body.find(";", close + 1)
            if semicolon < 0 or not re.fullmatch(
                    rf"\s*return\s+{re.escape(arrays[0].group(1))}\s*;\s*",
                    masked_body[semicolon + 1:]):
                continue
            values = parse_string_array_items(body[arrays[0].end():close], prefix)
            if values:
                candidates.setdefault(function.group(1).split("::")[-1], []).append(values)
    return {name: values[0] for name, values in candidates.items() if len(values) == 1}


def resolve_provider_combo_choices(
        body: str,
        position: int,
        items_expression: str,
        count_expression: str,
        providers: dict[str, tuple[tuple[str, str], ...]],
        constants: dict[str, float]) -> list[tuple[int, str, str]]:
    items = re.fullmatch(r"([A-Za-z_]\w*)\.data\(\)", items_expression.strip())
    if not items or not count_expression:
        return []
    alias = items.group(1)
    declarations = list(re.finditer(
        rf"\b(?:const\s+)?auto\s*&?\s+{re.escape(alias)}\s*=\s*"
        r"((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*\(\s*\)\s*;",
        mask_cpp_source(body[:position])))
    if len(declarations) != 1:
        return []
    provider = declarations[0].group(1).split("::")[-1]
    values = providers.get(provider)
    if not values or not choice_count_matches(count_expression, alias, len(values), constants):
        return []
    return [(index, label, key) for index, (label, key) in enumerate(values)]


def resolve_choice_label_call(
        source: str,
        expression: str,
        prefix: str,
        constants: dict[str, float]) -> tuple[str, str] | None:
    call = re.fullmatch(r"([A-Za-z_]\w*)\s*\((.*)\)", expression.strip())
    if not call:
        return None
    value = resolve_numeric_expression(call.group(2), constants)
    if value is None or not value.is_integer():
        return None

    function_pattern = re.compile(
        rf"\b(?:const\s+)?char\s*\*\s*{re.escape(call.group(1))}\s*\([^;{{]*\)\s*\{{")
    for function in function_pattern.finditer(mask_cpp_source(source)):
        body_end = find_matching_brace(source, function.end() - 1)
        if body_end < 0:
            continue
        body = source[function.end():body_end]
        masked_body = mask_cpp_source(body)
        cases = list(re.finditer(r"\bcase\s+([^:]+)\s*:", masked_body))
        for index, case in enumerate(cases):
            case_value = resolve_numeric_expression(case.group(1), constants)
            if case_value != value:
                continue
            next_case = cases[index + 1].start() if index + 1 < len(cases) else len(body)
            default = masked_body.find("default", case.end(), next_case)
            segment_end = default if default >= 0 else next_case
            return extract_i18n_call(body[case.end():segment_end], prefix)
    return None


def resolve_combo_choices(
        body: str,
        position: int,
        expression: str,
        prefix: str,
        source: str = "",
        constants: dict[str, float] | None = None) -> list[tuple[int, str, str]]:
    joined = parse_cpp_string_expression(expression)
    if joined is not None:
        return [
            (index, label, "")
            for index, label in enumerate(part for part in joined.split("\0") if part)
        ]

    variable = expression.strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", variable):
        return []
    declaration = list(re.finditer(
        rf"\b(?:const\s+)?char\s*\*\s*(?:const\s+)?{re.escape(variable)}\s*\[[^\]]*\]\s*=\s*\{{",
        body[:position]))
    if not declaration:
        return []
    open_brace = declaration[-1].end() - 1
    close_brace = find_matching_brace(body, open_brace)
    if close_brace < 0:
        return []

    items = [item for item in split_args(body[open_brace + 1:close_brace]) if item.strip()]
    choices = []
    for index, item in enumerate(items):
        translated = extract_i18n_call(item, prefix)
        if not translated and source:
            translated = resolve_choice_label_call(
                source, item, prefix, constants or {})
        if translated:
            choices.append((index, translated[1], translated[0]))
            continue
        literal = parse_cpp_string_expression(item)
        if literal is None:
            return []
        choices.append((index, literal, ""))
    return choices


def finalize_ui_choices(
        collected: dict[tuple[str, tuple[str, ...]], list[tuple[int, str, str]]]
        ) -> dict[tuple[str, tuple[str, ...]], tuple[tuple[int, str, str], ...]]:
    finalized = {}
    for identity, choices in collected.items():
        by_value: dict[int, tuple[int, str, str]] = {}
        conflict = False
        for choice in choices:
            previous = by_value.get(choice[0])
            if previous is not None and previous != choice:
                conflict = True
                break
            by_value[choice[0]] = choice
        if not conflict and len(by_value) >= 2:
            finalized[identity] = tuple(
                by_value[value] for value in sorted(by_value))
    return finalized


def collect_type_aliases(text: str) -> dict[str, str]:
    return {
        match.group(1): clean_type(match.group(2))
        for match in re.finditer(
            r"\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]+);",
            mask_cpp_source(text))
    }


def resolve_type_alias(type_name: str, aliases: dict[str, str]) -> str:
    resolved = clean_type(type_name)
    seen = set()
    while resolved in aliases and resolved not in seen:
        seen.add(resolved)
        resolved = clean_type(aliases[resolved])
    return resolved.split("::")[-1]


def find_parameter_origin(
        expression: str,
        parameters: tuple[tuple[str, str, str | None], ...]
        ) -> tuple[int, tuple[str, ...]] | None:
    for index, (_, name, _) in enumerate(parameters):
        match = re.fullmatch(
            rf"\s*&?\s*{re.escape(name)}"
            r"((?:\.[A-Za-z_]\w*)*)\s*",
            expression)
        if match:
            member_path = tuple(
                part for part in match.group(1).split(".") if part)
            return index, member_path
    return None


def unwrap_combo_proxy(expression: str, allow_clamp: bool = False) -> str:
    value = expression.strip()
    while value:
        if value.startswith("(") and find_matching_paren(value, 0) == len(value) - 1:
            value = value[1:-1].strip()
            continue
        cast = re.fullmatch(
            r"(?:static_cast|const_cast)\s*<[^>]+>\s*\((.*)\)", value)
        if cast:
            value = cast.group(1).strip()
            continue
        clamp = re.fullmatch(r"(?:[A-Za-z_]\w*::)*Clamp[A-Za-z_]\w*\s*\((.*)\)", value)
        if allow_clamp and clamp and len(split_args(clamp.group(1))) == 1:
            value = clamp.group(1).strip()
            continue
        break
    return value


def find_storage_parameter(
        expression: str,
        parameters: tuple[tuple[str, str, str | None], ...],
        body_before_control: str,
        body_after_control: str) -> int | None:
    direct = find_parameter_origin(expression, parameters)
    if direct and not direct[1]:
        return direct[0]

    local = re.fullmatch(r"\s*&?\s*([A-Za-z_]\w*)\s*", expression)
    if not local:
        return None
    local_name = local.group(1)
    assignments = list(re.finditer(
        rf"\b{re.escape(local_name)}\s*=\s*(?!=)([^;]+);",
        mask_cpp_source(body_before_control)))
    if not assignments:
        return None
    origin = find_parameter_origin(
        unwrap_combo_proxy(assignments[-1].group(1), True), parameters)
    if not origin or origin[1]:
        return None
    parameter_name = parameters[origin[0]][1]
    writes = re.findall(
        rf"\b{re.escape(parameter_name)}\s*=\s*(?!=)([^;]+);",
        mask_cpp_source(body_after_control))
    if not writes or any(
            unwrap_combo_proxy(write) != local_name for write in writes):
        return None
    return origin[0]




def _control_category(
        body: str, position: int, prefix: str) -> tuple[str, str]:
    scoped = [
        category for category in _collect_scoped_categories(body, prefix)
        if category[0] < position < category[1]
    ]
    if scoped:
        scoped.sort(key=lambda category: (category[0], -category[1]))
        return scoped[-1][2], scoped[-1][3]

    candidates = []
    pattern = re.compile(
        r"\b(?:ImGui|Util)::(?:" +
        "|".join(sorted(PERSISTENT_CATEGORY_CONTROL_NAMES)) + r")\s*\(")
    masked = mask_cpp_source(body[:position])
    for category in pattern.finditer(masked):
        close = find_matching_paren(body, category.end() - 1)
        if close < 0 or close >= position:
            continue
        translated = extract_i18n_call(
            body[category.start():close + 1], prefix)
        if translated:
            candidates.append((category.start(), translated[1], translated[0]))
    if not candidates:
        return "", ""
    _, label, key = max(candidates, key=lambda candidate: candidate[0])
    return label, key


def collect_member_selector_helpers(text: str):
    candidates = {}
    constants = collect_numeric_constants(text)
    pattern = re.compile(
        r"\b(?:const\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*\s*&\s*"
        r"([A-Za-z_]\w*)\s*\(([^)]*)\)\s*(?:const\s*)?\{")
    masked = mask_cpp_source(text)
    for function in pattern.finditer(masked):
        body_end = find_matching_brace(text, function.end() - 1)
        if body_end < 0:
            continue
        parameters = parse_helper_parameters(
            text[function.start(2):function.end(2)])
        body = text[function.end():body_end]
        masked_body = masked[function.end():body_end]
        switches = list(re.finditer(r"\bswitch\s*\(", masked_body))
        if len(switches) != 1:
            continue
        switch = switches[0]
        condition_end = find_matching_paren(body, switch.end() - 1)
        block_start = masked_body.find("{", condition_end + 1)
        if condition_end < 0 or block_start < 0:
            continue
        block_end = find_matching_brace(body, block_start)
        selector_origin = find_parameter_origin(
            body[switch.end():condition_end], parameters)
        if block_end < 0 or not selector_origin or not selector_origin[1]:
            continue

        switch_body = body[block_start + 1:block_end]
        masked_switch_body = masked_body[block_start + 1:block_end]
        clauses = list(re.finditer(
            r"\b(?:(case)\s+([^:]+)|(default))\s*:", masked_switch_body))
        case_paths = []
        default_path = None
        valid = bool(clauses)
        for index, clause in enumerate(clauses):
            clause_end = clauses[index + 1].start() if index + 1 < len(clauses) else len(switch_body)
            returns = list(re.finditer(
                r"\breturn\s+([^;]+);", switch_body[clause.end():clause_end]))
            if len(returns) != 1:
                valid = False
                break
            origin = find_parameter_origin(returns[0].group(1), parameters)
            if not origin or origin[0] != selector_origin[0] or not origin[1]:
                valid = False
                break
            if clause.group(3):
                if default_path is not None:
                    valid = False
                    break
                default_path = origin[1]
                continue
            value = resolve_numeric_expression(clause.group(2), constants)
            if value is None or not value.is_integer():
                valid = False
                break
            case_paths.append((int(value), origin[1]))
        if (not valid or default_path is None or
                len({value for value, _ in case_paths}) != len(case_paths)):
            continue
        summary = (
            selector_origin[0], selector_origin[1],
            tuple(sorted(case_paths)), default_path)
        candidates.setdefault(function.group(1), set()).add(summary)
    return {
        name: next(iter(summaries))
        for name, summaries in candidates.items()
        if len(summaries) == 1
    }


def collect_member_selector_contexts(paths: list[Path], ui_choices):
    collected = {}
    method_pattern = re.compile(
        r"\b(?:bool|void)\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)::DrawSettings\s*"
        r"\(([^)]*)\)\s*(?:const\s*)?\{")
    alias_pattern = re.compile(
        r"\b(?:const\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*\s*&\s*"
        r"[A-Za-z_]\w*\s*=\s*([A-Za-z_]\w*)\s*\(([^;]*)\)\s*;")
    for path in paths:
        text = read_text(path)
        masked = mask_cpp_source(text)
        aliases = collect_type_aliases(text)
        helpers = collect_member_selector_helpers(text)
        for method in method_pattern.finditer(masked):
            body_end = find_matching_brace(text, method.end() - 1)
            if body_end < 0:
                continue
            parameters = parse_helper_parameters(
                text[method.start(2):method.end(2)])
            feature = method.group(1).split("::")[-1]
            body = masked[method.end():body_end]
            for alias in alias_pattern.finditer(body):
                helper = helpers.get(alias.group(1))
                arguments = split_args(alias.group(2))
                if not helper or helper[0] >= len(arguments):
                    continue
                origin = find_parameter_origin(arguments[helper[0]], parameters)
                if not origin or origin[1]:
                    continue
                selector_path = (*origin[1], *helper[1])
                choices = ui_choices.get((feature, selector_path))
                if not choices:
                    continue
                cases = dict(helper[2])
                mapped = [
                    (cases.get(value, helper[3]), label, key)
                    for value, label, key in choices
                ]
                returned_paths = {path for _, path in helper[2]} | {helper[3]}
                if (len({member for member, _, _ in mapped}) != len(mapped) or
                        {member for member, _, _ in mapped} != returned_paths):
                    continue
                owner = resolve_type_alias(parameters[origin[0]][0], aliases)
                if not owner or owner in PRIMITIVE_TYPES or owner in VECTOR_COMPONENTS:
                    continue
                for member, label, key in mapped:
                    identity = (owner, (*origin[1], *member))
                    collected.setdefault(identity, set()).add((label, key))
    return {
        identity: next(iter(candidates))
        for identity, candidates in collected.items()
        if len(candidates) == 1
    }


def _collect_record_schemas(text: str, masked: str | None = None):
    masked = masked if masked is not None else mask_cpp_source(text)
    schemas = {}
    for declaration in re.finditer(STRUCT_DECL_RE, masked):
        body_end = find_matching_brace(text, declaration.end() - 1)
        if body_end < 0:
            continue
        fields = tuple(
            parsed[0]
            for statement in masked[declaration.end():body_end].split(";")
            if (parsed := parse_field_statement(statement))
        )
        if fields:
            schemas[declaration.group(1)] = fields
    return schemas


def _collect_record_label_providers(text: str, prefix: str):
    masked = mask_cpp_source(text)
    schemas = _collect_record_schemas(text, masked)

    candidates = {}
    provider_pattern = re.compile(
        r"\bstd::array\s*<\s*([A-Za-z_]\w*)\s*,[^>]+>\s+"
        r"([A-Za-z_]\w*)\s*\([^)]*\)\s*\{")
    for provider in provider_pattern.finditer(masked):
        fields = schemas.get(provider.group(1))
        body_end = find_matching_brace(text, provider.end() - 1)
        if not fields or body_end < 0:
            continue
        rows = []
        body = text[provider.end():body_end]
        for row in re.finditer(
                rf"\b{re.escape(provider.group(1))}\s*\{{", mask_cpp_source(body)):
            row_end = find_matching_brace(body, row.end() - 1)
            values = split_args(body[row.end():row_end]) if row_end >= 0 else []
            if len(values) == len(fields):
                rows.append(values)
        label_columns = []
        for field_index, field in enumerate(fields):
            labels = []
            for row in rows:
                translated = extract_i18n_call(row[field_index], prefix)
                literal = parse_cpp_string_expression(row[field_index])
                if translated:
                    labels.append((translated[1], translated[0]))
                elif literal is not None:
                    labels.append((literal, ""))
                else:
                    labels = []
                    break
            if len(labels) >= 2:
                label_columns.append((field, tuple(labels)))
        if len(label_columns) == 1:
            candidates.setdefault(provider.group(2), set()).add(
                (provider.group(1), *label_columns[0]))
    return {
        name: next(iter(values))
        for name, values in candidates.items()
        if len(values) == 1
    }


def collect_index_selector_contexts(paths: list[Path]):
    collected = {}
    for path in paths:
        text = read_text(path)
        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        providers = _collect_record_label_providers(text, prefix)
        functions = [
            (function.name,
             tuple((value.type_name, value.name, value.default)
                   for value in function.parameters),
             function.parameter_text, function.body)
            for function in collect_source_functions([path])
        ]
        definitions = {}
        for definition in functions:
            definitions.setdefault(definition[0], []).append(definition)
        wrapper_candidates = {}
        for wrapper_name, wrapper_parameters, _, wrapper_body in functions:
            parameter_indices = {
                name: index for index, (_, name, _) in enumerate(wrapper_parameters)
            }
            for local in re.finditer(
                    r"\b(?:const\s+)?auto\s+([A-Za-z_]\w*)\s*=\s*"
                    r"([A-Za-z_]\w*)\s*\(\s*\)\s*;", wrapper_body):
                provider = providers.get(local.group(2))
                if not provider:
                    continue
                for call in re.finditer(r"\b([A-Za-z_]\w*)\s*\(([^;]+)\)\s*;", wrapper_body):
                    arguments = split_args(call.group(2))
                    indexed = next((
                        (index, match.group(1))
                        for index, argument in enumerate(arguments)
                        if (match := re.fullmatch(
                            rf"\s*{re.escape(local.group(1))}\s*\[\s*([A-Za-z_]\w*)\s*\]\s*",
                            argument))
                    ), None)
                    if not indexed or len(definitions.get(call.group(1), ())) != 1:
                        continue
                    leaf = definitions[call.group(1)][0]
                    if indexed[0] >= len(leaf[1]):
                        continue
                    record_name = leaf[1][indexed[0]][1]
                    index_argument = next((
                        index for index, argument in enumerate(arguments)
                        if strip_integral_casts(argument) == indexed[1]), None)
                    selector_argument = next((
                        index for index, argument in enumerate(arguments)
                        if argument.strip() in parameter_indices), None)
                    if (record_name is None or index_argument is None or
                            selector_argument is None or
                            max(indexed[0], index_argument, selector_argument) >= len(leaf[1])):
                        continue
                    record_parameter = leaf[1][indexed[0]][1]
                    index_parameter = leaf[1][index_argument][1]
                    selector_parameter = leaf[1][selector_argument][1]
                    if not all((record_parameter, index_parameter, selector_parameter)):
                        continue
                    visible = re.search(
                        rf"\b(?:Text|TextUnformatted|TextWrapped)\s*\([^;]*"
                        rf"\b{re.escape(record_parameter)}\s*\.\s*{re.escape(provider[1])}\b",
                        leaf[3])
                    writes = re.findall(
                        rf"\b{re.escape(selector_parameter)}\s*=(?!=)\s*([^;]+);", leaf[3])
                    if (not visible or len(writes) != 1 or
                            strip_integral_casts(writes[0]) != index_parameter):
                        continue
                    wrapper_candidates.setdefault(wrapper_name, set()).add((
                        parameter_indices[arguments[selector_argument].strip()], provider[2]))

        wrappers = {
            name: next(iter(values))
            for name, values in wrapper_candidates.items()
            if len(values) == 1
        }
        draw_settings = extract_draw_settings_body(text)
        if not draw_settings:
            continue
        feature, body = draw_settings
        for call in re.finditer(r"\b([A-Za-z_]\w*)\s*\(([^;]+)\)\s*;", body):
            wrapper = wrappers.get(call.group(1))
            arguments = split_args(call.group(2))
            if not wrapper or wrapper[0] >= len(arguments):
                continue
            selector = strip_integral_casts(arguments[wrapper[0]])
            if not re.fullmatch(r"[A-Za-z_]\w*", selector):
                continue
            paths_for_selector = {
                normalize_setting_path(match.group(1))
                for match in re.finditer(
                    rf"\b(?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
                    rf"([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*\[\s*{re.escape(selector)}\s*\]",
                    body)
            }
            for setting_path in paths_for_selector:
                for index, label in enumerate(wrapper[1]):
                    collected.setdefault(
                        (feature, (*setting_path, str(index))), set()).add(label)
    return {
        identity: next(iter(candidates))
        for identity, candidates in collected.items()
        if len(candidates) == 1
    }




def _function_control_roots(functions):
    unique_functions = {
        name: (parameters, body)
        for name, parameters, body in functions
        if sum(candidate[0] == name for candidate in functions) == 1
    }
    roots = {name: set() for name in unique_functions}

    for name, (parameters, body) in unique_functions.items():
        masked = mask_cpp_source(body)
        for control in DIRECT_UI_CONTROL_RE.finditer(masked):
            close = find_matching_paren(body, control.end() - 1)
            if close < 0:
                continue
            args = split_args(body[control.end():close])
            storage_index = control_storage_argument_index(control.group(1))
            if len(args) <= storage_index:
                continue
            origin = find_parameter_origin(args[storage_index], parameters)
            if origin:
                roots[name].add(origin[0])

    changed = True
    while changed:
        changed = False
        for name, (parameters, body) in unique_functions.items():
            masked = mask_cpp_source(body)
            for callee, callee_roots in roots.items():
                if not callee_roots:
                    continue
                for invocation in re.finditer(
                        rf"\b{re.escape(callee)}\s*\(", masked):
                    close = find_matching_paren(body, invocation.end() - 1)
                    if close < 0:
                        continue
                    args = split_args(body[invocation.end():close])
                    for callee_root in callee_roots:
                        if callee_root >= len(args):
                            continue
                        origin = find_parameter_origin(args[callee_root], parameters)
                        if origin and origin[0] not in roots[name]:
                            roots[name].add(origin[0])
                            changed = True
    return unique_functions, roots


def collect_labeled_helper_roots(paths: list[Path]):
    collected = {}
    method_pattern = re.compile(
        r"\b(?:bool|void)\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)::"
        r"(Draw[A-Za-z_]\w*)\s*\([^;{]*\)\s*(?:const\s*)?\{")
    category_pattern = re.compile(
        r"\b(?:ImGui|Util)::(?:TreeNode|TreeNodeEx|CollapsingHeader)\s*\(")

    for path in paths:
        text = read_text(path)
        masked_text = mask_cpp_source(text)
        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        functions = [
            (function.name,
             tuple((value.type_name, value.name, value.default)
                   for value in function.parameters),
             function.body)
            for function in collect_source_functions([path])
            if function.name.startswith("Draw")
        ]
        unique_functions, control_roots = _function_control_roots(functions)
        summaries = {}
        for name, (parameters, body) in unique_functions.items():
            if len(control_roots[name]) != 1:
                continue
            label_parameters = set()
            masked_body = mask_cpp_source(body)
            for category in category_pattern.finditer(masked_body):
                close = find_matching_paren(body, category.end() - 1)
                args = split_args(body[category.end():close]) if close >= 0 else []
                if not args:
                    continue
                origin = find_parameter_origin(args[0], parameters)
                if origin and not origin[1]:
                    label_parameters.add(origin[0])
            if len(label_parameters) == 1:
                summaries[name] = (
                    next(iter(label_parameters)), next(iter(control_roots[name])))

        for method in method_pattern.finditer(masked_text):
            body_end = find_matching_brace(text, method.end() - 1)
            if body_end < 0:
                continue
            owner = method.group(1).split("::")[-1]
            body = text[method.end():body_end]
            masked_body = masked_text[method.end():body_end]
            for helper, (label_parameter, storage_parameter) in summaries.items():
                for invocation in re.finditer(
                        rf"\b{re.escape(helper)}\s*\(", masked_body):
                    close = find_matching_paren(body, invocation.end() - 1)
                    if close < 0:
                        continue
                    args = split_args(body[invocation.end():close])
                    if max(label_parameter, storage_parameter) >= len(args):
                        continue
                    storage = re.fullmatch(
                        r"\s*(?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
                        r"([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*",
                        args[storage_parameter])
                    translated = extract_i18n_call(args[label_parameter], prefix)
                    if not storage or not translated:
                        continue
                    identity = owner, normalize_setting_path(storage.group(1))
                    collected.setdefault(identity, set()).add(
                        (translated[1].split("##", 1)[0], translated[0]))

    return {
        identity: next(iter(candidates))
        for identity, candidates in collected.items()
        if len(candidates) == 1
    }


def integral_reference_parameters(parameter_text: str) -> set[int]:
    result = set()
    for index, parameter in enumerate(split_args(parameter_text)):
        declaration = parameter.partition("=")[0].strip()
        name = re.search(r"([A-Za-z_]\w*)\s*$", declaration)
        if not name or "&" not in declaration[:name.start()]:
            continue
        if PRIMITIVE_TYPES.get(clean_type(declaration[:name.start()])) == "Integer":
            result.add(index)
    return result


def collect_mapped_combo_helpers(paths: list[Path]) -> dict[str, MappedComboHelper]:
    definitions: dict[str, list[MappedComboHelper | None]] = {}
    for path in paths:
        text = read_text(path)
        prefix_match = re.search(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"', text)
        prefix = prefix_match.group(1) if prefix_match else ""
        companion_header = path.with_suffix(".h")
        constants = collect_numeric_constants(
            text + (read_text(companion_header) if companion_header.exists() else ""))
        string_arrays, integer_arrays = collect_literal_std_arrays(text, prefix, constants)
        for function in collect_source_functions([path]):
            name, parameter_text, body = (
                function.name, function.parameter_text, function.body)
            parameters = tuple(
                (value.type_name, value.name, value.default)
                for value in function.parameters)
            summary = None
            masked_body = mask_cpp_source(body)
            combos = list(re.finditer(r"\b(?:ImGui|Util)::Combo\s*\(", masked_body))
            if len(combos) == 1:
                combo = combos[0]
                close = find_matching_paren(body, combo.end() - 1)
                args = split_args(body[combo.end():close]) if close >= 0 else []
                proxy = re.fullmatch(r"\s*&\s*([A-Za-z_]\w*)\s*", args[1]) if len(args) >= 4 else None
                items = re.fullmatch(
                    r"((?:[A-Za-z_]\w*::)*([A-Za-z_]\w*))\.data\(\)",
                    args[2].strip()) if proxy else None
                labels = string_arrays.get(items.group(2)) if items else None
                if labels and choice_count_matches(args[3], items.group(1), len(labels), constants):
                    candidates = []
                    for parameter_index in integral_reference_parameters(parameter_text):
                        parameter_name = parameters[parameter_index][1]
                        assignments = list(re.finditer(
                            rf"\b{re.escape(parameter_name)}\s*=\s*(?!=)([^;]+);",
                            masked_body))
                        mappings = []
                        for assignment in assignments:
                            mapped = re.fullmatch(
                                rf"((?:[A-Za-z_]\w*::)*([A-Za-z_]\w*))\s*\[\s*"
                                rf"{re.escape(proxy.group(1))}\s*\]",
                                strip_integral_casts(assignment.group(1)))
                            if not mapped:
                                mappings = []
                                break
                            mappings.append((assignment.start(), mapped.group(2)))
                        if mappings and any(position > close for position, _ in mappings) and len({
                                array_name for _, array_name in mappings}) == 1:
                            values = integer_arrays.get(mappings[0][1])
                            if values and len(values) == len(labels) and len(set(values)) == len(values):
                                candidates.append((parameter_index, values))
                    if len(candidates) == 1:
                        label_origin = find_parameter_origin(args[0], parameters)
                        label_parameter = label_origin[0] if label_origin and not label_origin[1] else None
                        translated = extract_i18n_call(args[0], prefix)
                        label_key, label = translated if translated else (
                            "", parse_cpp_string_expression(args[0]) or "")
                        summary = MappedComboHelper(
                            candidates[0][0], label_parameter, label, label_key,
                            tuple((value, labels[index][0], labels[index][1])
                                  for index, value in enumerate(candidates[0][1])))
            definitions.setdefault(name, []).append(summary)
    return {
        name: summaries[0]
        for name, summaries in definitions.items()
        if len(summaries) == 1 and summaries[0] is not None
    }






_SOURCE_FUNCTION_PATTERN = re.compile(
    r"\b(?:static\s+)?(?:inline\s+)?(?:bool|void)\s+"
    r"(?:(?P<owner>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)::)?"
    r"(?P<name>[A-Za-z_]\w*)\s*\((?P<parameters>[^)]*)\)\s*(?:const\s*)?\{")
_NAMESPACE_PATTERN = re.compile(
    r"\bnamespace\s+(?:inline\s+)?([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\{")
_I18N_PREFIX_PATTERN = re.compile(r'#define\s+I18N_KEY_PREFIX\s+"([^"]*)"')


@functools.lru_cache(maxsize=None)
def _file_source_functions(
        path: Path, include_qualifiers: bool) -> tuple[SourceFunction, ...]:
    """Functions declared in one file. Cached: callers scan the same tree repeatedly."""
    functions = []
    text = read_text(path)
    masked = mask_cpp_source(text)
    namespace_ranges = []
    if include_qualifiers:
        for namespace in _NAMESPACE_PATTERN.finditer(masked):
            namespace_end = find_matching_brace(text, namespace.end() - 1)
            if namespace_end >= 0:
                namespace_ranges.append((
                    namespace.start(), namespace_end,
                    tuple(namespace.group(1).split("::"))))
    prefix_match = _I18N_PREFIX_PATTERN.search(text)
    prefix = prefix_match.group(1) if prefix_match else ""
    for match in _SOURCE_FUNCTION_PATTERN.finditer(masked):
        body_end = find_matching_brace(text, match.end() - 1)
        if body_end < 0:
            continue
        parameters = tuple(
            SourceParameter(type_name, name, default)
            for type_name, name, default in parse_helper_parameters(
                text[match.start("parameters"):match.end("parameters")]))
        body = text[match.end():body_end]
        namespace_path = tuple(
            segment
            for start, end, namespace in namespace_ranges
            if start < match.start() < end
            for segment in namespace)
        owner_parts = tuple((match.group("owner") or "").split("::"))
        owner_parts = tuple(part for part in owner_parts if part)
        functions.append(SourceFunction(
            match.group("name"),
            owner_parts[-1] if owner_parts else "",
            (*namespace_path, *owner_parts),
            parameters,
            body,
            mask_cpp_source(body),
            prefix,
            path,
            text[match.start("parameters"):match.end("parameters")]))
    return tuple(functions)


def collect_source_functions(
        paths: list[Path], include_qualifiers: bool = False) -> tuple[SourceFunction, ...]:
    return tuple(
        function
        for path in paths
        for function in _file_source_functions(path, include_qualifiers))


def _localized_text(expression: str, prefix: str) -> LocalizedText | None:
    conditional = re.fullmatch(
        r"\s*\(?\s*(true|false)\s*\)?\s*\?\s*(.+)\s*:\s*(.+)\s*",
        expression, re.DOTALL)
    if conditional:
        return _localized_text(
            conditional.group(2 if conditional.group(1) == "true" else 3),
            prefix)
    translated = extract_i18n_call(expression, prefix)
    if translated:
        return LocalizedText(translated[1].split("##", 1)[0], translated[0])
    literal = parse_cpp_string_expression(expression)
    return LocalizedText(literal.split("##", 1)[0]) if literal else None


def resolve_record_array_radio_choices(
        body: str, position: int, label_expression: str,
        value_expression: str, prefix: str,
        constants: dict[str, float]) -> tuple[tuple[int, str, str], ...]:
    label_member = re.fullmatch(
        r"\s*([A-Za-z_]\w*)\.([A-Za-z_]\w*)\s*", label_expression)
    value_member = re.fullmatch(
        r"\s*([A-Za-z_]\w*)\.([A-Za-z_]\w*)\s*",
        strip_integral_casts(value_expression))
    if (not label_member or not value_member or
            label_member.group(1) != value_member.group(1)):
        return ()

    masked = mask_cpp_source(body)
    ranges = {}
    for loop in re.finditer(
            r"\bfor\s*\(\s*[^;:()]*?\b([A-Za-z_]\w*)\s*:\s*"
            r"([A-Za-z_]\w*)\s*\)", masked[:position]):
        block_start = masked.find("{", loop.end())
        block_end = find_matching_brace(body, block_start) if block_start >= 0 else -1
        if block_start < position < block_end:
            ranges[loop.group(1)] = loop.group(2)

    array_name = label_member.group(1)
    seen = set()
    while array_name in ranges and array_name not in seen:
        seen.add(array_name)
        array_name = ranges[array_name]
    if not seen:
        return ()

    schemas = _collect_record_schemas(body, masked)

    declarations = list(re.finditer(
        rf"\b(?:(?:static|constexpr|const)\s+)*"
        rf"([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s+{re.escape(array_name)}\s*"
        r"(?:\[[^\]]*\]\s*)+\s*=\s*\{", masked))
    if len(declarations) != 1:
        return ()
    declaration = declarations[0]
    fields = schemas.get(declaration.group(1).split("::")[-1])
    if (not fields or label_member.group(2) not in fields or
            value_member.group(2) not in fields):
        return ()

    array_end = find_matching_brace(body, declaration.end() - 1)
    if array_end < 0:
        return ()
    initializer = body[declaration.end():array_end]
    masked_initializer = mask_cpp_source(initializer)
    label_index = fields.index(label_member.group(2))
    value_index = fields.index(value_member.group(2))
    choices = []
    for record in re.finditer(r"\{", masked_initializer):
        record_end = find_matching_brace(initializer, record.start())
        values = split_args(initializer[record.end():record_end]) if record_end >= 0 else []
        if len(values) != len(fields):
            continue
        value = resolve_numeric_expression(values[value_index], constants)
        label = _localized_text(values[label_index], prefix)
        if value is not None and value.is_integer() and label:
            choices.append((int(value), label.text, label.key))
    return tuple(choices)


def _project_standard_controls(
        paths: list[Path], provider_paths: list[Path]) -> dict[
            tuple[str, tuple[str, ...]], ControlBinding]:
    functions = collect_source_functions(paths, include_qualifiers=True)
    definitions: dict[str, list[SourceFunction]] = {}
    for function in functions:
        definitions.setdefault(function.name, []).append(function)
    def resolve_callee(name: str, qualifier: tuple[str, ...], argument_count: int):
        candidates = [
            function for function in definitions.get(name, ())
            if sum(parameter.default is None for parameter in function.parameters) <=
            argument_count <= len(function.parameters)
        ]
        if qualifier:
            qualified = [
                function for function in candidates
                if len(function.qualifier) >= len(qualifier) and
                function.qualifier[-len(qualifier):] == qualifier
            ]
            candidates = qualified
        return candidates[0] if len(candidates) == 1 else None
    text_by_path = {path: read_text(path) for path in paths}
    aliases_by_path = {
        path: collect_type_aliases(text) for path, text in text_by_path.items()
    }
    constants_by_path = {
        path: collect_numeric_constants(
            text + (read_text(path.with_suffix(".h"))
                    if path.with_suffix(".h").exists() else ""))
        for path, text in text_by_path.items()
    }
    selector_helpers_by_path = {
        path: collect_member_selector_helpers(text)
        for path, text in text_by_path.items()
    }
    providers = collect_string_array_providers(provider_paths)
    call_sites = {}
    for function in functions:
        calls = []
        for invocation in re.finditer(
                r"\b((?:[A-Za-z_]\w*::)*)([A-Za-z_]\w*)\s*\(",
                function.masked_body):
            qualifier = tuple(
                part for part in invocation.group(1).split("::") if part)
            name = invocation.group(2)
            if qualifier in {("ImGui",), ("Util",)} or name not in definitions:
                continue
            close = find_matching_paren(function.body, invocation.end() - 1)
            if close >= 0:
                arguments = split_args(function.body[invocation.end():close])
                callee = resolve_callee(
                    name, qualifier, len(arguments))
                if callee:
                    calls.append((callee, invocation.start(), arguments))
        call_sites[function] = calls
    draw_control_functions = {
        function for function in functions
        if function.name == "DrawSettings" and function.owner
    }
    for _ in range(len(functions)):
        discovered = {
            callee
            for caller in draw_control_functions
            for callee, _, _ in call_sites[caller]
            if callee.owner == caller.owner
        }
        if discovered <= draw_control_functions:
            break
        draw_control_functions.update(discovered)
    metadata_candidates = {}
    choice_candidates = {}

    def add_metadata(identity, binding, priority):
        """Records a candidate binding, keeping only those from the most direct evidence.

        Priority is how directly the control names the member: 3 = `settings.x` written
        literally in a DrawSettings body, 2 = recovered from the parameter origin of a typed
        helper, 1 = projected through a call chain. Ties accumulate instead of picking a
        winner, and a tie is later discarded as ambiguous.
        """
        previous_priority, candidates = metadata_candidates.get(
            identity, (-1, set()))
        if priority > previous_priority:
            metadata_candidates[identity] = (priority, {binding})
        elif priority == previous_priority:
            candidates.add(binding)

    def add_choices(identity, choices):
        choice_candidates.setdefault(identity, []).extend(choices)

    def resolve_choices(function, position, kind, args):
        if kind == "Combo" and len(args) >= 3:
            values = resolve_combo_choices(
                function.body, position, args[2], function.prefix,
                text_by_path[function.source], constants_by_path[function.source])
            if not values and len(args) >= 4:
                values = resolve_provider_combo_choices(
                    function.body, position, args[2], args[3], providers,
                    constants_by_path[function.source])
            return tuple(values)
        if kind == "RadioButton" and len(args) >= 3:
            value = resolve_numeric_expression(args[2], constants_by_path[function.source])
            label = _localized_text(args[0], function.prefix)
            if value is not None and value.is_integer() and label:
                return ((int(value), label.text, label.key),)
            return resolve_record_array_radio_choices(
                function.body, position, args[0], args[2], function.prefix,
                constants_by_path[function.source])
        return ()

    def make_binding(function, position, owner, setting_path, kind, args,
                     label_expression=None, category=None, choices=()):
        label_expression = label_expression if label_expression is not None else (
            args[0] if args else "")
        label = _localized_text(label_expression, function.prefix)
        if not label:
            translated = resolve_control_translation(
                function.body, position, label_expression,
                function.prefix, setting_path[-1])
            label = (LocalizedText(translated[1].split("##", 1)[0], translated[0])
                     if translated else None)
        if not label:
            label = LocalizedText(prettify(setting_path[-1]))
        if category is None:
            text, key = _control_category(function.body, position, function.prefix)
            category = LocalizedText(text, key)
        flags_expression = resolve_control_flags_expression(
            function.body, position, kind, list(args))
        numeric = get_control_numeric_metadata(
            kind, list(args), constants_by_path[function.source], flags_expression)
        minimum, maximum, scale = numeric or (None, None, 1.0)
        return ControlBinding(
            owner, setting_path, label, category, kind,
            minimum, maximum, scale, "Identity", tuple(choices),
            source_widget=control_source_widget(kind),
            clamp_numeric_input=control_clamps_numeric_input(
                kind, flags_expression),
            hdr_color=control_is_hdr_color(kind, flags_expression))

    templates: dict[SourceFunction, list[ControlTemplate]] = {}
    for function in functions:
        parameter_tuples = tuple(
            (parameter.type_name, parameter.name, parameter.default)
            for parameter in function.parameters)
        local_aliases = collect_local_setting_aliases(function.body)
        selected_aliases = {}
        selector_helpers = selector_helpers_by_path[function.source]
        for alias in re.finditer(
                r"\b(?:const\s+)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*\s*&\s*"
                r"([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*\(([^;]*)\)\s*;",
                function.masked_body):
            helper = selector_helpers.get(alias.group(2))
            call_args = split_args(alias.group(3))
            if not helper or helper[0] >= len(call_args):
                continue
            origin = find_parameter_origin(call_args[helper[0]], parameter_tuples)
            if origin and not origin[1]:
                selected_aliases[alias.group(1)] = (
                    origin[0], tuple(dict.fromkeys(
                        (*(path for _, path in helper[2]), helper[3]))))

        for control in DIRECT_UI_CONTROL_RE.finditer(function.masked_body):
            kind = control.group(1)
            close = find_matching_paren(function.body, control.end() - 1)
            args = split_args(function.body[control.end():close]) if close >= 0 else []
            storage_index = control_storage_argument_index(kind)
            if len(args) <= storage_index:
                continue
            choices = resolve_choices(function, control.start(), kind, args)
            setting_path = (
                extract_control_setting_path(kind, args, local_aliases)
                if function in draw_control_functions else None)
            if setting_path:
                identity = function.owner, setting_path
                add_metadata(identity, make_binding(
                    function, control.start(), *identity, kind, args, choices=choices), 3)
                add_choices(identity, choices)

            origins = []
            origin = find_parameter_origin(args[storage_index], parameter_tuples)
            if not origin and kind == "Combo":
                parameter = find_storage_parameter(
                    args[storage_index], parameter_tuples,
                    function.body[:control.start()], function.body[close + 1:])
                if parameter is not None:
                    origin = (parameter, ())
            if origin and origin[1]:
                origins.append(origin)
            else:
                for alias, (parameter_index, prefixes) in selected_aliases.items():
                    member = re.fullmatch(
                        rf".*&\s*{re.escape(alias)}\."
                        r"([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*).*",
                        args[storage_index].strip())
                    if member:
                        suffix = normalize_setting_path(member.group(1))
                        origins.extend((parameter_index, (*prefix, *suffix)) for prefix in prefixes)
            concrete_label = (
                resolve_control_translation(
                    function.body, control.start(), args[0] if args else "",
                    function.prefix, origins[0][1][-1] if origins and origins[0][1] else "")
                or _localized_text(args[0], function.prefix) if args else None)
            for parameter_index, member_path in origins:
                owner = resolve_type_alias(
                    function.parameters[parameter_index].type_name,
                    aliases_by_path[function.source])
                if (not concrete_label or not owner or owner in PRIMITIVE_TYPES or
                        owner in VECTOR_COMPONENTS):
                    continue
                identity = owner, member_path
                add_metadata(identity, make_binding(
                    function, control.start(), *identity, kind, args, choices=choices), 2)
                add_choices(identity, choices)

            parameter_origin = find_parameter_origin(
                args[storage_index], parameter_tuples)
            if not parameter_origin and kind == "Combo":
                parameter = find_storage_parameter(
                    args[storage_index], parameter_tuples,
                    function.body[:control.start()], function.body[close + 1:])
                if parameter is not None:
                    parameter_origin = (parameter, ())
            if parameter_origin:
                category_text, category_key = _control_category(
                    function.body, control.start(), function.prefix)
                templates.setdefault(function, []).append(ControlTemplate(
                    parameter_origin[0], parameter_origin[1],
                    args[0] if args else "", LocalizedText(category_text, category_key),
                    kind, tuple(args), choices))

    for function in functions:
        parameter_tuples = tuple(
            (parameter.type_name, parameter.name, parameter.default)
            for parameter in function.parameters)
        local_aliases = collect_local_setting_aliases(function.body)
        for control in SHIFT_UNIFIED_CONTROL_RE.finditer(function.masked_body):
            kind = f"ShiftSliderFloat{control.group(1)}"
            close = find_matching_paren(function.body, control.end() - 1)
            args = split_args(function.body[control.end():close]) if close >= 0 else []
            storage_index = control_storage_argument_index(kind)
            if len(args) <= storage_index:
                continue

            setting_path = (
                extract_control_setting_path(kind, args, local_aliases)
                if function in draw_control_functions else None)
            if setting_path:
                identity = function.owner, setting_path
                add_metadata(identity, make_binding(
                    function, control.start(), *identity, kind, args), 3)

            parameter_origin = find_parameter_origin(
                args[storage_index], parameter_tuples)
            if parameter_origin and parameter_origin[1]:
                owner = resolve_type_alias(
                    function.parameters[parameter_origin[0]].type_name,
                    aliases_by_path[function.source])
                label = _localized_text(args[0], function.prefix) if args else None
                if label and owner and owner not in PRIMITIVE_TYPES and owner not in VECTOR_COMPONENTS:
                    identity = owner, parameter_origin[1]
                    add_metadata(identity, make_binding(
                        function, control.start(), *identity, kind, args), 2)

            if parameter_origin:
                category_text, category_key = _control_category(
                    function.body, control.start(), function.prefix)
                templates.setdefault(function, []).append(ControlTemplate(
                    parameter_origin[0], parameter_origin[1],
                    args[0] if args else "", LocalizedText(category_text, category_key),
                    kind, tuple(args)))

    projected_helpers = collect_projected_numeric_helpers(paths)
    for function in functions:
        if function.name != "DrawSettings" or not function.owner:
            continue
        aliases = collect_local_setting_aliases(function.body)
        source_text = text_by_path[function.source] + (
            read_text(function.source.with_suffix(".h"))
            if function.source.with_suffix(".h").exists() else "")
        for helper_name, summary in projected_helpers.items():
            for invocation in re.finditer(
                    rf"\b(?:[A-Za-z_]\w*::)*{re.escape(helper_name)}\s*\(",
                    function.masked_body):
                close = find_matching_paren(function.body, invocation.end() - 1)
                call_args = split_args(
                    function.body[invocation.end():close]) if close >= 0 else []
                arguments = {
                    name: (call_args[index] if index < len(call_args) else default)
                    for index, (name, default) in enumerate(summary.parameters)
                    if index < len(call_args) or default is not None
                }
                required = (summary.storage_parameter, summary.label_parameter,
                            summary.component_labels_parameter)
                if any(summary.parameters[index][0] not in arguments for index in required):
                    continue
                storage = arguments[summary.parameters[summary.storage_parameter][0]]
                pseudo_args = [""] * (control_storage_argument_index(summary.control_kind) + 1)
                pseudo_args[-1] = storage
                setting_path = extract_control_setting_path(
                    summary.control_kind, pseudo_args, aliases)
                components = resolve_storage_vector_components(storage, source_text)
                if not setting_path or not components or setting_path[-1] not in components:
                    continue
                component_start = components.index(setting_path[-1])
                if component_start + summary.component_count > len(components):
                    continue
                labels = resolve_local_component_labels(
                    function.body, invocation.start(),
                    arguments[summary.parameters[summary.component_labels_parameter][0]],
                    function.prefix, summary.component_count)
                if not labels:
                    continue
                resolved_args = tuple(
                    substitute_helper_parameters(argument, arguments)
                    for argument in summary.control_args)
                label_expression = arguments[summary.parameters[summary.label_parameter][0]]
                base = make_binding(
                    function, invocation.start(), function.owner, setting_path,
                    summary.control_kind, resolved_args, label_expression)
                binding = ControlBinding(
                    base.owner, base.path, base.label, base.category,
                    f"ProjectedNumeric{summary.component_count}",
                    base.minimum, base.maximum, base.display_scale,
                    component_labels=tuple(LocalizedText(*label) for label in labels),
                    source_widget=base.source_widget,
                    clamp_numeric_input=base.clamp_numeric_input,
                    hdr_color=base.hdr_color)
                add_metadata((function.owner, setting_path), binding, 3)

    mapped_helpers = collect_mapped_combo_helpers(paths)
    for function in functions:
        if function.name != "DrawSettings" or not function.owner:
            continue
        aliases = collect_local_setting_aliases(function.body)
        for helper_name, summary in mapped_helpers.items():
            for invocation in re.finditer(
                    rf"\b(?:[A-Za-z_]\w*::)*{re.escape(helper_name)}\s*\(",
                    function.masked_body):
                close = find_matching_paren(function.body, invocation.end() - 1)
                call_args = split_args(
                    function.body[invocation.end():close]) if close >= 0 else []
                if summary.storage_parameter >= len(call_args):
                    continue
                storage = call_args[summary.storage_parameter]
                setting_path = extract_control_setting_path(
                    "Combo", ["", storage], aliases)
                if not setting_path:
                    continue
                label_expression = (
                    call_args[summary.label_parameter]
                    if summary.label_parameter is not None and
                    summary.label_parameter < len(call_args) else summary.label)
                binding = make_binding(
                    function, invocation.start(), function.owner, setting_path,
                    "Combo", (label_expression, storage), label_expression,
                    choices=summary.choices)
                add_metadata((function.owner, setting_path), binding, 3)
                add_choices((function.owner, setting_path), summary.choices)

    for _ in range(len(functions)):
        changed = False
        for wrapper in functions:
            wrapper_parameters = tuple(
                (parameter.type_name, parameter.name, parameter.default)
                for parameter in wrapper.parameters)
            for callee, position, call_args in call_sites[wrapper]:
                summaries = templates.get(callee, ())
                if callee == wrapper:
                    continue
                substitutions = {
                    parameter.name: (call_args[index] if index < len(call_args) else parameter.default)
                    for index, parameter in enumerate(callee.parameters)
                    if index < len(call_args) or parameter.default is not None
                }
                for summary in tuple(summaries):
                    storage_expression = substitutions.get(
                        callee.parameters[summary.storage_parameter].name)
                    if storage_expression is None:
                        continue
                    origin = find_parameter_origin(storage_expression, wrapper_parameters)
                    if not origin:
                        continue
                    category_text, category_key = _control_category(
                        wrapper.body, position, wrapper.prefix)
                    projected = ControlTemplate(
                        origin[0], (*origin[1], *summary.storage_path),
                        substitute_helper_parameters(summary.label_expression, substitutions),
                        LocalizedText(category_text, category_key)
                        if category_text else summary.category,
                        summary.control_kind,
                        tuple(substitute_helper_parameters(argument, substitutions)
                              for argument in summary.control_args),
                        summary.choices)
                    existing = templates.setdefault(wrapper, [])
                    if projected not in existing:
                        existing.append(projected)
                        changed = True
        if not changed:
            break

    for caller in functions:
        caller_parameters = tuple(
            (parameter.type_name, parameter.name, parameter.default)
            for parameter in caller.parameters)
        caller_aliases = collect_local_setting_aliases(caller.body)
        for callee, position, call_args in call_sites[caller]:
            summaries = templates.get(callee, ())
            substitutions = {
                parameter.name: (call_args[index] if index < len(call_args) else parameter.default)
                for index, parameter in enumerate(callee.parameters)
                if index < len(call_args) or parameter.default is not None
            }
            for summary in summaries:
                if summary.storage_parameter >= len(call_args):
                    continue
                storage = call_args[summary.storage_parameter]
                owner = ""
                path = None
                if caller.name == "DrawSettings" and caller.owner:
                    pseudo = [""] * (control_storage_argument_index(summary.control_kind) + 1)
                    pseudo[-1] = storage
                    path = extract_control_setting_path(
                        summary.control_kind, pseudo, caller_aliases)
                    owner = caller.owner
                if not path:
                    origin = find_parameter_origin(storage, caller_parameters)
                    if origin is not None:
                        owner = resolve_type_alias(
                            caller.parameters[origin[0]].type_name,
                            aliases_by_path[caller.source])
                        path = (*origin[1], *summary.storage_path)
                elif summary.storage_path:
                    path = (*path, *summary.storage_path)
                if not owner or not path:
                    continue
                resolved_args = tuple(
                    substitute_helper_parameters(argument, substitutions)
                    for argument in summary.control_args)
                label_expression = substitute_helper_parameters(
                    summary.label_expression, substitutions)
                category_text, category_key = _control_category(
                    caller.body, position, caller.prefix)
                category = (LocalizedText(category_text, category_key)
                            if category_text else summary.category)
                identity = owner, tuple(path)
                add_metadata(identity, make_binding(
                    caller, position, *identity, summary.control_kind,
                    resolved_args, label_expression, category, summary.choices), 1)
                add_choices(identity, summary.choices)

    for function in functions:
        if function.name != "DrawSettings" or not function.owner:
            continue
        constants = constants_by_path[function.source]
        for button in re.finditer(r"\bImGui::Button\s*\(", function.masked_body):
            close = find_matching_paren(function.body, button.end() - 1)
            args = split_args(function.body[button.end():close]) if close >= 0 else []
            following = function.body[close + 1:close + 300] if close >= 0 else ""
            assignment = re.search(
                r"(?:settings|debugSettings|[A-Za-z_]\w*Settings)\."
                r"([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*=\s*([^;]+);",
                following)
            label = _localized_text(args[0], function.prefix) if args else None
            value = resolve_numeric_expression(assignment.group(2), constants) if assignment else None
            if assignment and label and value is not None and value.is_integer():
                add_choices(
                    (function.owner, normalize_setting_path(assignment.group(1))),
                    ((int(value), label.text, label.key),))

    finalized_choices = finalize_ui_choices(choice_candidates)
    bindings = {
        identity: next(iter(candidates))
        for identity, (_, candidates) in metadata_candidates.items()
        if len(candidates) == 1
    }
    for identity, choices in finalized_choices.items():
        binding = bindings.get(identity)
        if binding:
            bindings[identity] = ControlBinding(
                binding.owner, binding.path, binding.label, binding.category,
                binding.control_kind, binding.minimum, binding.maximum,
                binding.display_scale, binding.numeric_transform, choices,
                binding.component_labels, binding.aggregate_all,
                binding.source_widget, binding.clamp_numeric_input,
                binding.hdr_color)
        else:
            bindings[identity] = ControlBinding(
                identity[0], identity[1], None, LocalizedText(), "Combo",
                choices=choices, source_widget="Combo")
    return bindings


def _collect_reversible_numeric_bindings(
        functions: tuple[SourceFunction, ...],
        constants_by_path: dict[Path, dict[str, float]]) -> dict[
            tuple[str, tuple[str, ...]], ControlBinding]:
    definitions: dict[str, list[SourceFunction]] = {}
    for function in functions:
        definitions.setdefault(function.name, []).append(function)

    summaries = {}
    inverse_pairs = {"log2": ("exp2", "Log2")}
    for name, candidates in definitions.items():
        if len(candidates) != 1:
            continue
        function = candidates[0]
        numeric_controls = []
        direct_controls = list(DIRECT_UI_CONTROL_RE.finditer(function.masked_body))
        if len(direct_controls) == 1:
            direct = direct_controls[0]
            if direct.group(1).startswith(("Slider", "Drag", "Input")):
                numeric_controls.append((direct, direct.group(1), 1))
        numeric_controls.extend(
            (control, f"ShiftSliderFloat{control.group(1)}", int(control.group(1)))
            for control in SHIFT_UNIFIED_CONTROL_RE.finditer(function.masked_body))

        function_summaries = set()
        for control, control_kind, component_count in numeric_controls:
            close = find_matching_paren(function.body, control.end() - 1)
            args = split_args(function.body[control.end():close]) if close >= 0 else []
            storage_index = control_storage_argument_index(control_kind)
            if len(args) <= storage_index:
                continue
            local = re.fullmatch(
                r"\s*&?\s*([A-Za-z_]\w*)(?:\s*\[\s*0\s*\])?\s*",
                args[storage_index])
            if not local:
                continue

            proven = []
            for parameter_index, parameter in enumerate(function.parameters):
                if PRIMITIVE_TYPES.get(parameter.type_name) not in {"Float", "Integer"}:
                    continue
                for forward, (inverse, transform) in inverse_pairs.items():
                    reads = re.findall(
                        rf"\b{re.escape(local.group(1))}\s*\[\s*([A-Za-z_]\w*)\s*\]\s*=\s*"
                        rf"{forward}\s*\(\s*{re.escape(parameter.name)}\s*\[\s*\1\s*\]\s*\)\s*;",
                        function.masked_body[:control.start()])
                    writes = re.findall(
                        rf"\b{re.escape(parameter.name)}\s*\[\s*([A-Za-z_]\w*)\s*\]\s*=\s*"
                        rf"{inverse}\s*\(\s*{re.escape(local.group(1))}\s*\[\s*\1\s*\]\s*\)\s*;",
                        function.masked_body[close + 1:])
                    if len(reads) == len(writes) == 1 and reads[0] == writes[0]:
                        proven.append((parameter_index, transform))
            if len(proven) != 1:
                continue
            label = _localized_text(args[0], function.prefix) if args else None
            if not label:
                continue
            flags_expression = resolve_control_flags_expression(
                function.body, control.start(), control_kind, args)
            numeric = get_control_numeric_metadata(
                control_kind, args, constants_by_path.get(function.source, {}),
                flags_expression)
            minimum, maximum, display_scale = numeric or (None, None, 1.0)
            if proven[0][1] == "Log2" and minimum is not None and maximum is not None:
                minimum, maximum = 2.0 ** minimum, 2.0 ** maximum
            function_summaries.add((
                component_count, proven[0][0], label, control_kind,
                minimum, maximum, display_scale, proven[0][1],
                control_source_widget(control_kind),
                control_clamps_numeric_input(control_kind, flags_expression),
                control_is_hdr_color(control_kind, flags_expression)))
        if function_summaries:
            summaries[name] = tuple(function_summaries)

    collected = {}
    conflicts = set()
    for function in functions:
        if function.name != "DrawSettings" or not function.owner:
            continue
        aliases = collect_local_setting_aliases(function.body)
        for helper_name, helper_summaries in summaries.items():
            for invocation in re.finditer(
                    rf"\b{re.escape(helper_name)}(?:\s*<\s*([234])\s*>)?\s*\(",
                    function.masked_body):
                close = find_matching_paren(function.body, invocation.end() - 1)
                args = split_args(function.body[invocation.end():close]) if close >= 0 else []
                requested_count = int(invocation.group(1)) if invocation.group(1) else None
                matching = [
                    summary for summary in helper_summaries
                    if requested_count is None or summary[0] == requested_count
                ]
                if requested_count is None and len(matching) > 1:
                    matching = [summary for summary in matching if summary[0] == 1]
                if len(matching) != 1:
                    continue
                (_, parameter_index, label, control_kind, minimum, maximum,
                 display_scale, transform, source_widget,
                 clamp_numeric_input, hdr_color) = matching[0]
                if parameter_index >= len(args):
                    continue
                path = extract_control_setting_path(
                    control_kind, ["", args[parameter_index]], aliases)
                if not path:
                    continue
                category, category_key = _control_category(
                    function.body, invocation.start(), function.prefix)
                identity = function.owner, path
                binding = ControlBinding(
                    function.owner, path, label,
                    LocalizedText(category, category_key), control_kind,
                    minimum, maximum, display_scale, transform,
                    source_widget=source_widget,
                    clamp_numeric_input=clamp_numeric_input,
                    hdr_color=hdr_color)
                previous = collected.get(identity)
                if identity in conflicts:
                    continue
                if previous is None:
                    collected[identity] = binding
                elif previous != binding:
                    collected.pop(identity)
                    conflicts.add(identity)
    return collected


def collect_control_index(
        paths: list[Path], provider_paths: list[Path] | None = None) -> ControlIndex:
    provider_paths = provider_paths or paths
    bindings = _project_standard_controls(paths, provider_paths)
    conflicts = set()

    def merge_unique(target, additions):
        """Merges bindings, dropping any identity two sources disagree on.

        Fail-closed: an ambiguous identity is poisoned for the rest of the merge rather than
        resolved arbitrarily, so it falls out of the catalog instead of shipping a binding
        that writes the wrong member.
        """
        for identity, value in additions.items():
            if identity in conflicts:
                continue
            previous = target.get(identity)
            if previous is None:
                target[identity] = value
            elif previous != value:
                target.pop(identity)
                conflicts.add(identity)

    (indirect_labels, indirect_components,
     aggregate_all) = collect_indirect_numeric_projections(paths)
    indirect_bindings = {}
    for identity, metadata in indirect_labels.items():
        kind = metadata[4]
        count = control_component_count(kind)
        base_path = (identity[1][:-1]
                     if identity[1] and identity[1][-1] in VECTOR_COMPONENT_NAMES
                     else identity[1])
        start = (VECTOR_COMPONENT_NAMES.index(identity[1][-1])
                 if identity[1] and identity[1][-1] in VECTOR_COMPONENT_NAMES else 0)
        component_labels = tuple(
            LocalizedText(*indirect_components.get(
                (identity[0], (*base_path, component)), ("", "")))
            for component in VECTOR_COMPONENT_NAMES[start:start + count])
        indirect_bindings[identity] = ControlBinding(
            identity[0], identity[1], LocalizedText(metadata[0], metadata[2]),
            LocalizedText(metadata[1], metadata[3]), kind,
            metadata[5], metadata[6], metadata[7],
            component_labels=component_labels,
            aggregate_all=bool(aggregate_all.get(identity)),
            source_widget=metadata[8],
            clamp_numeric_input=metadata[9],
            hdr_color=metadata[10])
    merge_unique(bindings, indirect_bindings)

    functions = collect_source_functions(paths)
    constants_by_path = {
        path: collect_numeric_constants(
            read_text(path) + (read_text(path.with_suffix(".h"))
                               if path.with_suffix(".h").exists() else ""))
        for path in paths
    }
    reversible = _collect_reversible_numeric_bindings(functions, constants_by_path)
    merge_unique(bindings, reversible)

    choices = {
        identity: binding.choices
        for identity, binding in bindings.items() if binding.choices
    }

    contexts = {}
    context_conflicts = set()
    for source in (
            collect_member_selector_contexts(paths, choices),
            collect_index_selector_contexts(paths)):
        for identity, value in source.items():
            localized = LocalizedText(*value)
            previous = contexts.get(identity)
            if identity in context_conflicts:
                continue
            if previous is None:
                contexts[identity] = localized
            elif previous != localized:
                contexts.pop(identity)
                context_conflicts.add(identity)

    return ControlIndex(
        bindings,
        collect_tab_selector_roots(paths),
        contexts,
        {
            identity: LocalizedText(*value)
            for identity, value in collect_labeled_helper_roots(paths).items()
        },
        conflicts)


def type_to_value_type(type_name: str) -> str | None:
    cleaned = clean_type(type_name)
    if cleaned.startswith("std::atomic<"):
        inner = cleaned[len("std::atomic<"):-1].strip()
        cleaned = inner
    if cleaned in PRIMITIVE_TYPES:
        return PRIMITIVE_TYPES[cleaned]
    if cleaned.endswith("::value_type"):
        return None
    if cleaned in {"RE::NiColor", "RE::NiPoint2", "RE::NiPoint3", "DirectX::XMFLOAT2", "DirectX::XMFLOAT3", "DirectX::XMFLOAT4"}:
        return None
    return None


def fixed_array_type(type_name: str, constants: dict[str, float] | None = None) -> tuple[str, int] | None:
    match = re.fullmatch(r"std::array\s*<\s*(.+)\s*>", clean_type(type_name))
    if not match:
        return None
    arguments = split_args(match.group(1))
    if len(arguments) != 2:
        return None
    count = resolve_numeric_expression(arguments[1], constants or {})
    if count is None or count <= 0 or not float(count).is_integer():
        return None
    return clean_type(arguments[0]), int(count)


def collect_enum_types(paths: list[Path]) -> set[str]:
    enum_types = set()
    for path in paths:
        enum_types.update(re.findall(r"\benum(?:\s+class)?\s+([A-Za-z_]\w*)", read_text(path)))
    return enum_types


def collect_class_numeric_constants(paths: list[Path]) -> dict[str, dict[str, float]]:
    class_constants: dict[str, dict[str, float]] = {}
    for path in paths:
        text = read_text(path)
        for match in re.finditer(r"\b(?:struct|class)\s+([A-Za-z_]\w*)[^;{]*\{", text):
            body_end = find_matching_brace(text, match.end() - 1)
            if body_end >= 0:
                class_constants.setdefault(match.group(1), {}).update(
                    collect_numeric_constants(text[match.end():body_end]))
    return class_constants


def nested_type_candidates(owner: str, field_type: str) -> list[str]:
    cleaned = clean_type(field_type)
    return [cleaned, f"{owner}::{cleaned}"]


def build_entries(source_dir: Path) -> list[dict[str, object]]:
    src_paths = list((source_dir / "src").rglob("*.cpp")) + list((source_dir / "src").rglob("*.h"))
    src_paths += [source_dir / "src" / "TruePBR.cpp", source_dir / "src" / "TruePBR.h"]
    src_paths = sorted({p for p in src_paths if p.exists()})

    macros = collect_nlohmann_macros(src_paths)
    struct_bodies = collect_struct_bodies(src_paths)
    struct_fields = {name: parse_struct_fields(body) for name, body in struct_bodies.items()}
    enum_types = collect_enum_types(src_paths)
    class_numeric_constants = collect_class_numeric_constants(
        [path for path in src_paths if path.suffix == ".h"])
    features = collect_features([p for p in src_paths if p.suffix == ".h"])
    settings_components = collect_settings_components(features, src_paths)
    component_persisted_controls = collect_component_persisted_controls(
        features, settings_components)
    settings_component_classes = {
        child[0]
        for children in settings_components.values()
        for child in children
    }
    feature_fields = collect_feature_struct_fields([p for p in src_paths if p.suffix == ".h"], features)
    feature_members = collect_feature_member_fields([p for p in src_paths if p.suffix == ".h"], features)
    component_fields = collect_feature_struct_fields(
        [p for p in src_paths if p.suffix == ".h"],
        {child_class: {} for child_class in settings_component_classes})
    component_members = collect_feature_member_fields(
        [p for p in src_paths if p.suffix == ".h"],
        {child_class: {} for child_class in settings_component_classes})
    catalog_class_fields = feature_fields | component_fields
    save_roots = collect_save_roots([p for p in src_paths if p.suffix == ".cpp"])
    direct_persisted_fields = collect_direct_persisted_fields(
        [p for p in src_paths if p.suffix == ".cpp"], feature_members)
    cpp_paths = [p for p in src_paths if p.suffix == ".cpp"]
    control_index = collect_control_index(cpp_paths, src_paths)

    entries: list[dict[str, object]] = []
    seen: dict[tuple[str, tuple[str, ...], str], tuple[object, ...]] = {}
    discovery_errors: list[str] = []

    def add_entry(context: CatalogContext, path: list[str], key: str, value_type: str, access: str,
                  *, serialized_path: list[str] | None = None,
                  serialized_key: str | None = None,
                  serialized_component: int = -1,
                  fallback_label: str | None = None,
                  display_member_path: list[str] | None = None,
                  metadata_owners: tuple[str, ...] = (),
                  metadata_suffix_owners: tuple[str, ...] = (),
                  label_metadata_override=None,
                  binding_override: ControlBinding | None = None,
                  component_label: str = "",
                  component_label_key: str = "",
                  aggregate_all: bool = False,
                  force_hidden: bool = False,
                  aggregate_semantic: str = "None",
                  aggregate_start: int = -1,
                  aggregate_count: int = 0):
        feature = features.get(context.feature_class)
        if not feature:
            return
        full_path = [*context.json_path_prefix, *path]
        full_serialized_path = [
            *context.json_path_prefix,
            *(serialized_path if serialized_path is not None else path),
        ]
        feature_short = feature["short"]
        identity = (feature_short, tuple(full_path), key)
        signature = (
            access, tuple(full_serialized_path),
            serialized_key if serialized_key is not None else key,
            serialized_component)
        if identity in seen:
            if seen[identity] != signature:
                discovery_errors.append(
                    f"conflicting catalog identity {feature_short}.{'/'.join(full_path)}.{key}")
            return
        seen[identity] = signature
        source_path = Path(feature["source"])
        try:
            include_path = source_path.relative_to(source_dir / "src").as_posix()
        except ValueError:
            include_path = source_path.as_posix()

        owners = metadata_owners or (context.field_class,)
        setting_address = tuple(path + [key])
        binding_match = control_index.match(
            owners, setting_address, metadata_suffix_owners)
        binding = binding_match.binding if binding_match else None
        if binding_override:
            binding = binding_override
            binding_match = BindingMatch(binding, 0)
        if label_metadata_override:
            (override_label, override_category, override_label_key,
             override_category_key, override_kind, override_minimum,
             override_maximum, override_scale) = label_metadata_override
            binding = ControlBinding(
                context.field_class, setting_address,
                LocalizedText(override_label, override_label_key),
                LocalizedText(override_category, override_category_key),
                override_kind, override_minimum, override_maximum,
                override_scale, source_widget=control_source_widget(override_kind))
            binding_match = BindingMatch(binding, 0)

        choices = binding.choices if binding else ()
        if binding and binding.label:
            label, label_key = binding.label.text, binding.label.key
        elif choices:
            label, label_key = "", ""
        else:
            label, label_key = fallback_label or prettify(key), ""
        ui_category = binding.category.text if binding else ""
        category_key = binding.category.key if binding else ""
        control_kind = binding.control_kind if binding else ""
        minimum = binding.minimum if binding else None
        maximum = binding.maximum if binding else None
        display_scale = binding.display_scale if binding else 1.0
        numeric_transform = binding.numeric_transform if binding else "Identity"
        editor_semantic = resolve_editor_semantic(
            binding, value_type, force_hidden)

        flags = ["SceneSettingsCatalog::SettingFlag::Persisted"]
        if value_type == "Float" and editor_semantic in {"Numeric", "Generic"}:
            flags.append("SceneSettingsCatalog::SettingFlag::Transitionable")
        if editor_semantic == "Toggle":
            flags.append("SceneSettingsCatalog::SettingFlag::BooleanControl")
        if editor_semantic == "None":
            flags.append("SceneSettingsCatalog::SettingFlag::Hidden")
        else:
            flags.append("SceneSettingsCatalog::SettingFlag::SceneControllable")

        display_path = [p for p in (display_member_path if display_member_path is not None else path)]
        display_path_keys = ["" for _ in display_path]
        heading_size = 0
        contextual = False
        for root_size in range(min(len(setting_address), len(display_path)), 0, -1):
            context_match = control_index.match_context(
                owners, setting_address[:root_size], metadata_suffix_owners)
            if context_match:
                context_label = context_match[0]
                display_path = [context_label.text, *display_path[root_size:]]
                display_path_keys = [context_label.key, *display_path_keys[root_size:]]
                contextual = True
                break
        if not contextual:
            for root_size in range(len(setting_address), 0, -1):
                heading = control_index.headings.get(
                    (context.field_class, setting_address[:root_size]))
                if heading:
                    display_path = [heading.text, *display_path[root_size:]]
                    display_path_keys = [heading.key, *display_path_keys[root_size:]]
                    heading_size = 1
                    break
        if ui_category:
            matching_index = next((
                index for index, part in enumerate(display_path)
                if normalize_display_text(prettify(part)) ==
                normalize_display_text(ui_category)
            ), None)
            if matching_index is not None:
                display_path[matching_index] = ui_category
                display_path_keys[matching_index] = category_key
            else:
                display_path.insert(heading_size, ui_category)
                display_path_keys.insert(heading_size, category_key)
        display_path = [*context.display_path_prefix, *display_path]
        display_path_keys = [
            *("" for _ in context.display_path_prefix), *display_path_keys]
        selector_path: tuple[str, ...] = ()
        selector_keys: tuple[str, ...] = ()
        for selector_root_size in range(len(setting_address), 0, -1):
            selector = control_index.selectors.get(
                (context.field_class, setting_address[:selector_root_size]))
            if selector:
                selector_path, selector_keys = selector
                break
        selector_path = (*context.selector_path_prefix, *selector_path)
        selector_keys = (*context.selector_key_prefix, *selector_keys)

        entries.append({
            "featureClass": context.feature_class,
            "feature": feature_short,
            "featureName": feature["name"],
            "include": include_path,
            "path": join_catalog_path(full_path),
            "key": key,
            "displayName": label,
            "displayNameKey": label_key,
            "displayPath": join_catalog_path(display_path),
            "displayPathKeys": join_catalog_path(key or "-" for key in display_path_keys),
            "selectorPath": join_catalog_path(selector_path),
            "selectorPathKeys": join_catalog_path(key or "-" for key in selector_keys),
            "serializedPath": join_catalog_path(full_serialized_path),
            "serializedKey": serialized_key if serialized_key is not None else key,
            "serializedComponent": serialized_component,
            "componentDisplayName": component_label,
            "componentDisplayNameKey": component_label_key,
            "aggregateAll": aggregate_all,
            "aggregateSemantic": aggregate_semantic,
            "aggregateStart": aggregate_start,
            "aggregateCount": aggregate_count,
            "type": value_type,
            "flags": " | ".join(flags),
            "editorSemantic": editor_semantic,
            "minimum": minimum if minimum is not None else 0.0,
            "maximum": maximum if maximum is not None else 0.0,
            "displayScale": display_scale,
            "numericTransform": numeric_transform,
            "hasNumericBounds": minimum is not None and maximum is not None,
            "sourceWidget": binding.source_widget if binding else "",
            "clampNumericInput": bool(
                binding and binding.clamp_numeric_input and
                minimum is not None and maximum is not None),
            "hdrColor": bool(binding and binding.hdr_color),
            "invertedDisplay": control_kind == "InvertedCheckbox",
            "choices": choices,
            "access": access,
            "componentClass": context.component_class,
            "componentType": context.component_type,
            "componentContainer": context.component_container,
        })

    def emit_type(
            context: CatalogContext,
            full_type: str,
            path: list[str],
            access: str,
            inherited_metadata_owners: tuple[str, ...] = ()):
        fields = macros.get(full_type)
        if not fields:
            return
        simple_type = full_type.split("::")[-1]
        type_owner = full_type.rsplit("::", 1)[0] if "::" in full_type else context.field_class
        type_owner_name = type_owner.split("::")[-1]
        metadata_owners = tuple(dict.fromkeys(
            (context.field_class, type_owner_name, simple_type)))
        # "Settings" is excluded because nearly every feature names its struct that: as a
        # lookup owner it would match members of an unrelated feature's settings.
        metadata_suffix_owners = tuple(
            owner for owner in dict.fromkeys((
                *inherited_metadata_owners, type_owner_name, simple_type))
            if owner not in {context.field_class, "Settings"})
        declared_fields = catalog_class_fields.get(context.field_class, {}).get(
            simple_type, struct_fields.get(simple_type, {}))
        for field in fields:
            field_type = declared_fields.get(field, "")
            if not field_type:
                discovery_errors.append(f"{full_type}.{field} has no discovered declaration")
                continue
            value_type = type_to_value_type(field_type)
            if not value_type and clean_type(field_type).split("::")[-1] in enum_types:
                value_type = "Integer"
            field_access = f"{access}.{field}"
            if value_type:
                add_entry(
                    context, path, field, value_type, field_access,
                    metadata_owners=metadata_owners,
                    metadata_suffix_owners=metadata_suffix_owners)
                continue
            vector_components = VECTOR_COMPONENTS.get(clean_type(field_type), ())
            vector_metadata = [
                resolve_vector_binding(
                    control_index, metadata_owners, tuple(path + [field]),
                    vector_components, component_index, metadata_suffix_owners)
                for component_index in range(len(vector_components))
            ]
            for component_index, component in enumerate(vector_components):
                binding_match, aggregate_start, aggregate_count, aggregate_semantic = (
                    vector_metadata[component_index])
                binding = binding_match.binding if binding_match else None
                grouped = aggregate_count > 1 and aggregate_semantic != "None"
                component_offset = component_index - aggregate_start
                component_display = (
                    binding.component_labels[component_offset]
                    if binding and 0 <= component_offset < len(binding.component_labels)
                    else LocalizedText())
                add_entry(
                    context, path + [field], component, "Float",
                    f"{field_access}.{component}",
                    serialized_path=path,
                    serialized_key=field,
                    serialized_component=component_index,
                    fallback_label=f"{prettify(field)} {component.upper()}",
                    display_member_path=path if grouped else path + [field],
                    metadata_owners=metadata_owners,
                    metadata_suffix_owners=metadata_suffix_owners,
                    binding_override=binding,
                    component_label=component_display.text,
                    component_label_key=component_display.key,
                    aggregate_all=bool(
                        binding and binding.aggregate_all and component_index == aggregate_start),
                    force_hidden=binding is None,
                    aggregate_semantic=aggregate_semantic,
                    aggregate_start=aggregate_start,
                    aggregate_count=aggregate_count)
            if vector_components:
                continue
            array_type = fixed_array_type(
                field_type, class_numeric_constants.get(context.field_class, {}))
            if array_type:
                element_type, element_count = array_type
                element_value_type = type_to_value_type(element_type)
                if not element_value_type and element_type.split("::")[-1] in enum_types:
                    element_value_type = "Integer"
                element_components = VECTOR_COMPONENTS.get(element_type, ())
                nested_element_type = next((
                    candidate for candidate in nested_type_candidates(type_owner, element_type)
                    if candidate in macros
                ), None)
                array_components = tuple(str(index) for index in range(element_count))
                array_metadata = [
                    resolve_vector_binding(
                        control_index, metadata_owners, tuple(path + [field]),
                        array_components, element_index, metadata_suffix_owners)
                    for element_index in range(element_count)
                ] if element_value_type else []
                for element_index in range(element_count):
                    element_access = f"{field_access}[{element_index}]"
                    if element_value_type:
                        binding_match, aggregate_start, aggregate_count, aggregate_semantic = (
                            array_metadata[element_index])
                        binding = binding_match.binding if binding_match else None
                        grouped = aggregate_count > 1 and aggregate_semantic != "None"
                        add_entry(
                            context, path + [field], str(element_index),
                            element_value_type, element_access,
                            serialized_path=path if grouped else path + [field],
                            serialized_key=field if grouped else str(element_index),
                            serialized_component=element_index if grouped else -1,
                            fallback_label=f"{prettify(field)} {element_index + 1}",
                            display_member_path=path if grouped else path + [field],
                            metadata_owners=metadata_owners,
                            metadata_suffix_owners=metadata_suffix_owners,
                            binding_override=binding,
                            force_hidden=binding is None,
                            aggregate_semantic=aggregate_semantic,
                            aggregate_start=aggregate_start,
                            aggregate_count=aggregate_count)
                    element_metadata = [
                        resolve_vector_binding(
                            control_index, metadata_owners,
                            tuple(path + [field, str(element_index)]),
                            element_components, component_index, metadata_suffix_owners)
                        for component_index in range(len(element_components))
                    ]
                    for component_index, component in enumerate(element_components):
                        binding_match, aggregate_start, aggregate_count, aggregate_semantic = (
                            element_metadata[component_index])
                        binding = binding_match.binding if binding_match else None
                        grouped = aggregate_count > 1 and aggregate_semantic != "None"
                        add_entry(
                            context, path + [field, str(element_index)], component,
                            "Float", f"{element_access}.{component}",
                            serialized_path=path + [field],
                            serialized_key=str(element_index),
                            serialized_component=component_index,
                            fallback_label=(
                                f"{prettify(field)} {element_index + 1} {component.upper()}"),
                            display_member_path=(
                                path + [field] if grouped else path + [field, str(element_index)]),
                            metadata_owners=metadata_owners,
                            metadata_suffix_owners=metadata_suffix_owners,
                            binding_override=binding,
                            force_hidden=binding is None,
                            aggregate_semantic=aggregate_semantic,
                            aggregate_start=aggregate_start,
                            aggregate_count=aggregate_count)
                    if nested_element_type:
                        emit_type(
                            context, nested_element_type,
                            path + [field, str(element_index)], element_access,
                            (*inherited_metadata_owners, simple_type))
                if element_value_type or element_components or nested_element_type:
                    continue
            emitted_nested = False
            for candidate in nested_type_candidates(type_owner, field_type):
                if candidate in macros:
                    emit_type(
                        context, candidate, path + [field], field_access,
                        (*inherited_metadata_owners, simple_type))
                    emitted_nested = True
                    break
            if not emitted_nested and field_type:
                continue

    for feature_class in sorted(features):
        members = feature_members.get(feature_class, {})
        root_member = save_roots.get(feature_class)
        if not root_member:
            for member_name, member_type in members.items():
                if clean_type(member_type).split("::")[-1] == "Settings":
                    root_member = member_name
                    break
        if not root_member:
            if f"{feature_class}::Settings" in macros:
                discovery_errors.append(
                    f"{feature_class} has persisted Settings but no discovered settings member")
            continue

        root_type = clean_type(members.get(root_member, ""))
        if not root_type:
            if f"{feature_class}::Settings" in macros:
                discovery_errors.append(
                    f"{feature_class}.{root_member} has no discovered settings type")
            continue
        root_full_type = f"{feature_class}::{root_type.split('::')[-1]}"
        emit_type(CatalogContext(feature_class, feature_class), root_full_type, [], root_member)

    for feature_class, children in settings_components.items():
        for (child_class, child_type, component_container,
             component_display_name, component_display_key) in children:
            component_context = CatalogContext(
                feature_class,
                child_class,
                json_path_prefix=(child_type,),
                display_path_prefix=(component_display_name,),
                selector_path_prefix=(component_display_name,),
                selector_key_prefix=(component_display_key,),
                component_class=child_class,
                component_type=child_type,
                component_container=component_container)
            for (container, member, serialized_key,
                 (label, label_key)) in component_persisted_controls.get(
                     feature_class, ()):
                if container == component_container:
                    add_entry(
                        component_context, [], serialized_key, "Boolean", member,
                        label_metadata_override=(
                            label, "", label_key, "", "Checkbox",
                            None, None, 1.0))
            members = component_members.get(child_class, {})
            root_member = next((
                member_name for member_name, member_type in members.items()
                if clean_type(member_type).split("::")[-1] == "Settings"
            ), None)
            if not root_member:
                if f"{child_class}::Settings" in macros:
                    discovery_errors.append(
                        f"{child_class} has persisted Settings but no discovered settings member")
                continue

            root_type = clean_type(members.get(root_member, ""))
            root_full_type = f"{child_class}::{root_type.split('::')[-1]}"
            emit_type(
                CatalogContext(
                    feature_class,
                    child_class,
                    json_path_prefix=(child_type, "settings"),
                    display_path_prefix=(component_display_name,),
                    selector_path_prefix=(component_display_name,),
                    selector_key_prefix=(component_display_key,),
                    component_class=child_class,
                    component_type=child_type,
                    component_container=component_container),
                root_full_type,
                [],
                root_member)

    for feature_class, persisted_fields in direct_persisted_fields.items():
        context = CatalogContext(feature_class, feature_class)
        for key, value_type, access in persisted_fields:
            add_entry(context, [], key, value_type, access)

    if discovery_errors:
        raise ValueError("scene settings catalog discovery failed: " + "; ".join(sorted(set(discovery_errors))))

    entries.sort(key=lambda e: (e["feature"], e["displayPath"], e["displayName"], e["path"], e["key"]))
    return entries


def write_catalog(entries: list[dict[str, object]], out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    header = out_dir / "SceneSettingsCatalog.generated.h"
    source = out_dir / "SceneSettingsCatalog.generated.cpp"
    adapters = out_dir / "FeatureSceneSettingsAdapters.generated.cpp"

    entries = sorted(
        entries,
        key=lambda entry: (entry["feature"], entry["path"], entry["key"]),
    )

    header.write_text("""#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

struct Feature;

namespace SceneSettingsCatalog
{
\tenum class ValueType : std::uint8_t
\t{
\t\tBoolean,
\t\tInteger,
\t\tFloat,
\t\tString,
\t};

\tenum class EditorSemantic : std::uint8_t
\t{
\t\tNone,
\t\tGeneric,
\t\tToggle,
\t\tNumeric,
\t\tChoice,
\t\tText,
\t};

\tenum class AggregateSemantic : std::uint8_t
\t{
\t\tNone,
\t\tNumeric,
\t\tColor,
\t};

\tenum class NumericTransform : std::uint8_t
\t{
\t\tIdentity,
\t\tLog2,
\t};

\tenum class SettingFlag : std::uint32_t
\t{
\t\tNone = 0,
\t\tPersisted = 1u << 0,
\t\tTransitionable = 1u << 1,
\t\tHidden = 1u << 2,
\t\tBooleanControl = 1u << 3,
\t\tSceneControllable = 1u << 4,
\t};

\tconstexpr SettingFlag operator|(SettingFlag lhs, SettingFlag rhs)
\t{
\t\treturn static_cast<SettingFlag>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
\t}

\tconstexpr bool HasFlag(SettingFlag flags, SettingFlag flag)
\t{
\t\treturn (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
\t}

\tstruct SettingMetadata
\t{
\t\tstd::string_view featureShortName;
\t\tstd::string_view featureDisplayName;
\t\tstd::string_view settingPath;
\t\tstd::string_view settingKey;
\t\tstd::string_view displayName;
\t\tstd::string_view displayNameKey;
\t\tstd::string_view displayPath;
\t\tstd::string_view displayPathKeys;
\t\tstd::string_view selectorPath;
\t\tstd::string_view selectorPathKeys;
\t\tstd::string_view serializedPath;
\t\tstd::string_view serializedKey;
\t\tstd::int8_t serializedComponent;
\t\tstd::string_view componentDisplayName;
\t\tstd::string_view componentDisplayNameKey;
\t\tbool aggregateAll;
\t\tAggregateSemantic aggregateSemantic;
\t\tstd::int8_t aggregateStart;
\t\tstd::uint8_t aggregateCount;
\t\tValueType valueType;
\t\tSettingFlag flags;
\t\tEditorSemantic editorSemantic;
\t\tstd::string_view sourceWidget;
\t\tdouble minimumValue;
\t\tdouble maximumValue;
\t\tdouble displayScale;
\t\tNumericTransform numericTransform;
\t\tbool hasNumericBounds;
\t\tbool clampNumericInput;
\t\tbool hdrColor;
\t\tbool invertedDisplay;
\t\tconst std::int64_t* choices;
\t\tstd::size_t choiceCount;
\t};

\tconstexpr bool IsSceneControllable(const SettingMetadata& setting)
\t{
\t\treturn HasFlag(setting.flags, SettingFlag::SceneControllable) &&
\t\t       !HasFlag(setting.flags, SettingFlag::Hidden);
\t}

\tstd::span<const SettingMetadata> GetSettings();
\t/// ImGui entry points SceneWidgetInterceptor must detour to cover every scene-controllable control.
\tstd::span<const std::string_view> GetRequiredInterceptorEntryPoints();
\tconst SettingMetadata* FindSetting(std::string_view featureShortName, std::string_view settingPath, std::string_view settingKey);
\tusing ControlResolver = const SettingMetadata* (*)(Feature*, const void*);
\tbool RegisterControlResolver(std::string_view featureShortName, ControlResolver resolver);
\tconst SettingMetadata* FindSettingForControl(Feature* feature, const void* valueAddress);
}
""", encoding="utf-8")
    rows = []
    choice_arrays = []
    for index, e in enumerate(entries):
        choices = e["choices"]
        choice_pointer = "nullptr"
        choice_count = 0
        if choices:
            choice_name = f"kSceneSettingChoices{index}"
            choice_rows = ",\n".join(f"\t\t{value}" for value, _, _ in choices)
            choice_arrays.append(
                f"\tstatic constexpr std::array<std::int64_t, {len(choices)}> {choice_name} = {{{{\n"
                f"{choice_rows}\n\t}}}};")
            choice_pointer = f"{choice_name}.data()"
            choice_count = len(choices)
        rows.append(
            f'\t\t{{ "{cpp_escape(e["feature"])}", "{cpp_escape(e["featureName"])}", '
            f'"{cpp_escape(e["path"])}", "{cpp_escape(e["key"])}", "{cpp_escape(e["displayName"])}", "{cpp_escape(e["displayNameKey"])}", '
            f'"{cpp_escape(e["displayPath"])}", "{cpp_escape(e.get("displayPathKeys", ""))}", '
            f'"{cpp_escape(e["selectorPath"])}", "{cpp_escape(e["selectorPathKeys"])}", '
            f'"{cpp_escape(e["serializedPath"])}", "{cpp_escape(e["serializedKey"])}", {e["serializedComponent"]}, '
            f'"{cpp_escape(e.get("componentDisplayName", ""))}", '
            f'"{cpp_escape(e.get("componentDisplayNameKey", ""))}", '
            f'{str(e.get("aggregateAll", False)).lower()}, '
            f'SceneSettingsCatalog::AggregateSemantic::{e["aggregateSemantic"]}, '
            f'{e["aggregateStart"]}, {e["aggregateCount"]}, '
            f'SceneSettingsCatalog::ValueType::{e["type"]}, {e["flags"]}, '
            f'SceneSettingsCatalog::EditorSemantic::{e["editorSemantic"]}, '
            f'"{cpp_escape(e.get("sourceWidget", ""))}", '
            f'{e["minimum"]!r}, {e["maximum"]!r}, {e["displayScale"]!r}, '
            f'SceneSettingsCatalog::NumericTransform::{e.get("numericTransform", "Identity")}, '
            f'{str(e["hasNumericBounds"]).lower()}, '
            f'{str(e.get("clampNumericInput", False)).lower()}, '
            f'{str(e.get("hdrColor", False)).lower()}, '
            f'{str(e["invertedDisplay"]).lower()}, '
            f'{choice_pointer}, {choice_count} }},'
        )
    joined_rows = "\n".join(rows)
    joined_choice_arrays = "\n".join(choice_arrays)
    includes = "\n".join(f'#include "{cpp_escape(include_path)}"' for include_path in sorted({e["include"] for e in entries}))
    feature_blocks = []
    for feature_short in sorted({e["feature"] for e in entries}):
        feature_entries = [e for e in entries if e["feature"] == feature_short]
        feature_class = feature_entries[0]["featureClass"]
        direct_checks = "\n".join(
            f'\t\tif (valueAddress == static_cast<const void*>(&typedFeature->{e["access"]}))\n'
            f'\t\t\treturn SceneSettingsCatalog::FindSetting("{cpp_escape(e["feature"])}", '
            f'"{cpp_escape(e["path"])}", "{cpp_escape(e["key"])}");'
            for e in feature_entries if not e["componentClass"])
        component_groups: dict[str, dict[tuple[str, str], list[dict[str, object]]]] = {}
        for entry in feature_entries:
            if entry["componentClass"]:
                container_group = component_groups.setdefault(entry["componentContainer"], {})
                container_group.setdefault(
                    (entry["componentClass"], entry["componentType"]), []).append(entry)
        component_checks = []
        for component_container, container_components in component_groups.items():
            type_checks = []
            for (component_class, component_type), child_entries in container_components.items():
                child_checks = "\n".join(
                    f'\t\t\t\tif (valueAddress == static_cast<const void*>(&component->{e["access"]}))\n'
                    f'\t\t\t\t\treturn SceneSettingsCatalog::FindSetting("{cpp_escape(e["feature"])}", '
                    f'"{cpp_escape(e["path"])}", "{cpp_escape(e["key"])}");'
                    for e in child_entries)
                type_checks.append(
                    f'\t\t\tif (candidate->GetType() == "{cpp_escape(component_type)}") {{\n'
                    f'\t\t\t\tauto* component = static_cast<{component_class}*>(candidate.get());\n'
                    f'{child_checks}\n\t\t\t}}')
            joined_type_checks = "\n".join(type_checks)
            component_checks.append(
                f'\t\tfor (const auto& candidate : typedFeature->{cpp_escape(component_container)}) {{\n'
                f'\t\t\tif (!candidate)\n\t\t\t\tcontinue;\n'
                f'{joined_type_checks}\n\t\t}}')
        checks = "\n".join(part for part in [direct_checks, *component_checks] if part)
        resolver_name = f"Resolve{feature_class}SceneSettingControl"
        feature_blocks.append(f'''\tconst SceneSettingsCatalog::SettingMetadata* {resolver_name}(
\t\tFeature* feature, const void* valueAddress)
\t{{
\t\tauto* typedFeature = static_cast<{feature_class}*>(feature);
{checks}
\t\treturn nullptr;
\t}}

\t[[maybe_unused]] const bool registered{feature_class}SceneSettings =
\t\tSceneSettingsCatalog::RegisterControlResolver(
\t\t\t"{cpp_escape(feature_short)}", {resolver_name});''')
    joined_feature_blocks = "\n".join(feature_blocks)
    entry_points = required_entry_points(entries)
    entry_point_rows = "\n".join(f'\t\t"{point}",' for point in entry_points)
    source.write_text(f"""#include "SceneSettingsCatalog.generated.h"

#include <algorithm>
#include <array>

namespace
{{
{joined_choice_arrays}
\tstatic constexpr std::array<SceneSettingsCatalog::SettingMetadata, {len(entries)}> kSceneSettings = {{{{
{joined_rows}
\t}}}};
\tstatic constexpr std::array<std::string_view, {len(entry_points)}> kRequiredInterceptorEntryPoints = {{{{
{entry_point_rows}
\t}}}};
}}

namespace SceneSettingsCatalog
{{
\tstd::span<const SettingMetadata> GetSettings()
\t{{
\t\treturn kSceneSettings;
\t}}

\tstd::span<const std::string_view> GetRequiredInterceptorEntryPoints()
\t{{
\t\treturn kRequiredInterceptorEntryPoints;
\t}}

\tconst SettingMetadata* FindSetting(std::string_view featureShortName, std::string_view settingPath, std::string_view settingKey)
\t{{
\t\tconst std::array identity{{ featureShortName, settingPath, settingKey }};
\t\tconst auto found = std::lower_bound(
\t\t\tkSceneSettings.begin(), kSceneSettings.end(), identity,
\t\t\t[](const SettingMetadata& setting, const auto& target) {{
\t\t\t\tif (setting.featureShortName != target[0])
\t\t\t\t\treturn setting.featureShortName < target[0];
\t\t\t\tif (setting.settingPath != target[1])
\t\t\t\t\treturn setting.settingPath < target[1];
\t\t\t\treturn setting.settingKey < target[2];
\t\t\t}});
\t\tif (found == kSceneSettings.end())
\t\t\treturn nullptr;

\t\tconst auto& setting = *found;
\t\tif (setting.featureShortName != featureShortName ||
\t\t\tsetting.settingPath != settingPath ||
\t\t\tsetting.settingKey != settingKey)
\t\t\treturn nullptr;
\t\treturn &setting;
\t}}

}}
""", encoding="utf-8")

    adapters.write_text(f"""#include "SceneSettingsCatalog.generated.h"

#include "Feature.h"
{includes}

#include <algorithm>
#include <utility>
#include <vector>

namespace
{{
\tusing ControlResolverEntry = std::pair<std::string_view, SceneSettingsCatalog::ControlResolver>;

\tstd::vector<ControlResolverEntry>& GetControlResolvers()
\t{{
\t\tstatic std::vector<ControlResolverEntry> resolvers;
\t\treturn resolvers;
\t}}

{joined_feature_blocks}
}}

namespace SceneSettingsCatalog
{{
\tbool RegisterControlResolver(std::string_view featureShortName, ControlResolver resolver)
\t{{
\t\tif (featureShortName.empty() || !resolver)
\t\t\treturn false;

\t\tauto& resolvers = GetControlResolvers();
\t\tif (std::ranges::any_of(resolvers,
\t\t\t\t[&](const auto& entry) {{ return entry.first == featureShortName; }}))
\t\t\treturn false;
\t\tresolvers.emplace_back(featureShortName, resolver);
\t\treturn true;
\t}}

\tconst SettingMetadata* FindSettingForControl(Feature* feature, const void* valueAddress)
\t{{
\t\tif (!feature || !valueAddress)
\t\t\treturn nullptr;

\t\tconst auto featureShortName = feature->GetShortName();
\t\tauto& resolvers = GetControlResolvers();
\t\tauto resolver = std::ranges::find_if(resolvers,
\t\t\t[&](const auto& entry) {{ return entry.first == featureShortName; }});
\t\treturn resolver != resolvers.end() ? resolver->second(feature, valueAddress) : nullptr;
\t}}
}}
""", encoding="utf-8")


# Logical editor kind -> the ImGui entry points that can produce it. `sourceWidget` names an
# editor kind, not an ImGui function: Combo also covers 3-arg RadioButton groups, and
# PercentageSlider is Util::PercentageSlider over SliderFloat.
SOURCE_WIDGET_ENTRY_POINTS = {
    "Checkbox": ("Checkbox",),
    "ColorEdit3": ("ColorEdit3",),
    "ColorEdit4": ("ColorEdit4",),
    "Combo": ("Combo", "RadioButton"),
    "PercentageSlider": ("SliderFloat",),
    "SliderAngle": ("SliderAngle",),
    "SliderFloat": ("SliderFloat",),
    "SliderFloat2": ("SliderFloat2",),
    "SliderInt": ("SliderInt",),
    "SliderScalar": ("SliderScalar",),
}


def required_entry_points(entries: list[dict[str, object]]) -> list[str]:
    """Sorted union of ImGui entry points the interceptor must cover for these entries."""
    points: set[str] = set()
    for entry in entries:
        if "SceneSettingsCatalog::SettingFlag::SceneControllable" not in entry.get("flags", ""):
            continue
        points.update(SOURCE_WIDGET_ENTRY_POINTS.get(entry.get("sourceWidget", ""), ()))
    return sorted(points)


def validate_entries(
        entries: list[dict[str, object]],
        min_entries: int,
        min_controllable: int = 1,
        min_controllable_features: int = 1) -> None:
    """Fails the build when the catalog shrinks past the floors, then checks each entry.

    The three floors are not redundant. Persisted-but-hidden entries keep the total up even
    if every scene binding is lost, and a healthy total hides one feature's bindings vanishing
    behind another's growth, so the feature count is the only signal for a single broken parse.
    """
    controllable = [
        entry for entry in entries
        if "SceneSettingsCatalog::SettingFlag::SceneControllable" in entry.get("flags", "")]
    controllable_features = {entry["feature"] for entry in controllable}
    for found, floor, label in (
            (len(entries), min_entries, "entries"),
            (len(controllable), min_controllable, "scene-controllable entries"),
            (len(controllable_features), min_controllable_features, "features with scene-controllable entries")):
        if found < floor:
            raise ValueError(
                f"scene settings catalog found {found} {label}, expected at least {floor}")

    errors = []
    identities = set()
    aggregate_members = {}
    for entry in entries:
        identity = (entry["feature"], entry["path"], entry["key"])
        if identity in identities:
            errors.append(f"duplicate setting {identity}")
        identities.add(identity)

        allowed = "SceneSettingsCatalog::SettingFlag::SceneControllable" in entry["flags"]
        hidden = "SceneSettingsCatalog::SettingFlag::Hidden" in entry["flags"]
        if allowed == hidden:
            errors.append(f"invalid visibility for {identity}")
        semantic = entry["editorSemantic"]
        if allowed == (semantic == "None"):
            errors.append(f"invalid editor for {identity}")
        if allowed:
            widget = entry.get("sourceWidget", "")
            if widget not in SOURCE_WIDGET_ENTRY_POINTS:
                errors.append(
                    f"scene-controllable {identity} uses widget '{widget}' with no interceptor "
                    f"entry point; add it to SOURCE_WIDGET_ENTRY_POINTS and to "
                    f"SceneWidgetInterceptor's detour table")
        transform = entry.get("numericTransform", "Identity")
        if transform not in {"Identity", "Log2"}:
            errors.append(f"invalid numeric transform for {identity}")
        if transform != "Identity" and semantic != "Numeric":
            errors.append(f"numeric transform without numeric editor for {identity}")
        if transform == "Log2" and entry["hasNumericBounds"] and entry["minimum"] <= 0.0:
            errors.append(f"invalid Log2 storage bounds for {identity}")
        if entry.get("clampNumericInput", False) and not entry["hasNumericBounds"]:
            errors.append(f"clamped numeric input without bounds for {identity}")
        if entry.get("hdrColor", False):
            if (entry.get("sourceWidget") not in {"ColorEdit3", "ColorEdit4"} or
                    entry["hasNumericBounds"] or entry.get("clampNumericInput", False)):
                errors.append(f"invalid HDR color metadata for {identity}")
        if (entry.get("sourceWidget") in {"ColorEdit3", "ColorEdit4"} and
                not entry.get("hdrColor", False) and
                (not entry["hasNumericBounds"] or entry["minimum"] != 0.0 or
                 entry["maximum"] != 1.0 or not entry.get("clampNumericInput", False))):
            errors.append(f"invalid standard color metadata for {identity}")
        if semantic == "Choice":
            values = [choice[0] for choice in entry["choices"]]
            if len(values) < 2 or len(values) != len(set(values)):
                errors.append(f"invalid choices for {identity}")

        count = int(entry.get("aggregateCount", 0))
        start = int(entry.get("aggregateStart", -1))
        component = int(entry.get("serializedComponent", -1))
        if count > 1:
            if start < 0 or not start <= component < start + count:
                errors.append(f"invalid aggregate range for {identity}")
            if allowed:
                group = (
                    entry["feature"], entry["serializedPath"], entry["serializedKey"],
                    entry["aggregateSemantic"], start, count)
                aggregate_members.setdefault(group, set()).add(component)

    for group, members in aggregate_members.items():
        expected = set(range(group[-2], group[-2] + group[-1]))
        if members != expected:
            errors.append(f"incomplete aggregate {group}")
    if errors:
        raise ValueError("scene settings catalog validation failed: " + "; ".join(errors[:10]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--min-entries", type=int, default=1)
    parser.add_argument("--min-controllable", type=int, default=1)
    parser.add_argument("--min-controllable-features", type=int, default=1)
    args = parser.parse_args()

    source_dir = Path(args.source_dir)
    out_dir = Path(args.out_dir)
    entries = build_entries(source_dir)
    try:
        validate_entries(
            entries, args.min_entries, args.min_controllable, args.min_controllable_features)
    except ValueError as error:
        parser.error(str(error))
    write_catalog(entries, out_dir)
    print(f"Generated {len(entries)} scene setting catalog entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
