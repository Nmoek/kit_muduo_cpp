#!/usr/bin/env python3

r"""Generate a local test CA and an HTTPS server certificate using OpenSSL.

Example:
    python3 tools/generate_tls_certificate.py --output-dir /tmp/kit-https-certs
    python3 tools/generate_tls_certificate.py --output-dir /tmp/kit-lan-certs \
        --dns localhost --ip 127.0.0.1 --ip 192.168.1.10 --days 30

The output directory must not exist. Keys are unencrypted PEM files with mode
0600; the directory has mode 0700. No system trust settings are changed.
"""

import argparse
import ipaddress
import json
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys


def dns_name(value: str) -> str:
    value = value.rstrip('.').lower()
    labels = value.split('.')
    if len(value) > 253 or any(
        re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', label) is None
        for label in labels
    ):
        raise argparse.ArgumentTypeError(f'invalid DNS name: {value}')
    return value


def ip_address(value: str) -> str:
    try:
        return str(ipaddress.ip_address(value))
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def validity_days(value: str) -> int:
    try:
        days = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError('days must be an integer') from error
    if not 1 <= days <= 3650:
        raise argparse.ArgumentTypeError('days must be in range [1, 3650]')
    return days


def generate(directory: Path, dns_names: list, ip_addresses: list, days: int) -> None:
    def openssl(*args: str) -> None:
        subprocess.run(['openssl', *args], cwd=directory, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256',
            '-days', str(days + 1), '-subj', '/CN=Kit Local Test CA',
            '-keyout', 'ca-key.pem', '-out', 'ca.pem',
            '-addext', 'basicConstraints=critical,CA:TRUE,pathlen:0',
            '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
    openssl('req', '-new', '-newkey', 'rsa:2048', '-nodes', '-sha256',
            '-subj', '/CN=Kit HTTPS Test Server', '-keyout', 'server-key.pem', '-out', 'server.csr')
    san = [f'DNS:{name}' for name in dns_names] + [f'IP:{address}' for address in ip_addresses]
    (directory / 'extensions.cnf').write_text(
        'basicConstraints=critical,CA:FALSE\n'
        'keyUsage=critical,digitalSignature,keyEncipherment\n'
        'extendedKeyUsage=serverAuth\n'
        f'subjectAltName={",".join(san)}\n', encoding='utf-8')
    openssl('x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca-key.pem',
            '-set_serial', str(secrets.randbits(128) + 1), '-days', str(days), '-sha256',
            '-extfile', 'extensions.cnf', '-out', 'server.pem')
    for name in ('ca-key.pem', 'server-key.pem'):
        (directory / name).chmod(0o600)
    (directory / 'server-chain.pem').write_bytes(
        (directory / 'server.pem').read_bytes() + (directory / 'ca.pem').read_bytes())
    openssl('verify', '-purpose', 'sslserver', '-CAfile', 'ca.pem', 'server.pem')
    for name in ('server.csr', 'extensions.cnf'):
        (directory / name).unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output-dir', required=True, type=Path,
                        help='new output directory (existing directories are never overwritten)')
    parser.add_argument('--dns', action='append', type=dns_name,
                        help='DNS SAN, repeatable; defaults to localhost if no SAN options are given')
    parser.add_argument('--ip', action='append', type=ip_address,
                        help='IP SAN, repeatable; defaults to 127.0.0.1 and ::1 with default DNS')
    parser.add_argument('--days', type=validity_days, default=365,
                        help='server certificate validity in days (default: 365)')
    args = parser.parse_args()
    dns_names = list(dict.fromkeys(args.dns or []))
    ip_addresses = list(dict.fromkeys(args.ip or []))
    if not dns_names and not ip_addresses:
        dns_names, ip_addresses = ['localhost'], ['127.0.0.1', '::1', '172.25.66.135']
    if shutil.which('openssl') is None:
        parser.error('openssl is required (OpenSSL 1.1.1 or newer)')
    directory = args.output_dir.expanduser().resolve()
    try:
        directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    except OSError as error:
        print(f'cannot create output directory: {error}', file=sys.stderr)
        return 1
    try:
        generate(directory, dns_names, ip_addresses, args.days)
    except (OSError, subprocess.CalledProcessError) as error:
        shutil.rmtree(directory)
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        print(f'certificate generation failed: {detail}', file=sys.stderr)
        return 1
    print(f'Generated local test CA and server certificate in {directory}')
    print('CA certificate: ca.pem; server certificate: server.pem; chain: server-chain.pem')
    print('Private keys: server-key.pem, ca-key.pem (unencrypted PEM, mode 0600)')
    print('Add these absolute paths to config/config.yaml:')
    print('work:\n  https:')
    print(f'    cert_chain_file: {json.dumps(str(directory / "server-chain.pem"))}')
    print(f'    private_key_file: {json.dumps(str(directory / "server-key.pem"))}')
    print('Use curl --cacert <output-dir>/ca.pem https://<SAN-host>:<port>/<path> to verify HTTPS.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
