#pragma once

#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>

namespace yue2_abcedit {

// Exact rational arithmetic for quarter-note timing. No floats in conversion path.
class Fraction {
public:
    long long n = 0;  // numerator
    long long d = 1;  // denominator, always > 0

    Fraction() = default;
    Fraction(long long num, long long den) {
        if (den == 0) throw std::invalid_argument("zero denominator");
        if (den < 0) { num = -num; den = -den; }
        long long g = std::gcd(num >= 0 ? num : -num, den);
        n = num / g;
        d = den / g;
    }

    static Fraction quarters(long long q) { return Fraction(q, 1); }

    bool operator==(const Fraction& o) const { return n == o.n && d == o.d; }
    bool operator!=(const Fraction& o) const { return !(*this == o); }
    bool operator<(const Fraction& o) const { return n * o.d < o.n * d; }
    bool operator<=(const Fraction& o) const { return !(o < *this); }
    bool operator>(const Fraction& o) const { return o < *this; }
    bool operator>=(const Fraction& o) const { return !(*this < o); }

    Fraction operator+(const Fraction& o) const {
        return Fraction(n * o.d + o.n * d, d * o.d);
    }
    Fraction operator-(const Fraction& o) const {
        return Fraction(n * o.d - o.n * d, d * o.d);
    }
    Fraction operator*(const Fraction& o) const {
        return Fraction(n * o.n, d * o.d);
    }
    Fraction operator*(long long k) const { return Fraction(n * k, d); }
    Fraction operator/(const Fraction& o) const {
        if (o.n == 0) throw std::invalid_argument("division by zero");
        long long nn = n * o.d, dd = d * o.n;
        if (dd < 0) { nn = -nn; dd = -dd; }
        return Fraction(nn, dd);
    }

    Fraction& operator+=(const Fraction& o) { *this = *this + o; return *this; }

    bool isZero() const { return n == 0; }
    bool isInteger() const { return d == 1; }

    // Exact integer division: returns (quotient, exact?). quotient = *this / unit.
    long long divExact(const Fraction& unit, bool& exact) const {
        Fraction q = *this / unit;
        if (q.d == 1) { exact = true; return q.n; }
        exact = false;
        return 0;
    }

    std::string str() const {
        if (d == 1) return std::to_string(n);
        return std::to_string(n) + "/" + std::to_string(d);
    }
};

inline Fraction barQuarters(int num, int den) {
    // Bar length in quarter notes = 4 * num / den.
    return Fraction(4LL * num, den);
}

inline Fraction unitQuarters(int denom) {
    // One L: unit = 1/denom whole note = 4/denom quarters.
    return Fraction(4, denom);
}

}  // namespace yue2_abcedit
