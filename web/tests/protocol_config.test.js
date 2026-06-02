import { describe, expect, it } from 'vitest';
import { createBrowserContext, loadCoreScripts, loginMockUser } from './helpers/browser_context.js';

describe('V1 protocol config API', () => {
    /**
     * 测试思路：协议项列表只带摘要字段，编辑和详情场景仍需按 id 拉取完整配置。
     * 示例：查询协议项 1 的完整配置，应能读取请求 method=GET 和 path=/api/test1。
     */
    it('保留按需查询完整协议项配置的接口能力', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        const cfg = await context.KitProxy.api.getProtocolDetailsCfg(1);

        expect(cfg.req_cfg.path).toBe('/api/test1');
        expect(cfg.req_cfg.method).toBe('GET');
    });
});
