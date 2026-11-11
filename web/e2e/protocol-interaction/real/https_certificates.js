import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

/** 为隔离后端和 Node TLS 客户端准备同一套临时 CA，工作进程通过环境变量共享路径。 */
export function prepareHttpsCertificates() {
    if (process.env.PLAYWRIGHT_REAL_HTTPS_CERT_DIR) return;
    const parent = mkdtempSync(path.join(tmpdir(), 'kit-playwright-https-'));
    const directory = path.join(parent, 'certificates');
    const script = path.resolve(__dirname, '../../../../tools/generate_tls_certificate.py');
    try {
        execFileSync('python3', [script, '--output-dir', directory, '--days', '2'], { stdio: 'pipe' });
    } catch (error) {
        rmSync(parent, { recursive: true, force: true });
        throw error;
    }
    process.env.PLAYWRIGHT_REAL_HTTPS_CERT_DIR = directory;
    process.env.PLAYWRIGHT_REAL_HTTPS_GENERATED = '1';
    process.env.PLAYWRIGHT_REAL_HTTPS_TEMP_DIR = parent;
}
