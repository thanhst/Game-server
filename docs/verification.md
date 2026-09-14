# Verification Record - 2026-09-14

The user builds this project manually. During this cleanup, the agent did not
run CMake configure, compile, CTest, the C++ executable, or a network client.

Completed source checks for this cleanup:

- Removed the compatibility source tree, related tests, importer scripts, sample
  compatibility content/config, and dedicated protocol/auth documents.
- Removed the compatibility CMake option, target, executable modes, and test
  target references.
- Removed compatibility metadata keys from the gameplay content schema and demo
  content.
- Kept the current C++ prototype: `GameDomain`, `GameProtocol`, `GameServer`,
  `content/demo.game`, `scripts/debug_client.py`, and the ServerEngine
  submodule.
- Planned verification commands were limited to source-level checks such as
  keyword scans, listed-source sanity, JSON/preset parsing, and `git diff --check`.

These checks do not prove runtime behavior, performance, or client
interoperability. Build and runtime validation remain part of the user's manual
workflow.
