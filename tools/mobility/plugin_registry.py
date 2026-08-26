#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import tempfile
from pathlib import Path


SCHEMA = "chitu.mobility-plugin.v1"


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def plugin_roots(root: Path) -> list[Path]:
    configured = os.environ.get("CHITU_MOBILITY_PLUGIN_ROOTS", "")
    roots = [root / "config" / "mobility" / "plugins", root / "run" / "plugins" / "mobility"]
    roots.extend(Path(value).expanduser() for value in configured.split(os.pathsep) if value)
    return roots


def load_plugins(root: Path) -> dict[str, dict]:
    plugins: dict[str, dict] = {}
    for plugin_root in plugin_roots(root):
        if not plugin_root.is_dir():
            continue
        for path in sorted(plugin_root.rglob("*.json")):
            manifest = json.loads(path.read_text(encoding="utf-8"))
            if manifest.get("schema_version") != SCHEMA:
                continue
            plugin_id = str(manifest.get("id", ""))
            if not plugin_id:
                raise ValueError(f"Plugin manifest has no id: {path}")
            manifest["manifest_path"] = str(path.resolve())
            plugins[plugin_id] = manifest
    return plugins


def resolve_parameters(root: Path, manifest: dict) -> Path:
    value = Path(str(manifest["parameters_file"]))
    if value.is_absolute():
        path = value.resolve()
    else:
        manifest_relative = Path(manifest["manifest_path"]).parent / value
        project_relative = root / value
        path = manifest_relative.resolve() if manifest_relative.is_file() else project_relative.resolve()
    if not path.is_file():
        raise FileNotFoundError(f"Mobility parameter file does not exist: {path}")
    return path


def activate(root: Path, manifest: dict, output: Path) -> None:
    parameters = resolve_parameters(root, manifest)
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copyfile(parameters, temporary)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    metadata = {
        "schema_version": "chitu.mobility-active.v1",
        "plugin_id": manifest["id"],
        "plugin_version": manifest["version"],
        "manifest_path": manifest["manifest_path"],
        "parameters_file": str(output),
        "accepts": manifest.get("accepts", []),
        "produces": manifest.get("produces", []),
    }
    output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Manage Chitu mobility plugin manifests")
    parser.add_argument("command", choices=("list", "show", "activate"))
    parser.add_argument("plugin_id", nargs="?")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    root = project_root()
    plugins = load_plugins(root)
    if arguments.command == "list":
        for plugin_id, manifest in sorted(plugins.items()):
            print(f"{plugin_id}\t{manifest.get('version', '')}\t{manifest['manifest_path']}")
        return 0
    if not arguments.plugin_id or arguments.plugin_id not in plugins:
        raise ValueError(f"Unknown mobility plugin: {arguments.plugin_id}")
    manifest = plugins[arguments.plugin_id]
    if arguments.command == "show":
        print(json.dumps(manifest, indent=2))
        return 0
    output = arguments.output or root / "run" / "config" / "mobility.yaml"
    activate(root, manifest, output)
    print(f"plugin_id={manifest['id']}")
    print(f"parameters_file={output.resolve()}")
    print("MOBILITY_PLUGIN_ACTIVATED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
