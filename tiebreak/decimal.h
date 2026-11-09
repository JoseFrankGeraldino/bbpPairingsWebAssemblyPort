/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#ifndef DECIMAL_H
#define DECIMAL_H

#include <cstdint>
#include <string>

namespace tiebreak
{
  /**
   * A decimal number coefficient * 10^exponent that follows the arithmetic of
   * Python's decimal.Decimal for the operations the tie-break rules use, so
   * that values print exactly as the reference implementation prints them
   * ("13.0" for a Buchholz, "6.50" for a Sonneborn-Berger product).
   *
   * Addition and subtraction keep the smaller exponent, multiplication adds
   * the exponents, and an exact quotient keeps the ideal exponent of the
   * division. An inexact quotient is rounded half-even to PRECISION
   * significant digits (Python uses 28; the quotients here are averages that
   * are compared or rounded to at most five decimals, which 15 digits decide
   * the same way).
   */
  class Decimal
  {
  public:
    static constexpr int PRECISION{ 15 };

    std::int64_t coefficient{ };
    int exponent{ };

    Decimal() = default;
    Decimal(std::int64_t coefficient_, int exponent_);

    /**
     * Parse the text the way Python's Decimal(str) does for plain numbers:
     * optional sign, digits with an optional decimal point, optional
     * exponent.
     *
     * @throws std::invalid_argument if the text is not such a number.
     * @throws std::overflow_error if it has too many digits.
     */
    static Decimal parse(const std::string &text);

    static Decimal fromInt(std::int64_t value);

    Decimal operator+(const Decimal &other) const;
    Decimal operator-(const Decimal &other) const;
    Decimal operator-() const;
    Decimal operator*(const Decimal &other) const;

    /**
     * Python's Decimal division.
     *
     * @throws std::domain_error on division by zero.
     */
    Decimal operator/(const Decimal &other) const;

    /**
     * Python's Decimal.quantize to the given exponent, rounding half-up or
     * half-even.
     */
    Decimal quantize(int exponent_, bool halfUp) const;

    /** Python's round(Decimal): half-even to an integer. */
    std::int64_t roundToInteger() const;

    /** -1, 0 or 1 as the numeric values compare; exponents do not matter. */
    int compare(const Decimal &other) const;

    bool isZero() const;

    /** Python's str(Decimal). */
    std::string toString() const;

    bool operator==(const Decimal &other) const
    {
      return compare(other) == 0;
    }
    bool operator<(const Decimal &other) const
    {
      return compare(other) < 0;
    }
  };
}

#endif
