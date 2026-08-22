"""Command line entry point.

    python -m lime_automation list                        # scripts a project ships
    python -m lime_automation run smoke                   # launch an engine and run one script
    python -m lime_automation run smoke --attach          # run against an already running engine
    python -m lime_automation run smoke rotation --backend vulkan
    python -m lime_automation info                        # describe running engines
    python -m lime_automation shell                       # interactive client
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .client import LimeClient
from .discovery import discover_engines, find_engine
from .errors import LimeAutomationError
from .paths import DEFAULT_CONFIG, DEFAULT_PRESET, DEFAULT_PROJECT
from .runner import ScriptResult, discover_scripts, run_scripts, script_directory
from .session import LimeSession


def _add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--project", default=DEFAULT_PROJECT, help="Project to work with")
    parser.add_argument("--config", default=DEFAULT_CONFIG, choices=["Debug", "Release", "RelWithDebInfo"])
    parser.add_argument("--preset", default=DEFAULT_PRESET, help="CMake preset whose Build directory to use")


def _add_launch_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--backend", choices=["d3d12", "vulkan"], help="RHI backend to launch with")
    parser.add_argument("--width", type=int, help="Window width")
    parser.add_argument("--height", type=int, help="Window height")
    parser.add_argument("--no-editor", action="store_true", help="Run without the editor UI")
    parser.add_argument("--keep-layout", action="store_true", help="Keep the saved dock layout")
    parser.add_argument(
        "--attach",
        action="store_true",
        help="Use a running engine instead of launching one, which keeps the window open afterwards",
    )


def command_list(arguments: argparse.Namespace) -> int:
    scripts = discover_scripts(project=arguments.project)
    directory = script_directory(project=arguments.project)

    if not scripts:
        print(f"No automation scripts in {directory}")
        print(f"Create {directory / 'smoke.py'} with a 'def run(engine):' function to add one.")
        return 0

    print(f"{len(scripts)} script(s) in {directory}:\n")
    width = max(len(script.name) for script in scripts)
    for script in scripts:
        print(f"  {script.name:<{width}}  {script.description or '(no description)'}")
    return 0


def command_info(arguments: argparse.Namespace) -> int:
    engines = discover_engines(config=arguments.config, preset=arguments.preset)
    if not engines:
        print("No running engine found.")
        return 1

    for engine in engines:
        client = engine.connect()
        print(f"{engine.url}  pid {engine.pid}  project {engine.project}")
        try:
            info = client.engine_info()
            stats = client.engine_stats()
            print(f"  backend    {info.get('backend')} on {info.get('adapter')}")
            print(f"  window     {info.get('backBufferWidth')}x{info.get('backBufferHeight')}")
            print(f"  editor     {'on' if info.get('editorEnabled') else 'off'}")
            print(f"  frame      {stats.get('frame')} at {stats.get('fps', 0.0):.1f} fps")
            print(f"  commands   {len(client.commands())}")
        except LimeAutomationError as error:
            print(f"  unreachable: {error}")
    return 0


def _report(results: list[ScriptResult]) -> int:
    print("\n=== summary ===")
    failed = [result for result in results if not result.ok]

    for result in results:
        status = "PASS" if result.ok else "FAIL"
        print(f"  [{status}] {result.name}")
        for artifact in result.artifacts:
            print(f"         -> {artifact}")
        if not result.ok:
            print(f"         {result.error}")

    if failed:
        print(f"\n{len(failed)} of {len(results)} script(s) failed.")
        # The traceback is printed once, at the end, so the per script lines stay readable.
        for result in failed:
            if result.traceback_text:
                print(f"\n--- {result.name} ---")
                print(result.traceback_text.rstrip())
        return 1

    print(f"\nAll {len(results)} script(s) passed.")
    return 0


def command_run(arguments: argparse.Namespace) -> int:
    names = arguments.scripts
    if not names:
        names = [script.name for script in discover_scripts(project=arguments.project)]
        if not names:
            print(f"No automation scripts in {script_directory(project=arguments.project)}")
            return 1
        print(f"Running all {len(names)} script(s): {', '.join(names)}")

    if arguments.attach:
        endpoint = find_engine(project=arguments.project, config=arguments.config, preset=arguments.preset)
        print(f"Attached to {endpoint.url} (pid {endpoint.pid})")
        results = run_scripts(names, endpoint.connect(), project=arguments.project)
        return _report(results)

    with LimeSession(
        project=arguments.project,
        config=arguments.config,
        preset=arguments.preset,
        backend=arguments.backend,
        width=arguments.width,
        height=arguments.height,
        no_editor=arguments.no_editor,
        keep_layout=arguments.keep_layout,
    ) as engine:
        info = engine.engine_info()
        print(f"Launched {arguments.project} on {info.get('backend')} ({info.get('adapter')})")
        results = run_scripts(names, engine, project=arguments.project)

        # Engine side errors are a failure even when every script passed, since they mean the run was
        # not clean.
        errors = engine.errors()
        for entry in errors:
            print(f"  engine error: {entry['category']}: {entry['message']}")

        exit_code = _report(results)
        return 1 if errors else exit_code


def command_shell(arguments: argparse.Namespace) -> int:
    """Opens a Python REPL with a connected engine, for exploring interactively."""
    import code

    if arguments.attach:
        endpoint = find_engine(project=arguments.project, config=arguments.config, preset=arguments.preset)
        engine = endpoint.connect()
        print(f"Attached to {endpoint.url}")
        _interact(code, engine)
        return 0

    with LimeSession(
        project=arguments.project,
        config=arguments.config,
        preset=arguments.preset,
        backend=arguments.backend,
        width=arguments.width,
        height=arguments.height,
        no_editor=arguments.no_editor,
    ) as engine:
        _interact(code, engine)
    return 0


def _interact(code_module, engine: LimeClient) -> None:
    banner = (
        "LimeEngine automation shell\n"
        "  engine   the connected client\n"
        "  Try: engine.engine_info(), engine.list_passes(), engine.save_screenshot('shot.png')\n"
    )
    code_module.interact(banner=banner, local={"engine": engine, "Path": Path}, exitmsg="")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="lime_automation",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List a project's automation scripts")
    _add_common_arguments(list_parser)
    list_parser.set_defaults(handler=command_list)

    info_parser = subparsers.add_parser("info", help="Describe running engines")
    _add_common_arguments(info_parser)
    info_parser.set_defaults(handler=command_info)

    run_parser = subparsers.add_parser("run", help="Run one or more scripts; all of them when none is named")
    run_parser.add_argument("scripts", nargs="*", help="Script names, without the .py extension")
    _add_common_arguments(run_parser)
    _add_launch_arguments(run_parser)
    run_parser.set_defaults(handler=command_run)

    shell_parser = subparsers.add_parser("shell", help="Interactive client against a running or new engine")
    _add_common_arguments(shell_parser)
    _add_launch_arguments(shell_parser)
    shell_parser.set_defaults(handler=command_shell)

    arguments = parser.parse_args(argv)

    try:
        return arguments.handler(arguments)
    except LimeAutomationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
