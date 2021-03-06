/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef OPERATORS_H
#define OPERATORS_H

#include "../InstructionWalker.h"

namespace vc4c
{
    namespace intermediate
    {
        NODISCARD InstructionWalker intrinsifySignedIntegerMultiplication(
            Method& method, InstructionWalker it, IntrinsicOperation& op);
        bool canOptimizeMultiplicationWithBinaryMethod(const IntrinsicOperation& op);
        NODISCARD InstructionWalker intrinsifyUnsignedIntegerMultiplication(
            Method& method, InstructionWalker it, IntrinsicOperation& op);
        NODISCARD InstructionWalker intrinsifyIntegerMultiplicationViaBinaryMethod(
            Method& method, InstructionWalker it, IntrinsicOperation& op);
        NODISCARD InstructionWalker intrinsifySignedIntegerDivision(
            Method& method, InstructionWalker it, IntrinsicOperation& op, bool useRemainder = false);
        NODISCARD InstructionWalker intrinsifyUnsignedIntegerDivision(
            Method& method, InstructionWalker it, IntrinsicOperation& op, bool useRemainder = false);
        NODISCARD InstructionWalker intrinsifySignedIntegerDivisionByConstant(
            Method& method, InstructionWalker it, IntrinsicOperation& op, bool useRemainder = false);
        NODISCARD InstructionWalker intrinsifyUnsignedIntegerDivisionByConstant(
            Method& method, InstructionWalker it, IntrinsicOperation& op, bool useRemainder = false);

        NODISCARD InstructionWalker intrinsifyFloatingDivision(
            Method& method, InstructionWalker it, IntrinsicOperation& op);

        /*
         * Implementations for on-host calculations
         */

        Literal asr(const DataType& type, const Literal& left, const Literal& right);
        Literal clz(const DataType& type, const Literal& val);
        /*
         * OpSMod: "Signed modulo operation of Operand 1 modulo Operand 2. The sign of a non-0 result comes from
         * Operand 2."
         */
        Literal smod(const DataType& type, const Literal& numerator, const Literal& denominator);
        /*
         * OpSRem: "Signed remainder operation of Operand 1 divided by Operand 2. The sign of a non-0 result comes from
         * Operand 1."
         */
        Literal srem(const DataType& type, const Literal& numerator, const Literal& denominator);
        /*
         * OpFMod: "Floating-point remainder operation of Operand 1 divided by Operand 2. The sign of a non-0 result
         * comes from Operand 2."
         */
        Literal fmod(const DataType& type, const Literal& numerator, const Literal& denominator);
        /*
         * OpFRem: "Floating-point remainder operation of Operand 1 divided by Operand 2. The sign of a non-0 result
         * comes from	Operand 1."
         */
        Literal frem(const DataType& type, const Literal& numerator, const Literal& denominator);

    } // namespace intermediate
} // namespace vc4c

#endif /* OPERATORS_H */
