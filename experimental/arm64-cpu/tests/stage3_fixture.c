// Stage 3: source of truth for compiler-generated x86-64 functions.
// All functions accept one unsigned integer in EDI and return EAX.
// Deliberately avoid external calls, globals and undefined signed overflow.
__attribute__((noinline)) unsigned fixture_triangular(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 1; i <= n; ++i) s += i;
    return s;
}
__attribute__((noinline)) unsigned fixture_bitmix(unsigned n) {
    return ((n << 2) | (n >> 3)) & 255u;
}
__attribute__((noinline)) unsigned fixture_popcount(unsigned n) {
    unsigned c = 0;
    while (n) { c += n & 1u; n >>= 1; }
    return c;
}
__attribute__((noinline)) unsigned fixture_factorial(unsigned n) {
    unsigned a = 1;
    for (unsigned i = 2; i <= n; ++i) a *= i;
    return a;
}
__attribute__((noinline)) unsigned fixture_fib(unsigned n) {
    unsigned a = 0, b = 1;
    while (n--) { unsigned t = a + b; a = b; b = t; }
    return a;
}

__attribute__((noinline)) unsigned fixture_gcd(unsigned n) {
    unsigned a = n, b = 84u;
    while (b != 0) {
        unsigned remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}
__attribute__((noinline)) unsigned fixture_digit_sum(unsigned n) {
    unsigned sum = 0;
    do {
        sum += n % 10u;
        n /= 10u;
    } while (n != 0);
    return sum;
}
__attribute__((noinline)) unsigned fixture_reverse_bits(unsigned n) {
    unsigned result = 0;
    for (unsigned i = 0; i < 32; ++i) {
        result = (result << 1) | (n & 1u);
        n >>= 1;
    }
    return result;
}
