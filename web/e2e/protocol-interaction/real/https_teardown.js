import { rmSync } from 'node:fs';

/** 只清理由本轮测试生成的临时证书，不删除用户通过环境变量提供的证书目录。 */
export default function teardownHttpsCertificates() {
    if (process.env.PLAYWRIGHT_REAL_HTTPS_GENERATED === '1') {
        rmSync(process.env.PLAYWRIGHT_REAL_HTTPS_TEMP_DIR, { recursive: true, force: true });
    }
}
