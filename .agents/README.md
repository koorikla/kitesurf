# Agent skills for KiteSurf

`skills/` holds 16 skills in the `SKILL.md` format, for AI coding agents working in this
repository. `AGENTS.md` at the repository root lists when to use each one.

## Provenance

### Written for this repository

`kitesurf-build-test`, `kitesurf-automation-tests`, `kitesurf-editor-python`,
`kitesurf-big-air-sim`, `unreal-water-queries`.

These describe this repository's scripts, tests and physics model. Keep them in step with
the code: when a script or convention changes, update the skill in the same change.

### Third party, unmodified

Copied without changes from
[gamedev-skills/awesome-gamedev-agent-skills](https://github.com/gamedev-skills/awesome-gamedev-agent-skills)
at commit `d4b0e35550c55ae70bdfcab4ef5a0e94610438a9` (27 September 2026), licensed under
Apache-2.0. The licence and notice files are in
`licenses/awesome-gamedev-agent-skills/`.

| Skill | Upstream path |
| --- | --- |
| `unreal-cpp-gameplay` | `skills/unreal/unreal-cpp-gameplay` |
| `unreal-enhanced-input` | `skills/unreal/unreal-enhanced-input` |
| `unreal-niagara` | `skills/unreal/unreal-niagara` |
| `unreal-packaging` | `skills/unreal/unreal-packaging` |
| `game-feel` | `skills/disciplines/game-feel` |
| `camera-systems` | `skills/disciplines/camera-systems` |
| `game-ui-ux` | `skills/disciplines/game-ui-ux` |
| `physics-tuning` | `skills/disciplines/physics-tuning` |
| `input-systems` | `skills/disciplines/input-systems` |
| `audio-design` | `skills/disciplines/audio-design` |
| `performance-optimization` | `skills/disciplines/performance-optimization` |

Notes on the third-party set:

- They contain Markdown only: no scripts and no network calls. Each file was read before
  being added.
- The seven engine-neutral skills use Godot and Unity code samples. Use them for the
  method; the Unreal implementation is this project's own.
- They mention related skills that were not copied (Godot, Unity, genre and publishing
  skills). Those references lead nowhere here.
- To update, copy the folder again from a newer upstream commit, reread it, and change the
  commit hash above. Do not edit these folders in place; if a change is needed, note it
  here, as Apache-2.0 requires modified files to be marked.

## Using the skills with Hermes Agent

Hermes discovers project skills in `<repo>/.agents/skills/` but loads them only for
repository roots the user has trusted:

```bash
hermes skills trust /path/to/this/checkout
```

Trust is recorded per directory and per profile, so a fresh task workspace is not trusted
automatically. Two ways to cover that:

- List one stable checkout's skills folder under `skills.external_dirs` in the Hermes
  configuration, which makes the skills available in every session.
- Rely on `AGENTS.md`, which Hermes loads from any checkout without a trust step and which
  tells the agent to read the relevant `SKILL.md` directly.

Other tools that follow the `.agents/skills/` convention pick the folder up as well.
