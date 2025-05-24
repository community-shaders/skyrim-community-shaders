import os
import subprocess
import re
from py_markdown_table.markdown_table import markdown_table
from operator import itemgetter
import urllib.parse
from datetime import datetime


def create_link(text):
    return f"https://github.com/doodlum/skyrim-community-shaders/blob/dev/{text}"


# https://stackoverflow.com/questions/16673778/python-regex-match-in-multiline-but-still-want-to-get-the-line-number
def finditer_with_line_numbers(pattern, string, flags=0):
    """
    A version of 're.finditer' that returns '(match, line_number)' pairs.
    It handles line number adjustments based on '#line' directives in the source.
    """
    # handle pcpp info on skipped lines
    line_offsets = {}
    for line_number, line in enumerate(string.splitlines()):
        line_adjust = r"^#line (?P<line>[0-9]+) \"(?P<filename>.*)\""
        line_match = re.match(line_adjust, line)
        if line_match:
            offset = int(line_match.group("line")) - line_number - 1
            line_offsets[line_number] = offset

    matches = list(re.finditer(pattern, string, flags))
    if not matches:
        return []

    end = matches[-1].start()
    # -1 so a failed 'rfind' maps to the first line.
    newline_table = {-1: 0}
    for i, m in enumerate(re.finditer("\\n", string), 1):
        # Don't find newlines past our last match.
        offset = m.start()
        if offset > end:
            break
        newline_table[offset] = i

    # Failing to find the newline is OK, -1 maps to 0.
    for m in matches:
        newline_offset = string.rfind("\n", 0, m.start())
        newline_end = string.find("\n", m.end())  # '-1' gracefully uses the end.
        line = string[newline_offset + 1 : newline_end]
        line_number = newline_table[newline_offset]
        # search in offsets
        found_offset = 0
        for k, v in line_offsets.items():
            if k <= line_number:
                found_offset = v
            else:
                break
        yield (line_number + found_offset, m)


def capture_pattern(text, pattern):
    """Captures the pattern in the given text.

    Args:
      text: The text to search.
      pattern: The regular expression pattern to match.

    Returns:
      The line number of the first match, or None if no match is found.
    """

    # line adjust
    # line 288 "features/Grass Lighting/Shaders/RunGrass.hlsl"
    line_adjust = r"^#line (?P<line>[0-9]+) \"(?P<filename>.*)\""
    # Compile the regular expression pattern.
    regex = re.compile(pattern)
    results = []
    offset = 1
    # Iterate over the lines of the text.
    for line_number, line in enumerate(text.splitlines()):
        line_match = re.match(line_adjust, line)
        if line_match:
            offset = int(line_match.group("line")) - line_number - 1

        # Match the pattern against the line.
        match = regex.match(line)

        # If there is a match, return the line number.
        if match:
            results.append((line_number + offset, match))

    # If no match is found, return None.
    return results


def main():
    defines_list = [
        {"PSHADER": ""},
        {"PSHADER": "", "VR": ""},
        {"VSHADER": ""},
        {"VSHADER": "", "VR": ""},
    ]
    # https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-in-hlsl
    hlsl_types = {"t": "SRV", "u": "UAV", "s": "Sampler", "b": "CBV"}
    # Get the current directory path
    cwd = os.getcwd()
    pattern = re.compile(r"(?P<filename>\w+)\.(?P<extension>hlsli?)")
    feature_pattern = re.compile(r".*features/(?P<feature>[\w -]*)/.*")
    shader_pattern = re.compile(
        r"(?P<type>[\w<> ]+)\s+(?P<name>[\w]+)\s+:\s+register\(\s*(?P<buffer_type>[a-z]*)(?P<buffer_number>[0-9]+)\s*\)"
    )
    feature = ""
    filename = ""
    results = []
    result_map = {}  # used to prune duplicates

    # New dictionary to track register usage per (file, defines) compilation unit
    compilation_units = {}

    # Iterate over the files in the current directory and all of its subdirectories
    for root, dirs, files in os.walk(cwd):
        if "extern" in dirs:
            dirs.remove("extern")
        # Iterate over the files in the current directory
        for file in files:
            # Match the regex pattern against the filename
            match = pattern.match(file)

            # If there is a match, print the filename and extension
            if match:
                feature_match = feature_pattern.match(root)
                if "package" in root.lower():
                    feature = match.group("filename")
                elif feature_match:
                    feature = feature_match.group("feature")
                # print(root, feature, match.group("filename"), match.group("extension"))
                path = os.path.join(root, file)
                short_path = path[
                    path.lower().find("skyrim-community-shaders")
                    + len("skyrim-community-shaders") :
                ]
                for defines in defines_list:
                    arg_list = []
                    for define in defines.keys():
                        arg_list += ["-D", define]
                    try:
                        proc_result = subprocess.run(
                            [
                                "pcpp",
                                path,
                                "--passthru-unfound-includes",
                                "--passthru-defines",
                                # "--passthru-unknown-exprs",
                            ]
                            + arg_list,
                            stdout=subprocess.PIPE,
                        )
                    except Exception:
                        continue
                    if proc_result.stdout:
                        contents = proc_result.stdout.decode()
                        if contents:
                            capturelist = finditer_with_line_numbers(
                                shader_pattern,
                                contents,
                            )
                            for line_number, result in capturelist:
                                path_with_line_no = f"{short_path}:{line_number}"
                                entry_key = (path_with_line_no.lower(), frozenset(defines.keys()))
                                entry = result_map.get(path_with_line_no.lower())
                                if not entry:
                                    entry = {
                                        "Register": f'{result.group("buffer_type").lower()}{result.group("buffer_number")}',
                                        "Feature": feature,
                                        "Type": f'`{result.group("type")}`',
                                        "Name": result.group("name"),
                                        "File": f"[{path_with_line_no}]({create_link(f'{urllib.parse.quote(short_path)}#L{line_number}')})",
                                        "Register Type": hlsl_types.get(
                                            result.group("buffer_type").lower(),
                                            "Unknown",
                                        ),
                                        "Buffer Type": result.group("buffer_type"),
                                        "Number": int(result.group("buffer_number")),
                                        "PSHADER": False,
                                        "VSHADER": False,
                                        "VR": False,
                                    }
                                    for key in defines.keys():
                                        entry[key] = True
                                    result_map[path_with_line_no.lower()] = entry

                                # Track register usage per compilation unit
                                compilation_unit_key = (short_path.lower(), frozenset(defines.keys()))
                                if compilation_unit_key not in compilation_units:
                                    compilation_units[compilation_unit_key] = {}
                                reg = f'{result.group("buffer_type").lower()}{result.group("buffer_number")}'
                                if reg not in compilation_units[compilation_unit_key]:
                                    compilation_units[compilation_unit_key][reg] = set()
                                compilation_units[compilation_unit_key][reg].add(feature)

    results = [v for v in result_map.values()]
    # print(results)
    if results:
        results = sorted(results, key=itemgetter("Buffer Type", "Number", "File"))
        markdown = (
            markdown_table(results)
            .set_params(row_sep="markdown", quote=False)
            .get_markdown()
        )
        print(f"## Table generated on {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        print(markdown)

    # Detect conflicts per compilation unit
    conflicts = []
    for (compilation_path, defines_set), reg_map in compilation_units.items():
        for reg, features_set in reg_map.items():
            if len(features_set) > 1:
                conflicts.append((compilation_path, defines_set, reg, features_set))

    if conflicts:
        print("\n## Conflicts Detected\n")
        for compilation_path, defines_set, reg, features_set in conflicts:
            defines_str = ", ".join(sorted(defines_set)) if defines_set else "No defines"
            features_str = ", ".join(sorted(features_set))
            print(f"- Register `{reg}` in `{compilation_path}` with defines [{defines_str}] is used by multiple features: {features_str}") 


if __name__ == "__main__":
    main()
