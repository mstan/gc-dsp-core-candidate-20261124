# gc-dsp-core

Current licensing and the grant for existing owner contributions are documented in [LICENSE-SCOPE.md](LICENSE-SCOPE.md).

Shared GameCube/Wii DSP low-level interpreter and static AOT support, extracted
without source changes from `mstan/gcnllr` commit
`f07bfb9515082a964c36a4bbafb095dbb2d584b7`, directory
`third_party/lle/runtime/dsp_lle`.

Consumers add this directory with CMake and link `gcn_dsp_lle`. The existing C
API, DMA binding and build-selected AOT catalog remain unchanged. The initial
host binding covers GameCube MEM1 and ARAM; Wii-specific memory binding remains
consumer integration work and is not established by this extraction.

Imported Dolphin sources retain GPL-2.0-or-later notices; integration code is
GPL-3.0-or-later. See LICENSE and UPSTREAM.md. Firmware and generated microcode
catalogs are private consumer inputs and are never part of this package.

Consumers retain the exercised DSP tests and compare AOT behavior with the
functioning LLE reference before accepting new execution paths.

`native/CMakeLists.txt` provides the extracted `gcnllr_add_native_dsp` engine
contract from gcnllr c055532. HLE requires consumer-supplied pinned donor targets
and `GC_DSP_DONOR_HOST_DIR`; AOT requires a local generated catalog. The explicit
`interpreter-development` provider assists discovery and cannot qualify static
execution. These sources retain their imported licenses. Engine extraction does
not itself establish Wii DMA, audio or native execution compatibility.

Private candidate default: native/HLE services. Root CMake requires the declared pinned native dependency targets; it never silently selects the reference provider. Existing accuracy sources remain behind the explicit `lle` build selection. See the paired qualification attachment for a real standalone component build.
