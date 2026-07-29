#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
run_dir="${TMPDIR:-/tmp}/kit_protocol_playwright_real_${PPID}_$$"
admin_password="${PLAYWRIGHT_REAL_ADMIN_PASSWORD:-admin123}"
if [[ -n "${PLAYWRIGHT_REAL_ADMIN_PASSWORD_HASH:-}" ]]; then
    admin_password_hash="$PLAYWRIGHT_REAL_ADMIN_PASSWORD_HASH"
else
    hash_tool="$repo_root/tools/password_hash.py"
    if [[ ! -f "$hash_tool" ]]; then
        echo "missing password hash tool: $hash_tool" >&2
        exit 1
    fi
    admin_password_hash="$(python3 "$hash_tool" "$admin_password")"
fi
attachment_protocol_id="${PLAYWRIGHT_REAL_ATTACHMENT_PROTOCOL_ID:-4}"
attachment_response_body_hex="${PLAYWRIGHT_REAL_ATTACHMENT_RESPONSE_BODY_HEX:-05060708}"

if ! [[ "$attachment_protocol_id" =~ ^[0-9]+$ ]]; then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_PROTOCOL_ID must be an integer" >&2
    exit 1
fi
if ! [[ "$attachment_response_body_hex" =~ ^([0-9A-Fa-f]{2})+$ ]]; then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_RESPONSE_BODY_HEX must contain an even-length hex string" >&2
    exit 1
fi

mkdir -p "$run_dir"
cp "$repo_root/kit.sqlite" "$run_dir/kit.sqlite"
ln -s "$repo_root/web" "$run_dir/web"
mkdir -p "$run_dir/log"

# Real E2E 使用副本数据库，并把副本中的现有管理员密码固定为测试密码。
# 生产数据库和仓库根目录 kit.sqlite 不会被修改。
sqlite3 "$run_dir/kit.sqlite" <<SQL
UPDATE users
SET password_hash = '${admin_password_hash}'
WHERE note_name = 'admin' AND role = 2;

-- 仅把 HTTP protocol 4 的 response body 配成可稳定验证的真实 binary attachment。
-- 该修改只发生在 /tmp 数据库副本，不影响 TCP protocol 1 的 open/live 测试。
UPDATE protocols
SET resp_body_type = 7,
    resp_body_status = 1,
    resp_body_data = X'${attachment_response_body_hex}'
WHERE id = ${attachment_protocol_id};
SQL

cd "$run_dir"
exec "$repo_root/bin/kit_protocol_test_platform"
