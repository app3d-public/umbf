#!/usr/bin/env python3
"""
Signature generator for the UMBF SDK with variable bit size.
Supports any bit size from 1 to 64.
"""

import random
import sys


def generate_hex(bit_size: int, signed: bool = False) -> str:
    """
    Generates a random hexadecimal signature based on the given type.

    Args:
        type (str): Data type specifier.

    Returns:
        str: The randomly generated hexadecimal number in the format '0xXXXX'.
    """

    if not (1 <= bit_size <= 64):
        raise ValueError("Bit size must be between 1 and 64.")

    min_val = -(2**(bit_size - 1)) if signed else 0
    max_val = (2**(bit_size - 1) - 1) if signed else (2**bit_size - 1)
    number = random.randint(min_val, max_val)

    if signed and number < 0:
        # Convert to two's complement representation
        number = (1 << bit_size) + number

    hex_digits = (bit_size + 3) // 4
    hex_number = f'0x{number:0{hex_digits}X}'
    return hex_number


def parse_input(arg: str):
    """Parses the input argument to determine the bit size and sign."""
    if arg.startswith("u"):
        is_signed = False
    elif arg.startswith("i"):
        is_signed = True
    else:
        raise ValueError("Invalid format. Use 'u8', 'i16', 'u32', etc.")

    try:
        bit_size = int(arg[1:])
    except ValueError as exc:
        raise ValueError("Invalid bit size. Use numbers from 1 to 64.") from exc

    if not (1 <= bit_size <= 64):
        raise ValueError("Bit size must be between 1 and 64.")

    return bit_size, is_signed


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: sign_request.py <type>")
        print("Example: sign_request.py u32 or sign_request.py i8")
        sys.exit(1)

    try:
        bit_size_, signed_ = parse_input(sys.argv[1])
        hex_value = generate_hex(bit_size_, signed_)
        print(f'Generated signature: {hex_value}')
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)
