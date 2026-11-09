/*
 * Part of the bbpPairingsWebAssemblyPort tie-break tool (MIT licence, see
 * LICENSE). The tie-break rules are ported from Gacrux (TieBreakServer,
 * Copyright (c) 2024 FIDE, MIT licence, https://www.gacrux.no); see
 * THIRD_PARTY_NOTICES.md.
 */

#ifndef VALUE_H
#define VALUE_H

#include <cstdint>
#include <string>

#include "decimal.h"

namespace tiebreak
{
  /**
   * A tie-break value as the reference implementation holds it: nothing (an
   * unrated field), an integer (a count, a rank, a rating) or a decimal (a
   * score). Integer and decimal mix the way Python's int and Decimal do: an
   * integer plus a decimal is a decimal, and a count that never had a score
   * added stays an integer and prints as one ("0", not "0.0").
   */
  class Value
  {
  public:
    enum Kind
    {
      NONE,
      INTEGER,
      DECIMAL
    };

    Kind kind{ NONE };
    std::int64_t integer{ };
    Decimal decimal{ };

    Value() = default;

    static Value none()
    {
      return Value{ };
    }

    static Value fromInt(const std::int64_t value)
    {
      Value result{ };
      result.kind = INTEGER;
      result.integer = value;
      return result;
    }

    static Value fromDecimal(const Decimal &value)
    {
      Value result{ };
      result.kind = DECIMAL;
      result.decimal = value;
      return result;
    }

    bool isNone() const
    {
      return kind == NONE;
    }

    /** The value as a decimal; an integer keeps exponent 0. */
    Decimal asDecimal() const
    {
      return kind == DECIMAL ? decimal : Decimal::fromInt(integer);
    }

    Value operator+(const Value &other) const
    {
      if (kind == INTEGER && other.kind == INTEGER)
      {
        return fromInt(integer + other.integer);
      }
      return fromDecimal(asDecimal() + other.asDecimal());
    }

    Value operator*(const Value &other) const
    {
      if (kind == INTEGER && other.kind == INTEGER)
      {
        return fromInt(integer * other.integer);
      }
      return fromDecimal(asDecimal() * other.asDecimal());
    }

    /** Numeric comparison; callers keep None out. */
    int compare(const Value &other) const
    {
      if (kind == INTEGER && other.kind == INTEGER)
      {
        return integer < other.integer ? -1 : integer > other.integer ? 1 : 0;
      }
      return asDecimal().compare(other.asDecimal());
    }

    /** Python's ==: None equals only None. */
    bool equals(const Value &other) const
    {
      if (kind == NONE || other.kind == NONE)
      {
        return kind == other.kind;
      }
      return compare(other) == 0;
    }

    /** Python's str(), with None written as the reference tool writes it. */
    std::string toString() const
    {
      if (kind == NONE)
      {
        return "-0";
      }
      if (kind == INTEGER)
      {
        return std::to_string(integer);
      }
      return decimal.toString();
    }
  };
}

#endif
