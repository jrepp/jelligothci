"""Style-guide lint verdicts for slice sprites: limits and waivers, shared by the review page and Jelli Art.

compare_slice.measure() finds specks, open edges and colours. This module turns those
metrics into a verdict per sprite using assets/slice/source/lint.json:

    {"max_colours": 6, "max_colours_by_kind": {"backgrounds": null},
     "waivers": {"<asset key>": {"rules": ["specks"], "reason": "..."}}}

A rule that fails and is waived is reported as waived, with its reason, instead of
failing. tools/jelli-art/lint.js applies the same rules in the page; test_server.py
checks that both agree on every asset. See docs/pixel-art-guide.md section 10.
"""
import json
from pathlib import Path

RULES = ("specks", "open_edges", "colours")
MAX_REASON = 200
RELATIVE = "source/lint.json"


class LintError(ValueError):
    """lint.json is malformed or a waiver edit is invalid."""


def check(config, keys=None, kinds=None):
    """Validate a lint document; keys and kinds, when given, are the asset keys and kinds it may name."""
    if not isinstance(config, dict):
        raise LintError("lint.json must be an object")
    unknown = set(config) - {"max_colours", "max_colours_by_kind", "waivers"}
    if unknown:
        raise LintError(f"lint.json has unknown fields: {', '.join(sorted(unknown))}")
    limit = config.get("max_colours")
    if limit is not None and (not isinstance(limit, int) or isinstance(limit, bool) or limit < 1):
        raise LintError("max_colours must be a positive integer or null")
    kinds_map = config.get("max_colours_by_kind", {})
    if not isinstance(kinds_map, dict) or any(v is not None and (not isinstance(v, int) or isinstance(v, bool) or v < 1)
                                              for v in kinds_map.values()):
        raise LintError("max_colours_by_kind maps a kind to a positive integer or null")
    unknown_kinds = set(kinds_map) - set(kinds) if kinds is not None else set()
    if unknown_kinds:
        raise LintError(f"max_colours_by_kind names unknown kinds: {', '.join(sorted(unknown_kinds))}")
    waivers = config.get("waivers", {})
    if not isinstance(waivers, dict):
        raise LintError("waivers must map an asset key to {rules, reason}")
    for key, waiver in waivers.items():
        check_waiver(key, waiver, keys)
    return config


def check_waiver(key, waiver, keys=None):
    if keys is not None and key not in keys:
        raise LintError(f"Waiver for unknown asset: {key}")
    if not isinstance(waiver, dict) or set(waiver) != {"rules", "reason"}:
        raise LintError(f"Waiver for {key} must be {{rules, reason}}")
    rules, reason = waiver["rules"], waiver["reason"]
    if not isinstance(rules, list) or not rules or any(r not in RULES for r in rules) or len(set(rules)) != len(rules):
        raise LintError(f"Waiver for {key} must list one or more of: {', '.join(RULES)}")
    if not isinstance(reason, str) or not reason.strip() or len(reason) > MAX_REASON:
        raise LintError(f"Waiver for {key} needs a reason of 1 to {MAX_REASON} characters")


def load(assets_dir, fallback_dir=None, kinds=None):
    """(config, error): the lint document beside the assets, or the fallback's; an error keeps the page usable."""
    for folder in (assets_dir, fallback_dir):
        path = Path(folder) / RELATIVE if folder else None
        if path and path.exists():
            try:
                return check(json.loads(path.read_text()), kinds=kinds), None
            except (OSError, ValueError) as error:  # LintError is a ValueError; a directory or unreadable file is OSError
                return {"waivers": {}}, f"{path.name}: {error}"
    return {"waivers": {}}, None


def colour_limit(config, kind):
    kinds = config.get("max_colours_by_kind", {})
    return kinds[kind] if kind in kinds else config.get("max_colours")


def verdict(key, kind, metrics, config):
    """{"fails": [rule], "waived": {rule: reason}, "colours": n, "max_colours": limit or None}."""
    limit, colours = colour_limit(config, kind), len(metrics["colors"])
    failing = [rule for rule, bad in (("specks", metrics["specks"]), ("open_edges", metrics["open_edges"]),
                                      ("colours", limit is not None and colours > limit)) if bad]
    waiver = config.get("waivers", {}).get(key)
    waived = {rule: waiver["reason"] for rule in failing if waiver and rule in waiver["rules"]}
    return {"fails": [rule for rule in failing if rule not in waived], "waived": waived, "colours": colours,
            "max_colours": limit}


def set_waiver(config, key, rules, reason, keys):
    """A copy of config with key's waiver replaced (or removed when rules is empty)."""
    waivers = dict(config.get("waivers", {}))
    if rules:
        waiver = {"rules": [r for r in RULES if r in rules], "reason": reason.strip() if isinstance(reason, str) else reason}
        if len(waiver["rules"]) != len(rules):
            raise LintError(f"Rules must be distinct and among: {', '.join(RULES)}")
        check_waiver(key, waiver, keys)
        waivers[key] = waiver
    elif key in waivers:
        del waivers[key]
    elif keys is not None and key not in keys:
        raise LintError(f"Unknown asset: {key}")
    return {**config, "waivers": dict(sorted(waivers.items()))}
