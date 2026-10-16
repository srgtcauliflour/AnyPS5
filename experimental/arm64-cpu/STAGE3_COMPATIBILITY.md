# Stage 3: compiler-generated program compatibility

## Scope and limitations

This is an experimental **x86-64 instruction interpreter**, not a PS5 emulator.
The first Stage 3 baseline uses eight C functions, each accepting a single
unsigned 32-bit SysV AMD64 argument in EDI and returning EAX. The runner
prepends `mov edi, imm32` and executes the compiler's real function bytes.
No guest OS, loader, external symbols, guest heap, or general ABI is provided.

## Reproducible generation

On Linux x86-64 CI, `clang-17` compiles
`tests/stage3_fixture.c` with `-O1 -fno-stack-protector -fno-pic
-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-builtin
-fcf-protection=none -falign-functions=1 -falign-loops=1`.
The generator uses `readelf -r` to **reject relocations**, `nm -S` to
locate exact function sizes and `objcopy --only-section=.text -O binary`
to extract machine code. The generated header is built in the CMake build
directory; checked-in byte arrays are not treated as compiler output.

The native reference is separately compiled from the **same C source**.
Only Linux x86-64 runs the ELF extractor; Windows/macOS retain the existing
portable C++ tests, and iOS ARM64 remains compile-only.

## Published baseline corpus

| Function | Inputs | Cases |
| --- | --- | ---: |
| triangular sum | 0–128 | 129 |
| bitmix | 0–511 | 512 |
| popcount | 0–255 | 256 |
| factorial | 0–12 | 13 |
| Fibonacci | 0–32 | 33 |
| Euclidean GCD | 0–255 | 256 |
| decimal digit sum | 0–255 | 256 |
| 32-bit bit reversal | 0–255 | 256 |
| **Total** | | **1,711** |

The CTest executable prints per-function pass counts and a total
`STAGE3 COMPATIBILITY passed/1711` line. A mismatch or execution exception
causes CI failure; failures are not counted as passes. The denominator
represents **only this deliberately limited corpus**, not compatibility
with arbitrary x86-64 software or PS5 games.

## Known gaps and next work

- Real recursion and multi-function calls, external relocations and ABI stack
  arguments need Phase 4 loader/ABI work.
- Pointer and array inputs require controlled guest-memory initialization.
- Sorting and more complex integer algorithms remain outside the baseline.
- The generator requires Linux ELF and Clang 17; other compilers are not
  silently treated as equivalent.
- A green CTest confirms only the exact generated corpus and toolchain.

## Expansion checkpoint

The eight-function corpus exercises compiler-emitted integer division in
addition to arithmetic, branching, shifts and multiplication. The interpreter
now includes unsigned F7 /6 DIV (32/64-bit), with dedicated quotient/remainder
regression tests. Treat the expanded compatibility percentage as **pending**
until its own complete CI run succeeds.
