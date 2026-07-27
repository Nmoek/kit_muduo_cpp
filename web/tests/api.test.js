import { describe, expect, it } from 'vitest';
import {
    createBrowserContext,
    loadCoreScripts,
    loginMockUser,
    readFormDataValueAsText,
} from './helpers/browser_context.js';

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
        expect(projects[0].runtime_state).toBe(0);
    });

    /**
     * 测试思路：Mock 普通用户从主页面跳转到独立协议项页时，新的 HTML 文档会重新创建内存 state，
     * 单项目查询必须先补齐该用户的示例项目，再加载项目和协议项。
     * 示例：文档 A 登录 testuser 得到 projectId=101，文档 B 只带 session 查询 101，仍能得到项目和协议 1001。
     */
    it('mock 普通用户跨页面查询示例项目后可以加载协议项', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=101');
        loadCoreScripts(context);
        const user = await loginMockUser(context);
        const state = context.KitProxy.mocks.state;
        const userProjectIds = state.projects
            .filter(project => Number(project.user_id) === Number(user.id))
            .map(project => Number(project.id));

        // 登录页已经补过示例；清掉它们以模拟跳转到新 HTML 文档后的初始 Mock state。
        state.projects = state.projects.filter(project => Number(project.user_id) !== Number(user.id));
        state.protocols = state.protocols.filter(protocol => !userProjectIds.includes(Number(protocol.project_id)));
        state.nextProjectId = 100;
        state.nextProtocolId = 1000;

        const projects = await context.KitProxy.api.getProject(101);
        const protocols = await context.KitProxy.api.getProtocolList(101, 0, 10);

        expect(projects).toHaveLength(1);
        expect(projects[0].id).toBe(101);
        expect(protocols).toHaveLength(1);
        expect(protocols[0].project_id).toBe(101);
    });

    /**
     * 测试思路：Mock 单项目查询也必须执行项目级权限校验，不能让普通用户读取管理员项目上下文。
     * 示例：testuser 查询 projectId=1 应返回空数组，而不是返回管理员项目后再显示空协议列表。
     */
    it('mock 普通用户不能查询其他用户的项目', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        await expect(context.KitProxy.api.getProject(1)).resolves.toEqual([]);
    });

    /**
     * 测试思路：新增项目的协议类型入参和新增协议保持一致，API 请求统一传 HTTP/TCP/HTTPS 字符串。
     * 示例：页面内部选择 protocol_type=1，真实 addProject 发给后端时应变成 protocol_type="HTTP"。
     */
    it('real 模式 addProject 发送字符串协议类型', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({ code: 0, data: { project_id: 7 } }),
        });

        await context.KitProxy.api.addProject({
            name: '字符串协议类型服务',
            mode: 1,
            protocol_type: 1,
            target_ip: '',
            pattern_info: {},
        });

        const [, options] = context.fetch.mock.calls.at(-1);
        expect(context.fetch.mock.calls.at(-1)[0]).toBe('/projects/add');
        expect(JSON.parse(options.body).protocol_type).toBe('HTTP');
    });

    /**
     * 测试思路：真实后端可能把项目协议类型返回为 HTTP/TCP/HTTPS 字符串，API 层要统一转成页面内部数值。
     * 示例：getProject 返回 protocol_type="TCP" 时，页面拿到 protocol_type=2，后续能渲染 TCP 控件。
     */
    it('real 模式 getProject 归一化字符串协议类型', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({
                code: 0,
                data: [{
                    id: 7,
                    name: '真实TCP服务',
                    protocol_type: 'TCP',
                    mode: 1,
                    runtime_state: 1,
                    status: 1,
                }],
            }),
        });

        const projects = await context.KitProxy.api.getProject(7);

        expect(projects[0].protocol_type).toBe(2);
        expect(projects[0].runtime_state).toBe(1);
    });

    /**
     * 测试思路：Mock addProject 也要接受字符串协议类型，但内存态保持页面已有数值格式。
     * 示例：传 protocol_type="TCP" 新增后，查询单项时 protocol_type 仍为 2，页面可继续走 TCP 注册表。
     */
    it('mock 模式 addProject 接受字符串协议类型并存为页面数值', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        const addResult = await context.KitProxy.api.addProject({
            name: '字符串TCP服务',
            mode: 1,
            protocol_type: 'TCP',
            target_ip: '',
            pattern_info: {},
        });
        const projects = await context.KitProxy.api.getProject(addResult.project_id);

        expect(projects[0].protocol_type).toBe(2);
    });

    /**
     * 测试思路：服务启停由 runtime_state 表示，Mock 启动时模拟分配端口，停止时 runtime_state 回到 0。
     * 示例：新增服务器模式服务 listen_port=0，启动后获得端口，停止后页面可显示“未开启”。
     */
    it('mock 模式 setProjectRuntimeState 更新 runtime_state 和监听端口', async () => {
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

        const started = await context.KitProxy.api.setProjectRuntimeState(addResult.project_id, true);
        expect(started.runtime_state).toBe(1);
        expect(started.listen_port).toBeGreaterThan(0);

        const stopped = await context.KitProxy.api.setProjectRuntimeState(addResult.project_id, false);
        expect(stopped.runtime_state).toBe(0);
        expect(stopped.listen_port).toBe(0);
    });

    /**
     * 测试思路：Real API 启停只走 /projects/{id}/runtime_state?operation=1|0，不发送 JSON body。
     * 示例：启动传 operation=1，停止传 operation=0。
     */
    it('real 模式 setProjectRuntimeState 调用后端启停入口', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({ code: 0, data: { listen_port: 18080 } }),
        });

        await context.KitProxy.api.setProjectRuntimeState(7, true);
        expect(context.fetch).toHaveBeenLastCalledWith('/projects/7/runtime_state?operation=1', {
            method: 'POST',
            credentials: 'same-origin',
        });

        await context.KitProxy.api.setProjectRuntimeState(7, false);
        expect(context.fetch).toHaveBeenLastCalledWith('/projects/7/runtime_state?operation=0', {
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
                config_state: 0,
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
     * 测试思路：Mock 写操作回执要模拟后端 persisted/runtime_applied 语义，方便页面按真实响应调试。
     * 示例：只保存协议 runtime_applied=0，保存并上线 runtime_applied=1，重配置完成后回到 config_state=0 且 runtime_applied=0。
     */
    it('mock 新增和重配置协议返回运行态回执语义', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        await loginMockUser(context);

        const saved = await context.KitProxy.api.addProtocol({
            cfg_header: {
                name: '只保存接口',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
                config_state: 0,
            },
            req_cfg: { method: 'GET', path: '/api/saved' },
            resp_cfg: {},
            request_body: '',
            response_body: '',
        });
        expect(saved.persisted).toBe(1);
        expect(saved.runtime_applied).toBe(0);
        expect(saved.config_state).toBe(0);

        const online = await context.KitProxy.api.addProtocol({
            cfg_header: {
                name: '保存上线接口',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
                config_state: 1,
            },
            req_cfg: { method: 'GET', path: '/api/online' },
            resp_cfg: {},
            request_body: '',
            response_body: '',
        });
        expect(online.persisted).toBe(1);
        expect(online.runtime_applied).toBe(1);
        expect(online.config_state).toBe(1);

        const reconfigured = await context.KitProxy.api.reconfigProtocol(online.protocol_id, {
            cfg_header: {
                name: '重配置后离线',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
                config_state: 0,
            },
            req_cfg: { method: 'GET', path: '/api/reconfigured' },
            resp_cfg: {},
            request_body: '',
            response_body: '',
        });
        expect(reconfigured.persisted).toBe(1);
        expect(reconfigured.runtime_applied).toBe(0);
        expect(reconfigured.config_state).toBe(0);
    });

    /**
     * 测试思路：协议上线/下线接口只发送 runtime_enabled 命令字段，页面状态由返回的 config_state 判断。
     * 示例：上线传 runtime_enabled=1，下线传 runtime_enabled=0，路径固定为 /protocols/{id}/runtime_enabled。
     */
    it('real 模式 setProtocolRuntime 调用协议上线下线入口', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({ code: 0, data: { protocol_id: 9, config_state: 1 } }),
        });

        await context.KitProxy.api.setProtocolRuntime(9, true);
        expect(context.fetch).toHaveBeenLastCalledWith('/protocols/9/runtime_enabled', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ runtime_enabled: 1 }),
            credentials: 'same-origin',
        });

        await context.KitProxy.api.setProtocolRuntime(9, false);
        expect(context.fetch).toHaveBeenLastCalledWith('/protocols/9/runtime_enabled', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ runtime_enabled: 0 }),
            credentials: 'same-origin',
        });
    });

    /**
     * 测试思路：重配置接口复用新增协议 multipart 结构，API 层不能走局部 update*。
     * 示例：提交 payload 后应 POST /protocols/{id}/reconfig，body 是 FormData 且 header 含 config_state=0。
     */
    it('real 模式 reconfigProtocol 调用重配置入口并使用 FormData', async () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        context.fetch.mockResolvedValue({
            ok: true,
            status: 200,
            json: async () => ({ code: 0, data: { protocol_id: 9, config_state: 0 } }),
        });

        await context.KitProxy.api.reconfigProtocol(9, {
            cfg_header: {
                name: '重配置接口',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
                config_state: 0,
            },
            req_cfg: { method: 'GET', path: '/api/reconfig' },
            resp_cfg: {},
            request_body: '',
            response_body: '',
        });

        const [, options] = context.fetch.mock.calls.at(-1);
        expect(context.fetch.mock.calls.at(-1)[0]).toBe('/protocols/9/reconfig');
        expect(options.method).toBe('POST');
        expect(options.body).toBeInstanceOf(context.FormData);
        const cfgHeader = JSON.parse(await readFormDataValueAsText(
            context,
            options.body.get('protocol_cfg_header'),
        ));
        expect(cfgHeader.config_state).toBe(0);
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
