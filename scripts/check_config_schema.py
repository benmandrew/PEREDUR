#!/usr/bin/env python3
"""Check that the hand-maintained descriptions of the TOML config agree.

The config surface is spelled out in five places, and nothing in the compiler
ties them together:

  * ``k_config_keys`` in ``src/config/keys.hpp`` -- the keys the parser reads
    and recognises. ``config_json()`` in ``src/repair/manifest.cpp`` writes the
    effective config to ``run.json`` from the same table, which is what a
    campaign reads back; this checks that it still does. A key missing from
    ``run.json`` is silent: the run behaves as configured and records nothing
    about it. Two of the four ``[tlsf.mutation]`` keys were missing this way
    before the table existed.
  * ``schemas/config-schema.json`` -- what an editor validates against. It sets
    ``additionalProperties: false``, so a key missing here is flagged as an
    error in the editor even though the binary accepts it.
  * ``example-config.toml`` -- the annotated template users copy from.
  * ``DEFAULTS`` in ``scripts/gen_configs.py`` -- the harness's own Python
    mirror of the built-in defaults, which ``--pin-vintage`` writes verbatim
    into generated configs. A stale entry there is a wrong value baked into a
    campaign archive by the very mechanism that exists to record a moved
    default.

Drift between them is silent and has happened: ``nsga2-replicate`` was added to
the parser and the docs but not to the schema enum. This compares all four
key sets and exits non-zero on any disagreement.

The parser reads only the keys the table lists, so a key cannot be read
without this check seeing it.
"""

import argparse
import importlib.util
import json
import re
import sys
import tomllib
from pathlib import Path

# Keys whose accepted values are a closed set, checked against the schema's
# "enum". Names are unique across sections, so the section need not be given.
ENUM_KEYS = ("selection_scheme", "metric", "repair_mode", "status_grading",
             "mrs_admission_order")


# --- src/config/keys.hpp -----------------------------------------------------


def table_entries(source):
    """(section, key, member) per k_config_keys entry, in table order."""
    start = source.find("k_config_keys{{")
    if start < 0:
        raise ValueError("k_config_keys not found")
    body = source[start : source.index("}};", start)]
    entries = re.findall(r'\{"([\w.]+)",\s*"(\w+)",\s*&Config::(\w+),', body)
    if not entries:
        raise ValueError("k_config_keys has no entries")
    return entries


def parser_keys(source):
    """Dotted paths of every key and section the table declares."""
    paths = set()
    for section, key, _ in table_entries(source):
        parts = section.split(".")
        for depth in range(1, len(parts) + 1):
            paths.add(".".join(parts[:depth]))
        paths.add(f"{section}.{key}")
    return paths


def enum_spellings(source):
    """Spellings per enum type, from the EnumNames specialisations."""
    found = {}
    for match in re.finditer(r"struct EnumNames<(\w+)> \{(.*?)\n\};", source, re.S):
        found[match.group(1)] = set(re.findall(r'"([^"]+)"', match.group(2)))
    return found


def member_types(hpp_source):
    """Declared type per Config member."""
    body = hpp_source[hpp_source.index("struct Config {") :]
    return dict(
        (name, kind) for kind, name in re.findall(r"^\s*(\w+) (\w+) = ", body, re.M)
    )


def parser_enums(source, names_source, hpp_source):
    """Accepted string values per key: each entry's Config member, through its
    declared type, to that type's EnumNames spellings."""
    spellings = enum_spellings(names_source)
    types = member_types(hpp_source)
    return {
        key: spellings[types[member]]
        for _, key, member in table_entries(source)
        if types.get(member) in spellings
    }


# --- src/repair/manifest.cpp -------------------------------------------------


def manifest_paths(source, keys_source):
    """Dotted paths of the effective config `config_json()` writes to run.json.

    config_json() writes one field per k_config_keys entry, so its paths are
    the table's, provided it still walks the table rather than a literal.
    """
    start = source.find("nlohmann::json config_json(const Config& cfg)")
    if start < 0:
        raise ValueError("config_json() not found")
    end = source.find("\n}\n", start)
    if "k_config_keys" not in source[start:end]:
        raise ValueError("config_json() no longer iterates k_config_keys")
    return parser_keys(keys_source)


# --- schemas/config-schema.json ----------------------------------------------


def schema_keys(properties, prefix=""):
    paths = set()
    for name, node in properties.items():
        paths.add(prefix + name)
        if "properties" in node:
            paths |= schema_keys(node["properties"], prefix + name + ".")
    return paths


def schema_nodes(properties, prefix=""):
    nodes = {}
    for name, node in properties.items():
        nodes[prefix + name] = node
        if "properties" in node:
            nodes.update(schema_nodes(node["properties"], prefix + name + "."))
    return nodes


def open_objects(schema, prefix="(root)"):
    """Objects missing additionalProperties: false, which validate anything."""
    offenders = []
    if schema.get("type") == "object" and schema.get("additionalProperties") is not False:
        offenders.append(prefix)
    for name, node in schema.get("properties", {}).items():
        offenders += open_objects(node, f"{prefix}.{name}" if prefix != "(root)" else name)
    return offenders


# --- example-config.toml -----------------------------------------------------


def toml_keys(table, prefix=""):
    paths = set()
    for name, value in table.items():
        paths.add(prefix + name)
        if isinstance(value, dict):
            paths |= toml_keys(value, prefix + name + ".")
    return paths


def toml_scalars(table, prefix=""):
    """Leaf (non-table) keys and their values, by dotted path."""
    values = {}
    for name, value in table.items():
        if isinstance(value, dict):
            values.update(toml_scalars(value, prefix + name + "."))
        else:
            values[prefix + name] = value
    return values


def toml_values(table, prefix=""):
    values = {}
    for name, value in table.items():
        if isinstance(value, dict):
            values.update(toml_values(value, prefix + name + "."))
        elif isinstance(value, str):
            values[prefix + name] = value
    return values


# --- include/config.hpp ------------------------------------------------------

# TOML path -> the Config member holding its default. Keys absent here are not
# value-checked: genetic.selection_scheme, tlsf.repair_mode and
# model_counting.metric are enums already covered by the enum check above, and
# runtime.parallel defaults to hardware concurrency, which is not a constant to
# pin. Everything else must appear, so a new key cannot quietly skip the check.
DEFAULT_FIELDS = {
    "genetic.generations": "generations",
    "genetic.population_size": "population_size",
    "genetic.selection_rate": "selection_rate",
    "genetic.elitism_rate": "elitism_rate",
    "genetic.crossover_rate": "crossover_rate",
    "genetic.mutation_rate": "mutation_rate",
    "genetic.accumulate_repairs": "accumulate_repairs",
    "genetic.max_individuals": "max_individuals",
    "genetic.max_wall_s": "max_wall_s",
    "fitness.weight_syntactic": "fitness_weight_syntactic",
    "fitness.weight_semantic": "fitness_weight_semantic",
    "fitness.weight_status": "fitness_weight_status",
    "mutation.p_trigger": "p_trigger",
    "mutation.p_response": "p_response",
    "mutation.p_timing": "p_timing",
    "mutation.p_condition_type": "p_condition_type",
    "mutation.p_scope": "p_scope",
    "mutation.p_stop": "p_stop",
    "mutation.p_monotone": "p_monotone",
    "mutation.p_add_assumption": "p_add_assumption",
    "mutation.p_remove_guarantee": "p_remove_guarantee",
    "mutation.p_conditional_assumption": "p_conditional_assumption",
    "tlsf.muc_max_iterations": "muc_max_iterations",
    "tlsf.muc_screen_depth": "muc_screen_depth",
    "tlsf.mutation.p_assumption": "tlsf_p_assumption",
    "tlsf.mutation.p_temporal": "tlsf_p_temporal",
    "tlsf.mutation.p_clone_assumption": "tlsf_p_clone_assumption",
    "tlsf.mutation.max_assumption_width": "tlsf_max_assumption_width",
    "tlsf.mutation.p_bare_assumption": "tlsf_p_bare_assumption",
    "model_counting.default_bound": "default_model_counting_bound",
    "filters.run_implication": "run_implication_filter",
    "runtime.black_timeout_ms": "black_timeout",
    "runtime.ltlsynt_timeout_ms": "ltlsynt_timeout",
    "runtime.ltl2tgba_timeout_ms": "ltl2tgba_timeout",
    "runtime.ltlfilt_timeout_ms": "ltlfilt_timeout",
    "runtime.max_scoring_failure_rate": "max_scoring_failure_rate",
    "runtime.dashboard": "dashboard",
}

# Keys the parser accepts but that carry no pinnable constant default.
UNPINNED_KEYS = {
    "genetic.selection_scheme",
    "genetic.termination",
    "tlsf.repair_mode",
    "model_counting.metric",
    "fitness.status_grading",
    "fitness.mrs_admission_order",
    "runtime.parallel",
}


def hpp_defaults(source):
    """Default value per Config member, from its in-class initialiser.

    Handles both spellings the struct uses: ``T name = value;`` for the scalars
    and ``std::chrono::milliseconds name{value};`` for the tool timeouts. C++
    digit separators are stripped so 10'000 reads as 10000.

    Comment lines are dropped first. ``[^;]+`` spans newlines, so prose of the
    form ``p = 0.0002`` in a doc comment matches through to the next semicolon
    and eats the initialiser that follows it -- which is a member documenting
    its own measured default breaking the check on that default.
    """
    body = source[source.index("struct Config {") :]
    body = body[: body.index("\n};")]
    body = "\n".join(
        line for line in body.splitlines() if not line.lstrip().startswith("//")
    )
    found = {}
    for name, value in re.findall(r"(\w+)\s*=\s*([^;]+);", body):
        found[name] = value.strip()
    for name, value in re.findall(r"(\w+)\{([^}]*)\}\s*;", body):
        found.setdefault(name, value.strip())
    return {k: v.replace("'", "") for k, v in found.items()}


def as_comparable(value):
    """Normalise a C++ literal or a TOML value to compare the two."""
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value).strip()
    if text in ("true", "false"):
        return text == "true"
    try:
        return float(text)
    except ValueError:
        return text


# --- scripts/gen_configs.py --------------------------------------------------

# gen_configs.DEFAULTS key -> the Config member it mirrors. The harness's own
# spelling is not always the TOML path's last component (default_bound,
# p_assumption), so the table is written out rather than derived. A key in
# DEFAULTS that is in neither this table nor GEN_CONFIGS_EXEMPT fails the check:
# that is what stops the next drift, and it is also where a harness-only knob
# with no C++ counterpart has to be declared rather than silently skipped.
GEN_CONFIGS_FIELDS = {
    "generations": "generations",
    "population_size": "population_size",
    "crossover_rate": "crossover_rate",
    "mutation_rate": "mutation_rate",
    "elitism_rate": "elitism_rate",
    "p_trigger": "p_trigger",
    "p_response": "p_response",
    "p_timing": "p_timing",
    "p_condition_type": "p_condition_type",
    "p_scope": "p_scope",
    "p_stop": "p_stop",
    "p_add_assumption": "p_add_assumption",
    "p_remove_guarantee": "p_remove_guarantee",
    "default_bound": "default_model_counting_bound",
    "run_implication": "run_implication_filter",
    "accumulate_repairs": "accumulate_repairs",
    "black_timeout_ms": "black_timeout",
    "status_grading": "status_grading",
    "mrs_admission_order": "mrs_admission_order",
    "repair_mode": "repair_mode",
    "p_assumption": "tlsf_p_assumption",
    "p_temporal": "tlsf_p_temporal",
    "p_monotone": "p_monotone",
    "p_clone_assumption": "tlsf_p_clone_assumption",
    "max_assumption_width": "tlsf_max_assumption_width",
    "p_bare_assumption": "tlsf_p_bare_assumption",
    "max_wall_s": "max_wall_s",
    "termination": "termination",
    "max_individuals": "max_individuals",
}

# Entries that deliberately do not track config.hpp, each with the reason it
# must not be "corrected" into agreement. The first two are results-CSV key
# columns and the next three are emitted unconditionally, so following the
# binary would put every new row at odds with roughly 225k archived ones; the
# last four are sentinels make_toml reads as "emit nothing", not mirrors of a
# C++ value at all.
GEN_CONFIGS_EXEMPT = {
    "metric":
        "pinned 'direct': `metric` is a KEY_FIELDS column and flat configs are "
        "attributed to LEGACY_METRIC",
    "selection_scheme":
        "pinned 'nsga2-truncate': the scheme is the config directory name read "
        "back into the `selection` KEY_FIELDS column",
    "weight_syntactic":
        "pinned 0.33 and emitted unconditionally, so every archived config "
        "states it; moving it breaks comparability with every past grid",
    "weight_semantic": "pinned 0.33 and emitted unconditionally (see "
                       "weight_syntactic)",
    "weight_status": "pinned 0.33 and emitted unconditionally (see "
                     "weight_syntactic)",
    "ltlsynt_timeout_ms":
        "0 is make_toml's 'emit only when positive' sentinel, not a mirror of "
        "config.hpp's 10000 ms",
    "ltl2tgba_timeout_ms":
        "0 is make_toml's 'emit only when positive' sentinel, not a mirror of "
        "config.hpp's 60000 ms",
    "max_scoring_failure_rate":
        "0.0 is make_toml's 'emit only when positive' sentinel, not a mirror of "
        "config.hpp's 0.15",
    "dashboard":
        "False is make_toml's 'emit only when true' sentinel; it coincides with "
        "config.hpp rather than tracking it",
    "parallel":
        "0 is make_toml's 'emit only when positive' sentinel; config.hpp's "
        "default is available_parallelism(), a call rather than a literal, so "
        "there is nothing here to track",
}


def gen_configs_defaults(root):
    """Import gen_configs.py for its DEFAULTS table alone.

    Loaded by path rather than by name, so --root selects the checkout being
    checked. The module's top level is nothing but tables and function
    definitions -- main() sits behind the ``__main__`` guard -- so importing it
    runs no campaign logic and writes no files.
    """
    path = root / "scripts" / "gen_configs.py"
    spec = importlib.util.spec_from_file_location("_gen_configs_for_check", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"{path}: cannot be loaded as a module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return dict(module.DEFAULTS)


def as_enum(value):
    """Normalise an enum spelling so 'Nsga2Truncate' compares to 'nsga2-truncate'.

    The C++ initialiser reads back as ``Scope::Value`` while the harness spells
    the same value the way the TOML parser accepts it. Case and separators are
    the whole difference, so both sides collapse to lowercase alphanumerics.
    """
    text = str(value).rsplit("::", 1)[-1]
    return re.sub(r"[^a-z0-9]", "", text.lower())


# --- checks ------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: the parent of scripts/)",
    )
    root = ap.parse_args().root

    cpp = root / "src" / "config" / "keys.hpp"
    manifest_cpp = root / "src" / "repair" / "manifest.cpp"
    hpp = root / "include" / "config.hpp"
    names_hpp = root / "src" / "config" / "enum_names.hpp"
    schema_path = root / "schemas" / "config-schema.json"
    example_path = root / "example-config.toml"

    # A file that will not load is a failure of this check, not a crash of it:
    # report it the same way as a mismatch so the lint output stays readable.
    def load(path, loader):
        try:
            return loader(path.read_text(encoding="utf-8")), None
        except FileNotFoundError:
            return None, f"{path} does not exist."
        except (OSError, ValueError) as exc:
            return None, f"{path}: {exc}"

    cpp_source, cpp_error = load(cpp, lambda text: text)
    manifest_source, manifest_error = load(manifest_cpp, lambda text: text)
    hpp_source, hpp_error = load(hpp, lambda text: text)
    names_source, names_error = load(names_hpp, lambda text: text)
    schema, schema_error = load(schema_path, json.loads)
    example, example_error = load(example_path, tomllib.loads)

    from_parser, spec_error = (None, None)
    if cpp_source is not None:
        try:
            from_parser = parser_keys(cpp_source)
        except ValueError as exc:
            spec_error = f"{cpp}: could not read k_config_keys: {exc}"

    from_manifest, manifest_parse_error = (None, None)
    if manifest_source is not None and cpp_source is not None:
        try:
            from_manifest = manifest_paths(manifest_source, cpp_source)
        except ValueError as exc:
            manifest_parse_error = (
                f"{manifest_cpp}: could not read config_json(): {exc}")

    gen_defaults, gen_error = (None, None)
    try:
        gen_defaults = gen_configs_defaults(root)
    except (OSError, ValueError, AttributeError, ImportError) as exc:
        gen_error = f"{root / 'scripts' / 'gen_configs.py'}: {exc}"

    fatal = [
        e
        for e in (cpp_error, hpp_error, names_error, schema_error,
                  example_error, spec_error, manifest_error,
                  manifest_parse_error, gen_error)
        if e
    ]
    if fatal:
        print("Config key parity check could not run:\n", file=sys.stderr)
        for error in fatal:
            print(f"  - {error}", file=sys.stderr)
        return 1

    assert cpp_source is not None and schema is not None and example is not None
    assert from_parser is not None and hpp_source is not None
    assert names_source is not None
    assert from_manifest is not None
    assert gen_defaults is not None

    from_schema = schema_keys(schema["properties"])
    from_example = toml_keys(example)

    errors = []

    for path in sorted(from_parser - from_schema):
        errors.append(
            f"{schema_path.name}: missing '{path}', which the parser accepts. "
            f"Editors validating against the schema will reject it."
        )
    for path in sorted(from_schema - from_parser):
        errors.append(
            f"keys.hpp: k_config_keys is missing '{path}', which the "
            f"schema declares. The parser will warn 'unknown key' on it."
        )
    # run.json is what a campaign reads its own configuration back out of, so a
    # key the parser accepts but the manifest omits leaves a run with no record
    # of how it was configured. That is silent in a way the other three are not:
    # the schema shows up in an editor and the template in a diff, while this
    # shows up only as a field nobody can find afterwards.
    for path in sorted(from_parser - from_manifest):
        errors.append(
            f"manifest.cpp: config_json() omits '{path}', which the parser "
            f"accepts. It will not appear in run.json, so a campaign that "
            f"crosses it records its own arms nowhere."
        )
    for path in sorted(from_manifest - from_parser):
        errors.append(
            f"manifest.cpp: config_json() reports '{path}', which "
            f"k_config_keys does not declare. A manifest field with no key "
            f"behind it cannot be set and cannot be reproduced from."
        )

    for path in sorted(from_example - from_parser):
        errors.append(
            f"{example_path.name}: sets '{path}', which the parser does not "
            f"recognise and will warn about."
        )

    # example-config.toml is the template users copy, and it says every value in
    # it is the built-in default. That was silently false for eight keys, so the
    # claim is now enforced: each scalar key's value must equal the initialiser
    # of the Config member that backs it.
    defaults = hpp_defaults(hpp_source)
    for path, value in sorted(toml_scalars(example).items()):
        if path in UNPINNED_KEYS:
            continue
        field = DEFAULT_FIELDS.get(path)
        if field is None:
            errors.append(
                f"check_config_schema.py: '{path}' is in "
                f"{example_path.name} but not in DEFAULT_FIELDS, so its value "
                f"goes unchecked. Add it (or to UNPINNED_KEYS if it has no "
                f"constant default)."
            )
            continue
        if field not in defaults:
            errors.append(
                f"config.hpp: no initialiser found for '{field}', which backs "
                f"'{path}'."
            )
            continue
        want = as_comparable(defaults[field])
        got = as_comparable(value)
        if want != got:
            errors.append(
                f"{example_path.name}: '{path}' is {got!r}, but config.hpp "
                f"defaults {field} to {want!r}. The template must show the "
                f"built-in default."
            )

    # gen_configs.DEFAULTS is the fourth description, and the one whose drift is
    # least visible: a generated config states a key only where a sweep
    # overrides it, so a stale entry shows up neither in the config nor in the
    # run. --pin-vintage is the exception -- it writes these values out
    # verbatim, which is how a stale mirror reaches an archive through the
    # mechanism meant to protect it. Every entry must equal the config.hpp
    # initialiser behind it unless it is listed as a deliberate divergence.
    for key in sorted(gen_defaults):
        if key in GEN_CONFIGS_EXEMPT:
            continue
        field = GEN_CONFIGS_FIELDS.get(key)
        if field is None:
            errors.append(
                f"check_config_schema.py: gen_configs.DEFAULTS['{key}'] is in "
                f"neither GEN_CONFIGS_FIELDS nor GEN_CONFIGS_EXEMPT, so its "
                f"value goes unchecked. Map it to the Config member it mirrors, "
                f"or add it to GEN_CONFIGS_EXEMPT with the reason it must not "
                f"track config.hpp."
            )
            continue
        if field not in defaults:
            errors.append(
                f"config.hpp: no initialiser found for '{field}', which backs "
                f"gen_configs.DEFAULTS['{key}']."
            )
            continue
        raw = defaults[field]
        want, got = as_comparable(raw), as_comparable(gen_defaults[key])
        if isinstance(want, str) or isinstance(got, str):
            want, got = as_enum(raw), as_enum(gen_defaults[key])
        if want != got:
            errors.append(
                f"gen_configs.py: DEFAULTS['{key}'] is "
                f"{gen_defaults[key]!r}, but config.hpp defaults {field} to "
                f"{raw!r}. DEFAULTS mirrors the binary; correct it, or add "
                f"'{key}' to GEN_CONFIGS_EXEMPT with the reason it diverges."
            )

    # Keys named by GEN_CONFIGS_FIELDS or GEN_CONFIGS_EXEMPT that DEFAULTS no
    # longer holds: a stale exemption reads as a live one and would let a real
    # divergence in under an old key's name.
    for key in sorted((set(GEN_CONFIGS_FIELDS) | set(GEN_CONFIGS_EXEMPT))
                      - set(gen_defaults)):
        errors.append(
            f"check_config_schema.py: '{key}' is mapped or exempted here but is "
            f"not in gen_configs.DEFAULTS any more. Drop the stale entry."
        )

    # An open object defeats the point of the schema: it would validate any key.
    for offender in open_objects(schema):
        errors.append(
            f"{schema_path.name}: object '{offender}' does not set "
            f'"additionalProperties": false, so it accepts unknown keys.'
        )

    # Closed-value keys: the parser's accepted strings must match the enum.
    nodes = schema_nodes(schema["properties"])
    enums = parser_enums(cpp_source, names_source, hpp_source)
    for key in ENUM_KEYS:
        accepted = enums.get(key)
        if accepted is None:
            errors.append(f"keys.hpp: no enum-valued key '{key}' found.")
            continue
        matches = [(p, n) for p, n in nodes.items() if p.split(".")[-1] == key]
        if len(matches) != 1:
            errors.append(
                f"{schema_path.name}: expected exactly one '{key}' property, "
                f"found {len(matches)}."
            )
            continue
        path, node = matches[0]
        declared = set(node.get("enum", []))
        for value in sorted(accepted - declared):
            errors.append(
                f"{schema_path.name}: '{path}' enum is missing \"{value}\", "
                f"which the parser accepts."
            )
        for value in sorted(declared - accepted):
            errors.append(
                f"{schema_path.name}: '{path}' enum declares \"{value}\", "
                f"which the parser rejects."
            )
        default = node.get("default")
        if default is not None and default not in accepted:
            errors.append(
                f"{schema_path.name}: '{path}' default \"{default}\" is not a "
                f"value the parser accepts."
            )

    # The template must use values the parser accepts, not just valid keys.
    for path, value in sorted(toml_values(example).items()):
        key = path.split(".")[-1]
        if key in ENUM_KEYS and value not in enums.get(key, {value}):
            errors.append(
                f"{example_path.name}: '{path}' is \"{value}\", which the "
                f"parser rejects."
            )

    if errors:
        print("Config key parity check failed:\n", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        print(
            f"\n{len(errors)} problem(s). The parser, the manifest, the "
            f"schema, and the template must describe the same config.",
            file=sys.stderr,
        )
        return 1

    print(f"Config key parity: {len(from_parser)} keys agree across "
          f"keys.hpp, manifest.cpp, {schema_path.name}, and "
          f"{example_path.name}; "
          f"{len(GEN_CONFIGS_FIELDS)} gen_configs.DEFAULTS entries match "
          f"config.hpp ({len(GEN_CONFIGS_EXEMPT)} exempt).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
