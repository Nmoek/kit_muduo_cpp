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
attachment_project_id="${PLAYWRIGHT_REAL_ATTACHMENT_PROJECT_ID:-2}"
attachment_http_port="${PLAYWRIGHT_REAL_ATTACHMENT_HTTP_PORT:-42627}"
attachment_response_body_hex="${PLAYWRIGHT_REAL_ATTACHMENT_RESPONSE_BODY_HEX:-05060708}"
attachment_response_body_value="H${attachment_response_body_hex}"
attachment_response_byte_len=$(( ${#attachment_response_body_hex} / 2 ))
attachment_response_body_json="{\"fields\":[{\"spec\":{\"byte_len\":${attachment_response_byte_len},\"byte_pos\":0,\"match\":\"${attachment_response_body_value}\",\"name\":\"Real E2E response body\",\"role\":\"common\",\"type\":\"STR\"},\"value\":\"${attachment_response_body_value}\"}]}"

if ! [[ "$attachment_protocol_id" =~ ^[0-9]+$ ]]; then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_PROTOCOL_ID must be an integer" >&2
    exit 1
fi
if ! [[ "$attachment_project_id" =~ ^[0-9]+$ ]]; then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_PROJECT_ID must be an integer" >&2
    exit 1
fi
if ! [[ "$attachment_http_port" =~ ^[0-9]+$ ]] || (( attachment_http_port < 1 || attachment_http_port > 65535 )); then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_HTTP_PORT must be in range [1, 65535]" >&2
    exit 1
fi
if ! [[ "$attachment_response_body_hex" =~ ^([0-9A-Fa-f]{2})+$ ]]; then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_RESPONSE_BODY_HEX must contain an even-length hex string" >&2
    exit 1
fi
if (( attachment_response_byte_len > 32 )); then
    echo "PLAYWRIGHT_REAL_ATTACHMENT_RESPONSE_BODY_HEX must be at most 32 bytes" >&2
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

-- 仅把 HTTP protocol 4 的 response body 配成当前 Binary Body 字段 JSON。
-- 该修改只发生在 /tmp 数据库副本，不影响 TCP protocol 1 的 open/live 测试。
-- 先清理副本里旧的 Binary Body。它们可能仍是历史裸字节格式，不能参与当前
-- FieldValueMap JSON 契约的运行态恢复；附件用例会在下一条 UPDATE 中重新写入。
UPDATE protocols
SET resp_body_type = 0,
    resp_body_status = 0,
    resp_body_data = X''
WHERE resp_body_type = 7;

UPDATE protocols
SET resp_body_type = 7,
    resp_body_status = 1,
    resp_body_data = CAST('${attachment_response_body_json}' AS BLOB)
WHERE id = ${attachment_protocol_id};

UPDATE projects
SET listen_port = ${attachment_http_port},
    runtime_state = 0
WHERE id = ${attachment_project_id};
SQL

cd "$run_dir"
exec "$repo_root/bin/kit_protocol_test_platform"
