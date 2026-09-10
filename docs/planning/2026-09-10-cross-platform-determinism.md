# Cross-platform determinism

**Decision (owner, 10 September 2026):** the engine's determinism claim is to be
cross-platform. `test_determinism`'s canonical constant must be one number on
Windows (the UCRT) and on Linux (glibc), not one number per C runtime.

Until now the claim was one C runtime. MSVC 2022, MSVC 14.50 and GCC 15.2 all
link the UCRT on Windows, and they agree on 8818694387102185031. GCC 13.3,
GCC 14.2 and clang 18.1 against glibc 2.39 agree on 7854318744396420989. The
constant was left failing on Linux until somebody decided which claim was meant.

## Step 1: measure before replacing anything

The canonical scene is `test_determinism`'s fixture: six boxes and a ball
falling onto a floor for 240 ticks. It exercises the integrator, broadphase,
SAT narrowphase and impulse solver.

**How it was measured:**
- **Linux:** a throwaway probe ran the scene with GNU ld's `--wrap` on every
  float and double libm entry point. No engine source was changed. It logged
  each call's function, input bits and output bits, plus the StateHash after
  each tick. The probe reproduced glibc's 7854318744396420989, so the trace is
  of the real run.
- **Windows:** each logged call was re-evaluated through `ucrtbase.dll` on the
  same input bits. Up to the first call whose output differs, both runtimes
  saw identical inputs. That call is therefore where the platforms part.

**Result:**

| call      | calls | outputs that differ | first tick |
|-----------|------:|--------------------:|-----------:|
| `atan2f`  | 2,118 | 335                 | 29         |
| `asinf`   | 1,059 | 50                  | 52         |
| `sinf`    | 9,996 | 36                  | 58         |
| `cosf`    | 9,996 | 68                  | 60         |
| `powf`    | 1,346 | 0                   | -          |

GCC fuses each `sinf`/`cosf` pair into one `sincosf`, which is why the two rows
count the same calls.

**The first divergence is `atan2f(bd002dd0, 3f7f9124)` during tick 29.** glibc
returns `bd005aa8` and the UCRT `bd005aa7`. It lies in
`TransformComponent::EulerFromRotation`, which turns each spun rotation matrix
back into the Euler angles the transform stores. Every difference in the table
is one ulp, and one ulp in one angle on tick 29 is the whole divergence.

The damping curve's `powf` agrees on every call. The test's own comment had
named it as a suspect alongside `asin`, and it is not one.

**The UCRT is the correct one.** On every sampled disagreement, the UCRT
returns the correctly rounded float and glibc is one ulp off. The divergence
is therefore not a Windows quirk that Linux should copy. glibc's float
functions are simply not correctly rounded, and nothing in the C standard
says they must be.

## Step 2: the simulation stops calling libm

**`src/core/DetMath.hpp`** provides `sin`, `cos`, `sincos`, `asin`, `atan2`,
`pow` and `angleAxis`. They are built only from operations IEEE 754 fixes on
every platform: + - × ÷ and sqrt, plus floor, fmod, frexp, ldexp and copysign,
which are exact. Each is evaluated in double and rounded to float once.
- `sin`/`cos` use a Cody–Waite reduction with fdlibm's split π/2, then Taylor
  kernels.
- `atan` inverts past 1, then halves the angle twice through an exactly
  rounded square root, then uses a short series.
- `asin` goes through `atan`.
- `pow` is `exp(e · ln b)`, with frexp and ldexp doing the exponent exactly.

A Python transcription of the same operations is IEEE double with no
contraction, so it computes what every conforming platform must. Over 100,000
random inputs across the five functions, every result was the correctly
rounded float. That makes DetMath agree with the UCRT almost everywhere the
UCRT is right, and with glibc wherever glibc is.

**Routed through it:** every transcendental on the simulation path.
- `TransformComponent::getModelMatrix` (sin/cos), the matrix physics collides
  and integrates with.
- `EulerFromRotation` (asin/atan2), where the tick-29 call was.
- `PhysicsSystem`: damping `pow`, and the spin and joint `angleAxis`.
- `TransformSystem`'s decomposition.
- The joint hinge angle.
- The built-in scripts' `sin`, since they run on the tick.

The renderer's UV transform and the debug line shapes keep libm: a picture
one ulp different on another machine is not a bug.

**Contraction is off for SupersonicCore,** and PUBLIC, so the inline header
math obeys it in every target that includes it: `-ffp-contract=off` on
GCC/Clang, `/fp:precise` on MSVC. An FMA rounds once where the source says
twice, and whether one is emitted depends on the target. GCC's GNU mode
contracts whenever the target has FMA, and clang contracts within a statement.

**`test_detmath`** pins:
- 61 golden rows, bit for bit, from the Python transcription. The first ten
  are the calls the platforms disagreed on, plus the damping factor.
- Agreement with the local libm to within 2 ulp, which catches a wrong
  quadrant or branch that the golden rows could share with the transcription.
- C's conventions for signed zeros, infinities and NaN.
- `angleAxis` against glm's.

## Step 3: one number

**MSVC 14.50 on the UCRT and GCC 13.3 on glibc 2.39 now both hash the
canonical scene to 881310125714727098.** `test_detmath` passes all 35 checks on
both, so both compilers reproduce every golden row bit for bit. The full
suites pass 72 of 72 on both once the constant is updated.

The Windows number did move, from 8818694387102185031. DetMath is correctly
rounded on every sampled input, and the UCRT is not, on every call this scene
makes. So the new value is neither old platform's value: it is the value of
the arithmetic itself.

`kCanonical` moved once, deliberately, and its comment now says why. The
constant is no longer something one platform happens to produce. From here,
a platform that disagrees is a finding about that platform.

**What the claim does not yet cover:**
- **Other architectures and libms.** ARM64, and macOS's libm, have not been
  run. DetMath's reliance on contraction being off is exactly what ARM64
  would test, since every ARM64 target has FMA.
- **Code outside the simulation path.** A game's own C++ that calls `std::sin`
  inside its tick inherits its C runtime's last bit. HUSK's sim avoids that by
  policy: it uses only + - * / sqrt, which is why it was bit-exact across both
  runtimes before any of this.
