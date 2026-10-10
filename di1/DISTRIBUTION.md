# Packaging and distribution

Build the x86 ASI with Visual Studio 2022 and CMake, then run:

```powershell
.\di1\package.ps1 -SourceRevision '<40-character source commit>'
```

The user archive is **LiteralCrab-DI1-<VERSION>-x86.zip**, with VERSION read from the root VERSION file. Its root contains exactly LiteralCrab.asi, LiteralCrab.ini and an unmodified copy of the root README.md. The packager checks the x86 PE32 DLL and verifies every archived file's hash and the exact entry list.

## Licensing material outside the user ZIP

The three-file ZIP alone lacks the complete GPL and the required MinHook/HDE redistribution notices. It must be accompanied by **LICENSE**, **NOTICE.txt** and clear access to the corresponding source. The packager writes these legal files and SOURCE.txt next to the ZIP. CI supplies a separate notices artifact; the release workflow supplies separate assets and the exact source link. Keep those materials available alongside the binary when redistributing it.

- [GPL-3.0 sections 4–6](https://www.gnu.org/licenses/gpl-3.0.html): licence copy, notices and corresponding source.
- [MinHook licence](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt): notices, conditions and disclaimer with binary distributions.

The source retains Lucas Lhomme's attribution, the original licence, and the vendored x86 MinHook/HDE copyright headers. The main README remains maintained by the repository owner.

## Release policy

CI builds main and dev. VERSION changes on main prepare a draft/pre-release with a v-prefixed tag; publication remains a maintainer action. A version already published under either its bare tag or its v-prefixed tag is skipped. Published releases, tags and assets are not updated by the workflow.

The x86 MinHook subset is vendored from this fork's a37579175c7e16572f062b7a391423f31584c811 revision. No runtime dependencies are downloaded during CMake configuration. The D3D9/D3D9Ex smoke test is kept for local use on an interactive graphics driver; policy and frame-pacing tests run in CI.
