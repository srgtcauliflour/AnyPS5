// Minimal real x86-64 compiler fixture. Do not hand-edit the machine-code bytes
// in GuestCpuTests.cpp; regenerate them from this source when changing the fixture.
// Build: clang -target x86_64-unknown-linux-gnu -O1 -fno-stack-protector
//        -fno-pic -c compiler_fixture.c -o compiler_fixture.o
// Inspect: objdump -d compiler_fixture.o
// Observed .text bytes (Clang x86-64): 8d 47 02 c3
__attribute__((noinline))
int guest_add_two(int value) {
    return value + 2;
}
