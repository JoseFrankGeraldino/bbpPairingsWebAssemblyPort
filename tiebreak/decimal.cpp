/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "decimal.h"

namespace tiebreak
{
  namespace
  {
    constexpr std::int64_t INT_MAX_VALUE{
      std::numeric_limits<std::int64_t>::max() };

    /**
     * Multiply by 10^power, or report that the result does not fit.
     */
    bool multiplyByPowerOfTen(
      std::int64_t &value,
      const int power)
    {
      for (int i{ }; i < power; ++i)
      {
        if (value > INT_MAX_VALUE / 10 || value < -(INT_MAX_VALUE / 10))
        {
          return false;
        }
        value *= 10;
      }
      return true;
    }

    std::int64_t absolute(const std::int64_t value)
    {
      return value < 0 ? -value : value;
    }

    int countDigits(std::int64_t value)
    {
      value = absolute(value);
      int digits{ 1 };
      while (value >= 10)
      {
        value /= 10;
        ++digits;
      }
      return digits;
    }

    /**
     * Bring both operands to the smaller exponent.
     *
     * @throws std::overflow_error if a coefficient does not fit.
     */
    int align(std::int64_t &first, const int firstExponent,
      std::int64_t &second, const int secondExponent)
    {
      if (firstExponent > secondExponent)
      {
        if (!multiplyByPowerOfTen(first, firstExponent - secondExponent))
        {
          throw std::overflow_error("decimal overflow");
        }
        return secondExponent;
      }
      if (!multiplyByPowerOfTen(second, secondExponent - firstExponent))
      {
        throw std::overflow_error("decimal overflow");
      }
      return firstExponent;
    }
  }

  Decimal::Decimal(const std::int64_t coefficient_, const int exponent_)
    : coefficient(coefficient_), exponent(exponent_)
  { }

  Decimal Decimal::fromInt(const std::int64_t value)
  {
    return Decimal(value, 0);
  }

  Decimal Decimal::parse(const std::string &text)
  {
    std::size_t position{ };
    bool negative{ };
    if (position < text.size()
      && (text[position] == '+' || text[position] == '-'))
    {
      negative = text[position] == '-';
      ++position;
    }
    std::int64_t coefficient_{ };
    int exponent_{ };
    bool digits{ };
    bool point{ };
    for (; position < text.size(); ++position)
    {
      const char character{ text[position] };
      if (character >= '0' && character <= '9')
      {
        if (!multiplyByPowerOfTen(coefficient_, 1)
          || coefficient_ > INT_MAX_VALUE - (character - '0'))
        {
          throw std::overflow_error("decimal overflow");
        }
        coefficient_ += character - '0';
        digits = true;
        if (point)
        {
          --exponent_;
        }
      }
      else if (character == '.' && !point)
      {
        point = true;
      }
      else
      {
        break;
      }
    }
    if (!digits)
    {
      throw std::invalid_argument("not a number: " + text);
    }
    if (position < text.size()
      && (text[position] == 'e' || text[position] == 'E'))
    {
      ++position;
      bool negativeExponent{ };
      if (position < text.size()
        && (text[position] == '+' || text[position] == '-'))
      {
        negativeExponent = text[position] == '-';
        ++position;
      }
      int value{ };
      bool exponentDigits{ };
      for (; position < text.size(); ++position)
      {
        const char character{ text[position] };
        if (character < '0' || character > '9' || value > 1000)
        {
          break;
        }
        value = value * 10 + (character - '0');
        exponentDigits = true;
      }
      if (!exponentDigits)
      {
        throw std::invalid_argument("not a number: " + text);
      }
      exponent_ += negativeExponent ? -value : value;
    }
    if (position != text.size())
    {
      throw std::invalid_argument("not a number: " + text);
    }
    return Decimal(negative ? -coefficient_ : coefficient_, exponent_);
  }

  Decimal Decimal::operator+(const Decimal &other) const
  {
    std::int64_t first{ coefficient };
    std::int64_t second{ other.coefficient };
    const int resultExponent{
      align(first, exponent, second, other.exponent) };
    if ((second > 0 && first > INT_MAX_VALUE - second)
      || (second < 0 && first < -INT_MAX_VALUE - second))
    {
      throw std::overflow_error("decimal overflow");
    }
    return Decimal(first + second, resultExponent);
  }

  Decimal Decimal::operator-() const
  {
    return Decimal(-coefficient, exponent);
  }

  Decimal Decimal::operator-(const Decimal &other) const
  {
    return *this + -other;
  }

  Decimal Decimal::operator*(const Decimal &other) const
  {
    if (coefficient != 0 && other.coefficient != 0
      && absolute(coefficient) > INT_MAX_VALUE / absolute(other.coefficient))
    {
      throw std::overflow_error("decimal overflow");
    }
    return Decimal(coefficient * other.coefficient, exponent + other.exponent);
  }

  Decimal Decimal::operator/(const Decimal &other) const
  {
    if (other.coefficient == 0)
    {
      throw std::domain_error("decimal division by zero");
    }
    const int idealExponent{ exponent - other.exponent };
    if (coefficient == 0)
    {
      return Decimal(0, idealExponent);
    }
    const bool negative{ (coefficient < 0) != (other.coefficient < 0) };
    const std::int64_t divisor{ absolute(other.coefficient) };
    std::int64_t quotient{ absolute(coefficient) / divisor };
    std::int64_t remainder{ absolute(coefficient) % divisor };
    int shift{ };
    // Long division, one digit at a time, so that nothing but the quotient
    // grows: the remainder stays below the divisor.
    while (remainder != 0 && countDigits(quotient) < PRECISION)
    {
      remainder *= 10;
      quotient = quotient * 10 + remainder / divisor;
      remainder %= divisor;
      ++shift;
    }
    if (remainder != 0)
    {
      const std::int64_t twice{ remainder * 2 };
      if (twice > divisor || (twice == divisor && quotient % 2 != 0))
      {
        ++quotient;
        if (countDigits(quotient) > PRECISION)
        {
          quotient /= 10;
          --shift;
        }
      }
    }
    return Decimal(negative ? -quotient : quotient, idealExponent - shift);
  }

  Decimal Decimal::quantize(const int exponent_, const bool halfUp) const
  {
    if (exponent_ <= exponent)
    {
      std::int64_t value{ coefficient };
      if (!multiplyByPowerOfTen(value, exponent - exponent_))
      {
        throw std::overflow_error("decimal overflow");
      }
      return Decimal(value, exponent_);
    }
    const int difference{ exponent_ - exponent };
    if (difference > 18)
    {
      return Decimal(0, exponent_);
    }
    std::int64_t power{ 1 };
    multiplyByPowerOfTen(power, difference);
    std::int64_t quotient{ absolute(coefficient) / power };
    const std::int64_t remainder{ absolute(coefficient) % power };
    const std::int64_t half{ power / 2 };
    if (remainder > half
      || (remainder == half && (halfUp || quotient % 2 != 0)))
    {
      ++quotient;
    }
    return Decimal(coefficient < 0 ? -quotient : quotient, exponent_);
  }

  std::int64_t Decimal::roundToInteger() const
  {
    const Decimal rounded{ quantize(exponent > 0 ? exponent : 0, false) };
    std::int64_t value{ rounded.coefficient };
    if (!multiplyByPowerOfTen(value, rounded.exponent))
    {
      throw std::overflow_error("decimal overflow");
    }
    return value;
  }

  int Decimal::compare(const Decimal &other) const
  {
    std::int64_t first{ coefficient };
    std::int64_t second{ other.coefficient };
    // A coefficient that overflows when scaled is larger in magnitude than
    // any int64, so its sign decides.
    if (exponent > other.exponent
      && !multiplyByPowerOfTen(first, exponent - other.exponent))
    {
      return coefficient < 0 ? -1 : 1;
    }
    if (other.exponent > exponent
      && !multiplyByPowerOfTen(second, other.exponent - exponent))
    {
      return other.coefficient < 0 ? 1 : -1;
    }
    return first < second ? -1 : first > second ? 1 : 0;
  }

  bool Decimal::isZero() const
  {
    return coefficient == 0;
  }

  std::string Decimal::toString() const
  {
    const std::string digits{ std::to_string(absolute(coefficient)) };
    const int length{ static_cast<int>(digits.size()) };
    const int leftDigits{ exponent + length };
    const int dotPlace{ exponent <= 0 && leftDigits > -6 ? leftDigits : 1 };
    std::string result{ coefficient < 0 ? "-" : "" };
    if (dotPlace <= 0)
    {
      result += "0." + std::string(static_cast<std::size_t>(-dotPlace), '0')
        + digits;
    }
    else if (dotPlace >= length)
    {
      result += digits
        + std::string(static_cast<std::size_t>(dotPlace - length), '0');
    }
    else
    {
      result += digits.substr(0, static_cast<std::size_t>(dotPlace)) + "."
        + digits.substr(static_cast<std::size_t>(dotPlace));
    }
    if (leftDigits != dotPlace)
    {
      const int scientific{ leftDigits - dotPlace };
      result += std::string("E") + (scientific < 0 ? "-" : "+")
        + std::to_string(scientific < 0 ? -scientific : scientific);
    }
    return result;
  }
}
