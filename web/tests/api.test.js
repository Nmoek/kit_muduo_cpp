import { describe, expect, it } from 'vitest';
import { createBrowserContext, loadCoreScripts, loginMockUser } from './helpers/browser_context.js';

describe('V1 config and API layer', () => {
    /**
     * 测试思路：没有 URL 或本地配置覆盖时，前端 API 层必须默认连接真实后端。
     * 示例：打开 main.html 不带 apiMode 参数时，apiMode 应为 real，避免生产环境误走 Mock。
     */
    it('默认使用 real 模式', () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        expect(context.KitProxy.config.apiMode).toBe('real');
    });

    /**
     * 测试思路：URL 查询参数是开发调试入口，应能临时切到 Mock 数据源且不触发 fetch。
     * 示例：访问 ?apiMode=mock 后，获取服务列表应返回 Mock 数据，并且不会请求真实后端。
     */
    it('URL 参数可以切到 mock 模式', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context, {
            note: 'admin',
            loginType: 'admin',
            password: 'admin123',
        });

        expect(context.KitProxy.config.apiMode).toBe('mock');
        const projects = await context.KitProxy.api.getProjectList();

        expect(projects.length).toBeGreaterThan(0);
        expect(context.fetch).not.toHaveBeenCalled();
    });

    /**
     * 测试思路：Mock 新增服务要模拟真实“新增后按 id 查询”的交互链路。
     * 示例：新增 listen_port=18080 的服务后，按返回 project_id 查询应能拿到同名同端口服务。
     */
    it('mock 模式新增服务后可以查询单项', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        const addResult = await context.KitProxy.api.addProject({
            name: '新增服务',
            mode: 1,
            protocol_type: 1,
            listen_port: 18080,
            target_ip: '',
            pattern_info: {},
        });
        const projects = await context.KitProxy.api.getProject(addResult.project_id);

        expect(projects[0].name).toBe('新增服务');
        expect(projects[0].listen_port).toBe(18080);
        expect(projects[0].status).toBe(1);
        expect(projects[0].active).toBe(0);
    });

    /**
     * 测试思路：服务启停由 active 表示，Mock 启动时模拟分配端口，停止时 active 回到 0。
     * 示例：新增服务器模式服务 listen_port=0，启动后获得端口，停止后页面可显示“未开启”。
     */
    it('mock 模式 setProjectActive 更新 active 和监听端口', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        const addResult = await context.KitProxy.api.addProject({
            name: '待启动服务',
            mode: 1,
            protocol_type: 1,
            listen_port: 0,
            target_ip: '',
            pattern_info: {},
        });

        const started = await context.KitProxy.api.setProjectActive(addResult.project_id, true);
        expect(started.active).toBe(1);
        expect(started.listen_port).toBeGreaterThan(0);

        const stopped = await context.KitProxy.api.setProjectActive(addResult.project_id, false);
        expect(stopped.active).toBe(0);
        expect(stopped.listen_port).toBe(0);
    });

    /**
     * 测试思路：Real API 启停只走 /projects/{id}/status?operation=1|0，不发送 JSON body。
     * 示例：启动传 operation=1，停止传 operation=0。
     */
    it('real 模式 setProjectActive 调用后端启停入口', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({ code: 0, data: { listen_port: 18080 } }),
        });

        await context.KitProxy.api.setProjectActive(7, true);
        expect(context.fetch).toHaveBeenLastCalledWith('/projects/7/status?operation=1', {
            method: 'POST',
            credentials: 'same-origin',
        });

        await context.KitProxy.api.setProjectActive(7, false);
        expect(context.fetch).toHaveBeenLastCalledWith('/projects/7/status?operation=0', {
            method: 'POST',
            credentials: 'same-origin',
        });
    });

    /**
     * 测试思路：Mock 服务列表分页应在内存数据上执行 offset/limit 切片。
     * 示例：limit=1 分别请求 offset=0 和 offset=1，应得到两个不同服务。
     */
    it('mock 服务列表支持 offset 和 limit 分页切片', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        const firstPage = await context.KitProxy.api.getProjectList(0, 1);
        const secondPage = await context.KitProxy.api.getProjectList(1, 1);

        expect(firstPage).toHaveLength(1);
        expect(secondPage).toHaveLength(1);
        expect(firstPage[0].id).not.toBe(secondPage[0].id);
    });

    /**
     * 测试思路：Mock 协议项列表分页要同时受 projectId、offset、limit 约束。
     * 示例：给 projectId=1 新增一个协议项后，前两页各取 1 条，应返回不同协议项。
     */
    it('mock 协议项列表支持 projectId、offset 和 limit 分页切片', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context, {
            note: 'admin',
            loginType: 'admin',
            password: 'admin123',
        });

        await context.KitProxy.api.addProtocol({
            cfg_header: {
                name: '接口2',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
            },
            req_cfg: {
                method: 'POST',
                path: '/api/test2',
            },
            resp_cfg: {},
            request_body: '',
            response_body: '',
        });

        const firstPage = await context.KitProxy.api.getProtocolList(1, 0, 1);
        const secondPage = await context.KitProxy.api.getProtocolList(1, 1, 1);

        expect(firstPage).toHaveLength(1);
        expect(secondPage).toHaveLength(1);
        expect(firstPage[0].id).not.toBe(secondPage[0].id);
    });

    /**
     * 测试思路：真实接口 HTTP 状态非 2xx 时，API 层应把后端错误信息抛给调用方。
     * 示例：fetch 返回 500 且 JSON message 为 server down，调用 getProjectList 应 reject。
     */
    it('real 模式会抛出 HTTP 非 2xx 错误', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: false,
            status: 500,
            json: async () => ({ message: 'server down' }),
        });

        await expect(context.KitProxy.api.getProjectList()).rejects.toThrow('server down');
    });

    /**
     * 测试思路：真实接口即使 HTTP 200，业务 code 非 0 也应视为失败。
     * 示例：返回 code=1001/message=业务失败 时，调用方应收到“业务失败”异常。
     */
    it('real 模式会抛出业务 code 非 0 错误', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({ code: 1001, message: '业务失败' }),
        });

        await expect(context.KitProxy.api.getProjectList()).rejects.toThrow('业务失败');
    });
});
