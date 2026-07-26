#!/usr/bin/env python3

"""Generate password hashes compatible with kit_domain::PasswordHasher."""

import hashlib
import os
import sys


SCRYPT_N = 1 << 15
SCRYPT_R = 8
SCRYPT_P = 1
SCRYPT_MAX_MEMORY = 64 * 1024 * 1024
SALT_SIZE = 16
DKLEN = 32


def hash_password(password: str) -> str:
    salt = os.urandom(SALT_SIZE)
    digest = hashlib.scrypt(
        password.encode(),
        salt=salt,
        n=SCRYPT_N,
        r=SCRYPT_R,
        p=SCRYPT_P,
        maxmem=SCRYPT_MAX_MEMORY,
        dklen=DKLEN,
    )
    return f"$scrypt${SCRYPT_N}${SCRYPT_R}${SCRYPT_P}${salt.hex()}${digest.hex()}"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: password_hash.py <password>", file=sys.stderr)
        return 2

    try:
        print(hash_password(sys.argv[1]))
    except Exception as error:
        print(f"password hash failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
