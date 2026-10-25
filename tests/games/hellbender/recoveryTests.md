# Hellbender recovery tests

These four opt-in suites run the production payload inside an owned x86 test process. They copy bounded native instruction spans from an exact retail executable into private memory at two load bases. They never launch or attach to a game and never change display settings or send keyboard input.

| Suite | Regression coverage | Controlled boundaries |
| --- | --- | --- |
| `fullscreenSetupTests` | Retained DirectDraw root, setup ownership, callback reentry, genuine failure cleanup and native caller publication order | Full fullscreen setup body, Windows callbacks and COM objects |
| `rendererRecoveryTests` | Child teardown and initialization, empty bank and lazy refill, bounded readiness recovery during child setup, completed frame and raw presentation, actual input pump, held/repeated input, key-up and next-presentation rearming, qualified shutdown menu presentation | Driver COM results, window queries, original input action, native mode-change body and nested close callback |
| `frameRecoveryTests` | Qualified terminal Unlock and EndScene failures, exact stack ancestry, execute-buffer debt, pre-lock restore ownership, movie cleanup and subsequent frame/presentation | Driver COM results, window/clock callbacks and fatal callback |
| `modeCompletionTests` | Hardware frame/Flip completion, software GDI completion, owner invalidation, deferred menu commands and cancellation | Frame, GDI, window and command callbacks |

The three input-mode cases in `rendererRecoveryTests` start from separate fixture states. They prove that hardware, software and windowed input paths can dispatch a transition. They do not simulate a continuous driver mode cycle. The same-presentation and next-presentation cases separately exercise scope lifetime and native key-up forwarding.

The shutdown cases execute the retail menu-publication tail and its actual virtual presentation call. A controlled bitmap-preparation callback sets the native quit state and clears the frame, matching the nested close boundary. The fixture stops before the enclosing native SEH epilogue. Rejected owner, caller, quit-state and live-frame cases use an original-presenter sentinel to prove that drawing is still reached.

The child-readiness cases inject surface loss from the actual native root query after old child resources have been released. They verify a single owned primary Restore, both surface checks, a shared deadline and completion through the native frame/presentation chain. Non-loss failures, failed or repeated loss, invalid owners and recursive readiness preserve failure. The primary Restore entry is also present in healthy fixtures because it is a required method on real DirectDraw surfaces.

Prepare the normal recipes first, then run `scripts/games/hellbender/runRecoveryTests.ps1` with the retail input for each selected edition:

```powershell
./scripts/games/hellbender/runRecoveryTests.ps1 `
  -Edition English `
  -EnglishInput "<English-retail-executable>"
```

Use `-FrenchInput` and `-GermanInput` when those editions are selected. The default selects all three editions. The runner requires Python, CMake 4 or later, and the x86 MSVC 14.44 toolchain used by the payload build.

The native-fixture generator checks the retail identity, each bounded span, relocations and mapped call sites. It writes generated headers and an input receipt beneath `out/test/hellbenderNative`. No retail byte arrays belong in the maintained test sources. Test builds and reports also remain under `out/test`.

The runner records source and input hashes and checks them again afterward. These tests establish control-flow and ownership behavior within the stated fixture boundaries. Live startup, visible rendering, cutscenes, monitor transitions and all three supported editions still require the manual regression checks.

The terminal-frame suite also covers a successful native pre-lock Restore followed by a later lost surface in the same frame. It executes both the normal begin-drawing and geometry-relock paths, preserves the real Lock result, and verifies that only the matching restore receipt authorizes bounded readiness recovery. Failed Lock and invalidated receipts remain rejected.

The frame recovery suite also runs the native movie cleanup drawing loop in all three editions. It retains the real black-quad converter, three execute/scene pairs and raw presentations. Cases cover healthy cleanup, a lost primary, an earlier native pre-Unlock restore followed by another loss, owner or method changes, foreign ancestry, a non-video phase and the shared deadline. COM/window/clock callbacks and the outer video operation remain fixture boundaries.
