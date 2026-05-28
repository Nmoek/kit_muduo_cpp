import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { JSDOM } from 'jsdom';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '..');

function flushPromises(times = 4) {
    let chain = Promise.resolve();
    for (let idx = 0; idx < times; idx += 1) {
        chain = chain.then(() => Promise.resolve());
    }
    return chain;
}

/**
 * @param {vm.Context} context
 * @param {string} filePath
 */
function runScript(context, filePath) {
    // 生产代码是经典 script，不是 ES Module；测试里用 vm 按浏览器脚本方式执行。
    const code = fs.readFileSync(path.join(repoRoot, filePath), 'utf8');
    vm.runInContext(code, context, { filename: filePath });
}

function createBrowserContext(search = '?apiMode=mock') {
    // jsdom 提供 window/document/localStorage/FormData 等浏览器 API，适合测原生前端工具层。
    const dom = new JSDOM('<!doctype html><html><body><div class="service-cards"></div></body></html>', {
        url: `http://localhost/html/main.html${search}`,
    });

    const context = vm.createContext(dom.window);
    context.console = console;
    context.fetch = vi.fn();
    context.TextEncoder = TextEncoder;
    context.TextDecoder = TextDecoder;

    return context;
}

/**
 * @param {vm.Context} context
 */
function loadCoreScripts(context) {
    // 这里的顺序必须和 main.html 保持一致，否则不能发现真实的脚本依赖问题。
    [
        'js/namespace.js',
        'js/config.js',
        'js/constants.js',
        'js/utils.js',
        'js/body_editor.js',
        'js/service_filters.js',
        'js/mock_data.js',
        'js/api.js',
        'js/pagination.js',
    ].forEach(filePath => runScript(context, filePath));
}

/**
 * @param {vm.Context} context
 */
function loadProtocolListScripts(context) {
    [
        'js/tcp_pattern_modal.js',
        'js/protocol_item.js',
        'js/protocol_registry.js',
        'js/main.js',
        'js/protocol_items.js',
    ].forEach(filePath => runScript(context, filePath));
}

describe('V1 utils', () => {
    /**
     * @type {vm.Context}
     */
    let context;

    beforeEach(() => {
        context = createBrowserContext();
        loadCoreScripts(context);
    });

    it('解析以数字结尾的 DOM id', () => {
        expect(context.ExtractId('service-card-12')).toBe(12);
        expect(context.ExtractId('protocol-item-9')).toBe(9);
        expect(context.ExtractId('bad-id')).toBe(-1);
    });

    it('校验 HTTP path 和端口范围', () => {
        expect(context.KitProxy.utils.validateHttpPath('/api/test')).toBe(true);
        expect(context.KitProxy.utils.validateHttpPath('api/test')).toBe(false);
        expect(context.KitProxy.utils.validatePort(1)).toBe(true);
        expect(context.KitProxy.utils.validatePort(65535)).toBe(true);
        expect(context.KitProxy.utils.validatePort(0)).toBe(false);
        expect(context.KitProxy.utils.validatePort(65536)).toBe(false);
    });

    it('校验和格式化 JSON 文本', () => {
        expect(context.KitProxy.utils.validateJsonText('{"ok":true}')).toBe(true);
        expect(context.KitProxy.utils.validateJsonText('{bad')).toBe(false);
        expect(context.KitProxy.utils.formatJsonText('{"ok":true}')).toContain('\n');
    });

    it('构造新增协议项 FormData payload', () => {
        const formData = context.KitProxy.utils.createAddProtocolFormData({
            cfg_header: {
                name: '接口1',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
            },
            req_cfg: {
                method: 'POST',
                path: '/api/test',
            },
            resp_cfg: {},
            request_body: '{"a":1}',
            response_body: '',
        });

        expect(JSON.parse(formData.get('protocol_cfg_header')).name).toBe('接口1');
        expect(JSON.parse(formData.get('protocol_req_cfg')).path).toBe('/api/test');
        expect(formData.get('protocol_req_body')).toBe('{"a":1}');
        expect(formData.has('protocol_resp_body')).toBe(true);
        expect(formData.get('protocol_resp_body')).toBe('');
    });

    /**
     * 测试思路：后端新增协议 multipart 解析要求 req/resp body 的 name 必须存在，即使业务上未配置 Body。
     * 示例：请求和响应 Body 都为空时，FormData 仍包含 protocol_req_body/protocol_resp_body，值为空字符串。
     */
    it('新增协议项空 Body 仍保留 multipart body 字段名', () => {
        const formData = context.KitProxy.utils.createAddProtocolFormData({
            cfg_header: {
                name: '空Body接口',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'json',
                resp_body_type: 'json',
            },
            req_cfg: {
                method: 'GET',
                path: '/api/empty',
                headers: {},
            },
            resp_cfg: {
                status_code: 200,
                headers: {},
            },
            request_body: '',
            response_body: '',
        });

        expect(formData.has('protocol_req_body')).toBe(true);
        expect(formData.has('protocol_resp_body')).toBe(true);
        expect(formData.get('protocol_req_body')).toBe('');
        expect(formData.get('protocol_resp_body')).toBe('');
    });

    it('escapeHTML 转义 HTML 特殊字符', () => {
        expect(context.KitProxy.utils.escapeHTML('<div a="1">&\'</div>'))
            .toBe('&lt;div a=&quot;1&quot;&gt;&amp;&#39;&lt;/div&gt;');
    });

    it('行内标题编辑支持保存、取消、空值、未变化和键盘操作', async () => {
        const title = context.document.createElement('span');
        title.textContent = '原名称';
        context.document.body.appendChild(title);

        const onSave = vi.fn(async value => value !== '失败名称');
        context.KitProxy.utils.bindInlineTitleEditor({
            titleElement: title,
            onSave,
        });

        title.click();
        let input = title.querySelector('.inline-title-input');
        input.value = '';
        title.nextElementSibling.querySelector('.inline-title-save').click();
        await Promise.resolve();
        expect(onSave).not.toHaveBeenCalled();
        expect(title.nextElementSibling.querySelector('.inline-title-error').textContent).toContain('不能为空');

        input.value = '原名称';
        input.dispatchEvent(new context.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
        await Promise.resolve();
        expect(onSave).not.toHaveBeenCalled();
        expect(title.textContent).toBe('原名称');

        title.click();
        input = title.querySelector('.inline-title-input');
        input.value = '新名称';
        input.dispatchEvent(new context.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
        await flushPromises();
        expect(onSave).toHaveBeenCalledWith('新名称');
        expect(title.textContent).toBe('新名称');

        title.click();
        input = title.querySelector('.inline-title-input');
        input.value = '取消名称';
        input.dispatchEvent(new context.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
        expect(title.textContent).toBe('新名称');
    });
});

describe('V1 config and API layer', () => {
    it('默认使用 real 模式', () => {
        const context = createBrowserContext('');
        loadCoreScripts(context);
        expect(context.KitProxy.config.apiMode).toBe('real');
    });

    it('URL 参数可以切到 mock 模式', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        expect(context.KitProxy.config.apiMode).toBe('mock');
        const projects = await context.KitProxy.api.getProjectList();

        expect(projects.length).toBeGreaterThan(0);
        expect(context.fetch).not.toHaveBeenCalled();
    });

    it('mock 模式新增服务后可以查询单项', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

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
        });

        await context.KitProxy.api.setProjectActive(7, false);
        expect(context.fetch).toHaveBeenLastCalledWith('/projects/7/status?operation=0', {
            method: 'POST',
        });
    });

    it('mock 服务列表支持 offset 和 limit 分页切片', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const firstPage = await context.KitProxy.api.getProjectList(0, 1);
        const secondPage = await context.KitProxy.api.getProjectList(1, 1);

        expect(firstPage).toHaveLength(1);
        expect(secondPage).toHaveLength(1);
        expect(firstPage[0].id).not.toBe(secondPage[0].id);
    });

    it('mock 协议项列表支持 projectId、offset 和 limit 分页切片', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

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

describe('V1 protocol config API', () => {
    it('保留按需查询完整协议项配置的接口能力', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const cfg = await context.KitProxy.api.getProtocolDetailsCfg(1);

        expect(cfg.req_cfg.path).toBe('/api/test1');
        expect(cfg.req_cfg.method).toBe('GET');
    });
});

describe('V1.2 pagination and protocol registry', () => {
    it('pageSize + 1 截断后能判断是否还有下一页', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const state = context.KitProxy.pagination.createState(2);
        const visibleItems = context.KitProxy.pagination.takeVisibleItems([1, 2, 3], state);

        expect(visibleItems).toEqual([1, 2]);
        expect(state.hasMore).toBe(true);
        expect(context.KitProxy.pagination.getRequestLimit(state)).toBe(3);
    });

    it('删除当前页最后一条时按规则回退上一页', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const state = context.KitProxy.pagination.createState();
        state.currentPage = 3;

        expect(context.KitProxy.pagination.nextPageAfterDelete(state, 1)).toBe(2);
        expect(context.KitProxy.pagination.nextPageAfterDelete(state, 2)).toBe(3);
    });

    it('服务分页 pageSize 支持 5/10/20/50 且请求 limit 使用 pageSize + 1', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        [5, 10, 20, 50].forEach(pageSize => {
            const state = context.KitProxy.pagination.createState(pageSize);
            expect(state.pageSize).toBe(pageSize);
            expect(context.KitProxy.pagination.getRequestLimit(state)).toBe(pageSize + 1);
        });
    });

    it('分页条内置每页数量选择器并支持切换回调', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        const container = context.document.createElement('div');
        const state = context.KitProxy.pagination.createState(10);
        const onPageSizeChange = vi.fn();

        context.KitProxy.pagination.render(container, state, {
            pageSizeOptions: context.KitProxy.pagination.DEFAULT_PAGE_SIZE_OPTIONS,
            onPageSizeChange,
        });

        const select = container.querySelector('.pagination-page-size');
        expect(container.classList.contains('has-page-size-control')).toBe(true);
        expect(Array.from(select.options).map(option => option.value)).toEqual(['5', '10', '20', '50']);
        expect(select.value).toBe('10');

        select.value = '5';
        select.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(onPageSizeChange).toHaveBeenCalledWith(5);
    });

    it('协议类型注册表能返回新增弹窗和协议项详情网格', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/add_protocol_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));

        const modalFactory = context.ProtocolTypeRegistry.getAddProtocolModal(context.ProtocolType.HTTP);
        const grid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            id: 1,
            type: 'HTTP',
            req_cfg: {
                method: 'GET',
                path: '/api/test',
            },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
        });

        expect(modalFactory).toBeTruthy();
        expect(grid.className).toContain('http');
        expect(grid.querySelector('[data-field-name="path"] .value').textContent).toBe('/api/test');
        expect(grid.querySelector('[data-field-name="path"] .value').getAttribute('title')).toBe('/api/test');
        expect(grid.querySelector('[data-field-name="status_code"] .value').textContent).toBe('200');
    });
});

describe('V1.3 service filters and body editor', () => {
    /**
     * 测试思路：服务运行态筛选必须看 active，而 status 只代表软删除有效性。
     * 示例：status 都是 1 时，只有 active=1 的 HTTP 服务能命中“开启 + HTTP + 日期”组合。
     */
    it('服务筛选支持 active 运行态、协议种类和日期范围', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const projects = [
            { id: 1, protocol_type: 1, status: 1, active: 1, ctime: '2025-08-11 07:55:15' },
            { id: 2, protocol_type: 2, status: 1, active: 0, ctime: '2025-08-12 07:55:15' },
            { id: 3, protocol_type: 1, status: 1, active: 0, ctime: '' },
        ];

        expect(context.KitProxy.serviceFilters.apply(projects, {
            startDate: '2025-08-11',
            endDate: '2025-08-11',
            status: 'active',
            protocolType: '1',
        })).toEqual([projects[0]]);

        expect(context.KitProxy.serviceFilters.validate({
            startDate: '2025-08-12',
            endDate: '2025-08-11',
            status: 'all',
            protocolType: 'all',
        }).valid).toBe(false);
    });

    it('Body 语法校验支持 JSON、XML、Text 和空 Body', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        expect(context.KitProxy.bodySyntax.validate('{"ok": true}', 'json').valid).toBe(true);
        const jsonError = context.KitProxy.bodySyntax.validate('{\n  bad\n}', 'json');
        expect(jsonError.valid).toBe(false);
        expect(jsonError.message).toContain('第');
        expect(jsonError.message).toContain('JSON');

        expect(context.KitProxy.bodySyntax.validate('<root><a /></root>', 'xml').valid).toBe(true);
        const xmlError = context.KitProxy.bodySyntax.validate('<root><a></root>', 'xml');
        expect(xmlError.valid).toBe(false);
        expect(xmlError.message).toContain('XML');

        expect(context.KitProxy.bodySyntax.validate('plain text', 'text').valid).toBe(true);
        expect(context.KitProxy.bodySyntax.validate('', 'json').valid).toBe(true);
    });

    it('Body 输入组件能切换类型、校验并格式化', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const editor = context.KitProxy.bodyEditor.create(host, {
            value: '{"ok":true}',
            bodyType: 'json',
            allowedTypes: ['json', 'xml', 'text'],
        });

        expect(editor.validate().valid).toBe(true);
        host.querySelector('.body-editor-format').click();
        expect(editor.getValue()).toContain('\n');

        editor.setType('text');
        editor.setValue('not json');
        expect(editor.validate().valid).toBe(true);
    });

    it('新增协议项 FormData 支持 XML 和 Text Body 类型', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const formData = context.KitProxy.utils.createAddProtocolFormData({
            cfg_header: {
                name: '接口XML',
                type: 'HTTP',
                project_id: 1,
                req_body_type: 'xml',
                resp_body_type: 'text',
            },
            req_cfg: {
                method: 'POST',
                path: '/api/xml',
            },
            resp_cfg: {},
            request_body: '<root><ok /></root>',
            response_body: 'done',
        });

        expect(JSON.parse(formData.get('protocol_cfg_header')).req_body_type).toBe('xml');
        expect(formData.get('protocol_req_body')).toBe('<root><ok /></root>');
        expect(formData.get('protocol_resp_body')).toBe('done');
    });
});

describe('V1.4 TCP Pattern, Body highlight and service interactions', () => {
    let context;

    beforeEach(() => {
        context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/add_protocol_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
    });

    /**
     * 测试思路：V2 字段以 byte_pos 为唯一位置索引，不再依赖 idx。
     * 示例：byte_pos=2 的字段应排在 byte_pos=4 的字段前面，序列化后仍输出 V2 fields。
     */
    it('TCP Pattern V2 普通字段按 byte_pos 排序并输出 fields', () => {
        const editor = context.KitProxy.tcpPatternEditor;
        const sorted = editor.sortPatternFields([
            { name: 'b', byte_pos: 4, byte_len: 4, type: 'UINT32', role: 'common' },
            { name: 'a', byte_pos: 2, byte_len: 2, type: 'UINT16', role: 'common' },
        ]);
        expect(sorted.map(field => field.name)).toEqual(['a', 'b']);

        const serialized = editor.serializePatternInfo({
            version: 2,
            header_bytes: 8,
            byte_order: 'big',
            length_policy: 'no_length',
            fields: [
                { name: '起始标识', byte_pos: 0, byte_len: 1, type: 'UINT8', role: 'start_magic', match: 'H02' },
                { name: '功能码', byte_pos: 1, byte_len: 1, type: 'UINT8', role: 'function_code' },
                ...sorted,
            ],
        });
        expect(serialized.fields.map(field => field.byte_pos)).toEqual([0, 1, 2, 4]);
        expect(serialized.fields.some(field => Object.prototype.hasOwnProperty.call(field, 'idx'))).toBe(false);
    });

    /**
     * 测试思路：V2 校验应拦截缺名称、非法 byte_pos、非法 byte_len 和缺失必需角色。
     * 示例：一个空字段不能通过，也不应再出现 idx 相关错误文案。
     */
    it('TCP Pattern V2 校验拦截空名称、非法 byte_pos 和非法 byte_len', () => {
        const validation = context.KitProxy.tcpPatternEditor.validatePatternInfo({
            version: 2,
            header_bytes: 0,
            byte_order: 'big',
            length_policy: 'body_length',
            fields: [
                { name: '', byte_pos: 'x', byte_len: 0, type: '', role: 'common' },
            ],
        });

        expect(validation.valid).toBe(false);
        expect(validation.errors.join('\n')).toContain('字段名称不能为空');
        expect(validation.errors.join('\n')).toContain('Byte 起始位置');
        expect(validation.errors.join('\n')).toContain('Byte 长度');
        expect(validation.errors.join('\n')).not.toContain('idx');
    });

    /**
     * 测试思路：项目 TCP 格式提交必须是 JSON V2，不能再把 legacy 字段带到 payload。
     * 示例：start_magic 使用 match，fields 内不含 idx，根对象不含 least_byte_len/special_fields/common_fields。
     */
    it('TCP Pattern 序列化输出 JSON V2 且不包含 legacy 字段', () => {
        const serialized = context.KitProxy.tcpPatternEditor.serializePatternInfo({
            version: 2,
            header_bytes: 8,
            byte_order: 'big',
            length_policy: 'body_length',
            fields: [
                { name: '开始魔数', byte_pos: 0, byte_len: 4, type: 'UINT32', role: 'start_magic', match: 'H01020304' },
                { name: '功能码', byte_pos: 4, byte_len: 2, type: 'UINT16', role: 'function_code' },
                { name: '长度', byte_pos: 6, byte_len: 2, type: 'UINT16', role: 'body_length' },
            ],
        });

        expect(Object.keys(serialized)).toEqual(['version', 'header_bytes', 'byte_order', 'length_policy', 'fields']);
        expect(serialized.header_bytes).toBe(8);
        expect(serialized.fields[0].match).toBe('H01020304');
        expect(serialized.fields.some(field => Object.prototype.hasOwnProperty.call(field, 'idx'))).toBe(false);
        expect(serialized).not.toHaveProperty('least_byte_len');
        expect(serialized).not.toHaveProperty('special_fields');
        expect(serialized).not.toHaveProperty('common_fields');
    });

    /**
     * 测试思路：config-pattern-modal 内部承担长度策略选择和 V2 字段编辑。
     * 示例：弹窗应有 length_policy 下拉框，不再有“最小解析长度”；固定值不单开输入列，而是在操作列冒泡填写。
     */
    it('config-pattern-modal 内置长度策略、移除最小解析长度并用操作列填写固定值', () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);

        context.createCustomTcpPatternModal(target, '项目格式字段', {
            version: 2,
            header_bytes: 7,
            byte_order: 'big',
            length_policy: 'body_length',
            fields: [
                { name: '起始标识', byte_pos: 0, byte_len: 4, type: 'UINT32', role: 'start_magic', match: 'H23232323' },
                { name: '功能码', byte_pos: 4, byte_len: 2, type: 'UINT16', role: 'function_code' },
                { name: '长度', byte_pos: 6, byte_len: 1, type: 'UINT8', role: 'body_length' },
            ],
        }, null, true);

        const modal = context.document.querySelector('.config-pattern-modal');
        expect(modal.querySelector('.pattern-length-policy')).toBeTruthy();
        expect(modal.querySelector('.pattern-least-length')).toBeNull();
        expect(modal.classList.contains('is-project-pattern')).toBe(true);
        expect(modal.querySelector('.pattern-field-grid-labels').textContent).not.toContain('match');
        expect(modal.querySelector('.pattern-field-grid-labels').textContent).not.toContain('固定值');

        const firstField = modal.querySelector('.pattern-field-container');
        const byteLenInput = firstField.querySelector('.pattern-field-byte-len');
        const typeSelect = firstField.querySelector('.pattern-field-type');
        const fixedValueButton = firstField.querySelector('.pattern-fixed-value-btn');
        expect(byteLenInput.readOnly).toBe(true);
        expect(fixedValueButton.hidden).toBe(false);
        expect(firstField.querySelector('.pattern-field-value').value).toBe('H23232323');
        expect(firstField.querySelector('.pattern-hex-prefix').textContent).toBe('H');
        expect(firstField.querySelector('.pattern-value-editor-input').value).toBe('23 23 23 23');

        fixedValueButton.click();
        const popover = firstField.querySelector('.pattern-fixed-value-popover');
        expect(popover).toBeTruthy();
        expect(popover.querySelector('label').textContent).toBe('固定值');
        expect(popover.querySelector('.pattern-hex-prefix').textContent).toBe('H');
        expect(popover.querySelector('.pattern-fixed-value-hex-digits').placeholder).toBe('00 00 00 00');
        popover.querySelector('.pattern-fixed-value-hex-digits').value = '01020304FF';
        popover.querySelector('.pattern-fixed-value-hex-digits').dispatchEvent(new context.Event('input', { bubbles: true }));
        popover.querySelector('.pattern-fixed-value-save').click();
        expect(firstField.querySelector('.pattern-field-value').value).toBe('H01020304');
        expect(firstField.querySelector('.pattern-value-editor-input').value).toBe('01 02 03 04');
        expect(firstField.querySelector('.pattern-fixed-value-popover')).toBeNull();

        typeSelect.value = 'UINT16';
        typeSelect.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(byteLenInput.value).toBe('2');
    });

    /**
     * 测试思路：字节布局预览只显示字段名称、类型和长度，角色通过颜色区分。
     * 示例：start_magic/function_code/body_length 三种角色分别带不同 role-* class，不直接显示 role 字符串。
     */
    it('字节布局预览隐藏 role 文本并按角色添加颜色 class', () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);

        context.createCustomTcpPatternModal(target, '项目格式字段', {
            version: 2,
            header_bytes: 7,
            byte_order: 'big',
            length_policy: 'body_length',
            fields: [
                { name: '起始标识', byte_pos: 0, byte_len: 4, type: 'UINT32', role: 'start_magic', match: 'H23232323' },
                { name: '功能码', byte_pos: 4, byte_len: 2, type: 'UINT16', role: 'function_code' },
                { name: '长度', byte_pos: 6, byte_len: 1, type: 'UINT8', role: 'body_length' },
            ],
        }, null, true);

        const blocks = Array.from(context.document.querySelectorAll('.pattern-byte-block'));
        expect(blocks[0].classList.contains('role-start-magic')).toBe(true);
        expect(blocks[1].classList.contains('role-function-code')).toBe(true);
        expect(blocks[2].classList.contains('role-body-length')).toBe(true);
        expect(blocks.map(block => block.textContent).join('\n')).not.toContain('start_magic');
        expect(blocks.map(block => block.textContent).join('\n')).not.toContain('function_code');
        expect(blocks.map(block => block.textContent).join('\n')).toContain('UINT32 · 4 Byte');
    });

    /**
     * 测试思路：字段值按钮只切换显示态，提交语义仍然保持 H 开头 wire hex。
     * 示例：H313233 的 STR 字段显示真值为 123，再切回时仍是 H313233。
     */
    it('pattern-field-value-display-btn 支持 STR 真值显示和切回 wire hex', () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);

        context.createCustomTcpPatternModal(target, '协议项字段', {
            version: 2,
            header_bytes: 3,
            byte_order: 'raw',
            length_policy: 'no_length',
            fields: [
                { name: '字符串字段', byte_pos: 0, byte_len: 3, type: 'STR', role: 'common', value: 'H313233' },
            ],
        }, null, false);

        const field = context.document.querySelector('.pattern-field-container');
        const input = field.querySelector('.pattern-field-value');
        const editor = field.querySelector('.pattern-value-editor-input');
        const button = field.querySelector('.pattern-field-value-display-btn');
        const byteLen = field.querySelector('.pattern-field-byte-len');

        expect(byteLen.value).toBe('3');
        expect(field.querySelector('.pattern-hex-prefix').textContent).toBe('H');
        expect(editor.placeholder).toBe('00 00 00');
        expect(editor.value).toBe('31 32 33');
        editor.value = '31323344';
        editor.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(input.value).toBe('H313233');
        expect(editor.value).toBe('31 32 33');
        expect(button.classList.contains('value-display-hex')).toBe(true);

        button.click();
        expect(button.textContent).toBe('S');
        expect(input.value).toBe('123');
        expect(editor.value).toBe('123');
        expect(button.classList.contains('value-display-str')).toBe(true);

        button.click();
        expect(button.textContent).toBe('H');
        expect(input.value).toBe('H313233');
        expect(editor.value).toBe('31 32 33');
        expect(byteLen.value).toBe('3');
    });

    it('Body 高亮生成 JSON/XML token，并对 Text 只转义', () => {
        const json = context.KitProxy.bodySyntax.highlight('{"ok": true, "n": 1, "x": null}', 'json');
        expect(json).toContain('body-token-key');
        expect(json).toContain('body-token-boolean');
        expect(json).toContain('body-token-number');
        expect(json).toContain('body-token-null');

        const xml = context.KitProxy.bodySyntax.highlight('<root id="1"><!--x--></root>', 'xml');
        expect(xml).toContain('body-token-tag');
        expect(xml).toContain('body-token-attr');
        expect(xml).toContain('body-token-comment');

        const text = context.KitProxy.bodySyntax.highlight('<script>alert(1)</script>', 'text');
        expect(text).toBe('&lt;script&gt;alert(1)&lt;/script&gt;');
        expect(text).not.toContain('body-token-tag');
    });

    it('Body 编辑器使用高亮层并保持协议 Body textarea id', () => {
        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const editor = context.KitProxy.bodyEditor.create(host, {
            idPrefix: 'protocol-body',
            value: '{"ok": true}',
            bodyType: 'json',
        });

        expect(host.querySelector('#protocol-body-content')).toBeTruthy();
        expect(host.querySelector('.body-editor-highlight-code').innerHTML).toContain('body-token-key');

        editor.setValue('x'.repeat(context.KitProxy.bodySyntax.HIGHLIGHT_SIZE_LIMIT + 1));
        expect(host.querySelector('.body-editor').classList.contains('is-highlight-disabled')).toBe(true);
    });

    it('Body 编辑器使用统一滚动容器承载行号、输入层和高亮层', () => {
        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const editor = context.KitProxy.bodyEditor.create(host, {
            idPrefix: 'protocol-body',
            value: '第一行\n第二行\n第三行',
            bodyType: 'text',
        });
        const css = fs.readFileSync(path.join(repoRoot, 'css/main.css'), 'utf8');

        expect(host.querySelector('.body-editor-input-wrap .body-editor-scroll-content')).toBeTruthy();
        expect(host.querySelector('.body-editor-code-wrap .body-editor-highlight')).toBeTruthy();
        expect(host.querySelector('.body-editor-code-wrap .body-editor-textarea')).toBeTruthy();
        expect(host.querySelector('.body-editor-textarea').getAttribute('wrap')).toBe('off');
        expect(host.querySelectorAll('.body-editor-line-number')).toHaveLength(3);

        editor.setValue('a\nb\nc\nd');
        expect(host.querySelectorAll('.body-editor-line-number')).toHaveLength(4);
        expect(host.querySelector('.body-editor').style.getPropertyValue('--body-editor-content-height')).toBe('104px');
        expect(host.querySelector('.body-editor').style.getPropertyValue('--body-editor-code-min-width')).toBe('4ch');

        editor.setValue('中文\nabc');
        expect(host.querySelector('.body-editor').style.getPropertyValue('--body-editor-code-min-width')).toBe('7ch');

        expect(css).toContain('.body-editor-lines');
        expect(css).toContain('.body-editor-scroll-content');
        expect(css).toContain('position: sticky;');
        expect(css).toContain('line-height: 20px;');
        expect(css).toContain('overflow: auto;');
        expect(css).toContain('overflow: hidden;');
        expect(css).toContain('.body-editor-highlight-code');
        expect(css).toContain('line-height: inherit;');
    });

    it('Body 导入弹窗使用 Body Editor 控件而不是普通 textarea', () => {
        context.KitProxy.utils.createTextImportModal({
            title: '导入响应Body内容',
            value: '{"ok": true}',
            bodyType: 'json',
            useBodyEditor: true,
            modalClassName: 'add-protocol-item-modal import-modal',
        });

        const modal = context.document.querySelector('.import-modal');
        expect(modal.querySelector('.body-editor-host')).toBeTruthy();
        expect(modal.querySelector('.body-editor-textarea')).toBeTruthy();
        expect(modal.querySelector('.body-editor-highlight-code').innerHTML).toContain('body-token-key');
    });

    it('服务卡片仅查看协议项按钮触发跳转', () => {
        const project = {
            id: 1,
            name: '服务1',
            protocol_type: context.ProtocolType.HTTP,
            listen_port: 18080,
            mode: context.ProjectMode.SERVER,
            status: 1,
            ctime: '2025-08-11 07:55:15',
        };

        const originalHref = context.window.location.href;
        const card = context.addServiceCard(project);
        let navigatedUrl = '';
        card.addEventListener('service-card:navigate-protocol-items', event => {
            event.preventDefault();
            navigatedUrl = event.detail.url;
        });
        card.dispatchEvent(new context.MouseEvent('click', { bubbles: true }));
        expect(context.window.location.href).toBe(originalHref);

        card.querySelector('.service-title').click();
        expect(context.window.location.href).toBe(originalHref);

        card.querySelector('.view-protocols-btn').click();
        expect(card.dataset.protocolItemsUrl).toBe('protocol_items.html?apiMode=mock&projectId=1');
        expect(navigatedUrl).toBe('protocol_items.html?apiMode=mock&projectId=1');
    });

    /**
     * 测试思路：服务卡片状态按钮应调用 setProjectActive，并用返回端口刷新卡片。
     * 示例：active=0 的服务器模式服务点击后变为“开启”，监听端口显示 Mock 返回值。
     */
    it('服务卡片状态开关调用 setProjectActive 并刷新 active 与端口', async () => {
        const project = {
            id: 9,
            name: '待启动服务',
            protocol_type: context.ProtocolType.HTTP,
            listen_port: 0,
            mode: context.ProjectMode.SERVER,
            status: 1,
            active: 0,
            ctime: '2025-08-11 07:55:15',
        };
        const setActive = vi.spyOn(context.KitProxy.api, 'setProjectActive')
            .mockResolvedValue({ active: 1, listen_port: 39009 });

        const card = context.addServiceCard(project);
        card.querySelector('.service-active-toggle').click();
        await flushPromises(8);

        expect(setActive).toHaveBeenCalledWith(9, true);
        expect(card.querySelector('.project-status .field-value').textContent).toBe('开启');
        expect(card.querySelector('.project-listen-port .field-value').textContent).toBe('39009');
    });
});

describe('V1.5 protocol item form page and compact cards', () => {
    it('协议项页添加按钮跳转到独立表单页', async () => {
        const dom = new JSDOM(`<!doctype html><html><body>
            <button id="add-protocol-item" disabled>添加协议项</button>
            <a id="back-service-list"></a>
            <h2 id="protocol-items-title"></h2>
            <div id="protocol-page-error"></div>
            <div class="protocol-items-page">
                <div id="protocol-service-meta"></div>
                <div class="protocol-list"></div>
                <div id="protocol-pagination"></div>
            </div>
        </body></html>`, {
            url: 'http://localhost/html/protocol_items.html?apiMode=mock&projectId=1',
        });
        const context = vm.createContext(dom.window);
        context.console = console;
        context.fetch = vi.fn();
        context.TextEncoder = TextEncoder;
        context.TextDecoder = TextDecoder;

        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);

        context.KitProxy.protocolItemsPage.initPage?.();
        context.document.dispatchEvent(new context.Event('DOMContentLoaded'));
        await flushPromises(8);

        const addButton = context.document.getElementById('add-protocol-item');
        let targetUrl = '';
        addButton.addEventListener('protocol-items:navigate-create', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        addButton.click();

        expect(addButton.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1');
    });

    /**
     * 测试思路：active 只表示服务运行态，不应阻止协议项配置维护。
     * 示例：Mock projectId=2 active=0，初始化后新增按钮仍可进入协议项表单。
     */
    it('协议项管理页未开启服务时仍允许新增协议项', async () => {
        const dom = new JSDOM(`<!doctype html><html><body>
            <button id="add-protocol-item" disabled>添加协议项</button>
            <a id="back-service-list"></a>
            <h2 id="protocol-items-title"></h2>
            <div id="protocol-page-error"></div>
            <div class="protocol-items-page">
                <div id="protocol-service-meta"></div>
                <div class="protocol-list"></div>
                <div id="protocol-pagination"></div>
            </div>
        </body></html>`, {
            url: 'http://localhost/html/protocol_items.html?apiMode=mock&projectId=2',
        });
        const context = vm.createContext(dom.window);
        context.console = console;
        context.fetch = vi.fn();
        context.TextEncoder = TextEncoder;
        context.TextDecoder = TextDecoder;

        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);

        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const addButton = context.document.getElementById('add-protocol-item');
        let targetUrl = '';
        addButton.addEventListener('protocol-items:navigate-create', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        addButton.click();

        expect(addButton.disabled).toBe(false);
        expect(addButton.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=2');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=2');
        expect(context.document.querySelector('#protocol-service-meta .status-inactive').textContent.trim()).toBe('未开启');
    });

    it('协议项管理页分页条支持每页数量切换', async () => {
        const dom = new JSDOM(`<!doctype html><html><body>
            <button id="add-protocol-item" disabled>添加协议项</button>
            <a id="back-service-list"></a>
            <h2 id="protocol-items-title"></h2>
            <div id="protocol-page-error"></div>
            <div class="protocol-items-page">
                <div id="protocol-service-meta"></div>
                <div class="protocol-list"></div>
                <div id="protocol-pagination" class="pagination-bar"></div>
            </div>
        </body></html>`, {
            url: 'http://localhost/html/protocol_items.html?apiMode=mock&projectId=1',
        });
        const context = vm.createContext(dom.window);
        context.console = console;
        context.fetch = vi.fn();
        context.TextEncoder = TextEncoder;
        context.TextDecoder = TextDecoder;

        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        loadProtocolListScripts(context);

        const getProtocolList = vi.spyOn(context.KitProxy.api, 'getProtocolList');
        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const select = context.document.querySelector('#protocol-pagination .pagination-page-size');
        expect(select).toBeTruthy();
        expect(Array.from(select.options).map(option => option.value)).toEqual(['5', '10', '20', '50']);

        select.value = '5';
        select.dispatchEvent(new context.Event('change', { bubbles: true }));
        await flushPromises(12);

        expect(context.KitProxy.protocolItemsPage.pageState.pageSize).toBe(5);
        expect(getProtocolList).toHaveBeenLastCalledWith(1, 0, 6);
    });

    it('协议项卡片默认折叠，支持展开收起和修改跳转', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');

        const root = context.document.createElement('div');
        root.className = 'protocol-items-page';
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        const details = protocolItem.querySelector('.protocol-details');
        const toggleButton = protocolItem.querySelector('.protocol-toggle-btn');
        expect(details.classList.contains('is-expanded')).toBe(false);
        expect(toggleButton.querySelector('.protocol-toggle-icon')).toBeTruthy();
        expect(toggleButton.getAttribute('aria-label')).toBe('展开协议项详情');
        expect(toggleButton.getAttribute('aria-expanded')).toBe('false');
        expect(protocolItem.querySelector('.delete-protocol-btn')).toBeTruthy();
        expect(fs.readFileSync(path.join(repoRoot, 'css/main.css'), 'utf8')).toContain('chevron-down.svg');
        expect(fs.existsSync(path.join(repoRoot, 'assets/icons/chevron-down.svg'))).toBe(true);

        toggleButton.click();
        expect(details.classList.contains('is-expanded')).toBe(true);
        expect(toggleButton.getAttribute('aria-label')).toBe('收起协议项详情');
        expect(toggleButton.getAttribute('aria-expanded')).toBe('true');

        toggleButton.click();
        expect(details.classList.contains('is-expanded')).toBe(false);
        expect(toggleButton.getAttribute('aria-expanded')).toBe('false');

        let targetUrl = '';
        protocolItem.addEventListener('protocol-item:navigate-form', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        protocolItem.querySelector('.edit-protocol-btn').click();
        expect(protocolItem.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1&protocolId=1');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1&protocolId=1');
    });

    it('协议项详情网格本身不附加编辑 modal 副作用', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=2');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));

        const httpGrid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test' },
            resp_cfg: {},
            req_body_status: 1,
            resp_body_status: 0,
        });
        context.document.body.appendChild(httpGrid);
        httpGrid.querySelector('[data-field-name="method"]').click();
        httpGrid.querySelector('[data-field-name="path"]').click();
        httpGrid.querySelector('.request-body').click();
        expect(context.document.querySelector('.modal-overlay')).toBeNull();

        const tcpGrid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            id: 2,
            type: 'TCP',
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000001' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: {},
            },
            req_body_status: 0,
            resp_body_status: 0,
        });
        context.document.body.appendChild(tcpGrid);
        expect(tcpGrid.querySelector('[data-field-name="function_code"]')).toBeNull();
        expect(tcpGrid.textContent).toContain('请求头部字段值');
        expect(tcpGrid.textContent).toContain('响应头部字段值');
        expect(tcpGrid.querySelectorAll('[data-field-name="fields"]').length).toBe(2);
        tcpGrid.querySelector('[data-field-name="fields"]').click();
        expect(context.document.querySelector('.modal-overlay')).toBeNull();
    });

    it('协议项 Body 字段点击会重新弹出编辑 modal', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 1,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        protocolItem.querySelector('.request-body').click();
        await flushPromises(12);

        expect(context.document.querySelector('.edit-body-modal')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-textarea')).toBeTruthy();
    });

    it('协议项 HTTP method/path/status 字段点击使用行内编辑', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
        context.alert = vi.fn();

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: { status_code: 200 },
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        protocolItem.querySelector('[data-field-name="method"]').click();
        expect(context.document.querySelector('.edit-method-modal')).toBeNull();
        expect(protocolItem.querySelector('[data-field-name="method"] .inline-field-editor')).toBeTruthy();
        protocolItem.querySelector('[data-field-name="method"] .inline-field-control').value = 'POST';
        protocolItem.querySelector('[data-field-name="method"] .inline-field-save').click();
        await flushPromises(8);
        expect(protocolItem.querySelector('[data-field-name="method"] .value').textContent).toBe('POST');

        protocolItem.querySelector('[data-field-name="path"]').click();
        expect(context.document.querySelector('.edit-path-modal')).toBeNull();
        protocolItem.querySelector('[data-field-name="path"] .inline-field-control').value = '/api/changed';
        protocolItem.querySelector('[data-field-name="path"] .inline-field-save').click();
        await flushPromises(8);
        expect(protocolItem.querySelector('[data-field-name="path"] .value').textContent).toBe('/api/changed');
        expect(protocolItem.querySelector('[data-field-name="path"] .value').getAttribute('title')).toBe('/api/changed');

        protocolItem.querySelector('[data-field-name="status_code"]').click();
        protocolItem.querySelector('[data-field-name="status_code"] .inline-field-control').value = '201';
        protocolItem.querySelector('[data-field-name="status_code"] .inline-field-save').click();
        await flushPromises(8);
        expect(protocolItem.querySelector('[data-field-name="status_code"] .value').textContent).toBe('201');
    });

    it('协议项未开启服务时仍允许配置 TCP 头部字段值', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=2');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
        context.alert = vi.fn();
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        const updateCfg = vi.spyOn(context.KitProxy.api, 'updateProtocolCfg');
        vi.spyOn(context.KitProxy.api, 'getProtocolDetailsCfg').mockResolvedValue({
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000001' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: {},
            },
        });

        const root = context.document.createElement('div');
        root.id = 'service-card-2';
        root.dataset.active = '0';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 3,
            name: 'TCP接口',
            project_id: 2,
            type: 'TCP',
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000001' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: {},
            },
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        expect(protocolItem.querySelector('[data-field-name="function_code"]')).toBeNull();
        expect(protocolItem.querySelector('[data-field-name="fields"] .value').textContent).toBe('已设置 2 个');

        protocolItem.querySelector('[data-field-name="fields"]').click();
        await flushPromises(12);
        const modal = context.document.querySelector('.config-pattern-modal');
        expect(modal).toBeTruthy();
        expect(modal.querySelector('.pattern-field-info-header').textContent).toContain('请求头部字段值');

        const fieldNodes = Array.from(modal.querySelectorAll('.pattern-field-container'));
        const functionNode = fieldNodes.find(node => node.querySelector('.pattern-field-role')?.value === 'function_code');
        const commonNode = fieldNodes.find(node => Number(node.querySelector('.pattern-field-byte-pos')?.value) === 4);
        const startNode = fieldNodes.find(node => node.querySelector('.pattern-field-role')?.value === 'start_magic');
        const lengthNode = fieldNodes.find(node => node.querySelector('.pattern-field-role')?.value === 'body_length');
        expect(functionNode).toBeTruthy();
        expect(commonNode).toBeTruthy();
        expect(startNode).toBeTruthy();
        expect(lengthNode).toBeTruthy();
        expect(startNode.querySelector('.pattern-value-editor-input').disabled).toBe(true);
        expect(lengthNode.querySelector('.pattern-value-editor-input').disabled).toBe(true);
        expect(functionNode.querySelector('.pattern-value-editor-input').disabled).toBe(false);
        expect(commonNode.querySelector('.pattern-value-editor-input').disabled).toBe(false);
        const previewText = Array.from(modal.querySelectorAll('.pattern-byte-block'))
            .map(block => block.textContent)
            .join('\n');
        expect(previewText).toContain('H23232323');
        expect(previewText).toContain('H1000');
        expect(previewText).toContain('H00000001');
        expect(previewText).not.toContain('UINT32 · 4 Byte');

        functionNode.querySelector('.pattern-value-editor-input').value = '';
        functionNode.querySelector('.pattern-value-editor-input').dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(modal.querySelector('.pattern-summary-item.is-error').textContent).toContain('校验状态');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toContain('功能码必须配置');

        functionNode.querySelector('.pattern-value-editor-input').value = '2000';
        functionNode.querySelector('.pattern-value-editor-input').dispatchEvent(new context.Event('input', { bubbles: true }));
        commonNode.querySelector('.pattern-value-editor-input').value = '00000002';
        commonNode.querySelector('.pattern-value-editor-input').dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(modal.querySelector('.pattern-summary-item.is-ok').textContent).toContain('校验状态');
        expect(modal.querySelector('.pattern-validation-errors').style.display).toBe('none');
        modal.querySelector('#config-pattern-modal-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(12);

        expect(updateCfg).toHaveBeenCalledWith(3, 2, 1, {
            function_code: 'H2000',
            fields: {
                4: 'H00000002',
            },
        });
        expect(protocolItem.querySelector('[data-field-name="fields"] .value').textContent).toBe('已设置 2 个');
        expect(context.alert).not.toHaveBeenCalledWith('请先开启测试服务，再执行该操作');
    });

    it('表单页新增 HTTP payload 与 Body 切换状态保持一致', () => {
        const dom = new JSDOM(`<!doctype html><html><body>
            <a id="back-protocol-list"></a>
            <h2 id="protocol-form-title"></h2>
            <p id="protocol-form-subtitle"></p>
            <div id="protocol-form-error"></div>
            <div id="protocol-form-project-context"></div>
            <form id="protocol-item-form">
                <input id="protocol-item-name">
                <div id="protocol-type-fields"></div>
                <button type="button" class="body-switch-btn is-active" data-body-tab="request">校验请求Body</button>
                <button type="button" class="body-switch-btn" data-body-tab="response">目标响应Body</button>
                <div id="protocol-body-editor-host"></div>
                <button id="save-protocol-form" type="submit"></button>
                <button id="cancel-protocol-form" type="button"></button>
            </form>
        </body></html>`, {
            url: 'http://localhost/html/protocol_item_form.html?apiMode=mock&projectId=1',
        });
        const context = vm.createContext(dom.window);
        context.console = console;
        context.fetch = vi.fn();
        context.TextEncoder = TextEncoder;
        context.TextDecoder = TextDecoder;

        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const state = context.KitProxy.protocolItemForm.pageState;
        state.projectId = 1;
        state.project = context.KitProxy.mocks.state.projects.find(project => project.id === 1);
        state.protocolType = context.ProtocolType.HTTP;
        context.KitProxy.protocolItemForm.initPage();

        return flushPromises(8).then(() => {
            context.document.getElementById('protocol-item-name').value = '新增HTTP';
            context.document.querySelector('input[name="request-method"][value="POST"]').checked = true;
            context.document.getElementById('request-path').value = '/api/new';
            expect(context.document.querySelectorAll('.protocol-config-card')).toHaveLength(2);

            context.document.getElementById('req-http-headers').click();
            let headersModal = context.document.querySelector('.http-headers-modal');
            expect(headersModal).toBeTruthy();
            expect(headersModal.querySelector('.modal-header').textContent).toContain('配置请求 Headers');
            headersModal.querySelector('.add-http-header-btn').click();
            headersModal.querySelector('.http-header-name').value = 'X-Req';
            headersModal.querySelector('.http-header-value').value = '1';
            headersModal.querySelector('.confirm-btn').click();
            expect(context.document.querySelector('.http-headers-modal')).toBeNull();

            context.document.getElementById('resp-http-headers').click();
            headersModal = context.document.querySelector('.http-headers-modal');
            expect(headersModal).toBeTruthy();
            expect(headersModal.querySelector('.modal-header').textContent).toContain('配置响应 Headers');
            headersModal.querySelector('.add-http-header-btn').click();
            headersModal.querySelector('.http-header-name').value = 'X-Resp';
            headersModal.querySelector('.http-header-value').value = '2';
            headersModal.querySelector('.confirm-btn').click();
            expect(context.document.querySelector('.http-headers-modal')).toBeNull();

            const editor = state.bodyEditor;
            editor.setValue('{"req":true}');
            context.KitProxy.protocolItemForm.setActiveBodyTab('response');
            editor.setType('text');
            editor.setValue('ok');

            const data = context.KitProxy.protocolItemForm.collectFormData();
            const payload = context.KitProxy.protocolItemForm.buildAddPayload(data);

            expect(payload.cfg_header.type).toBe('HTTP');
            expect(payload.req_cfg).toEqual({
                method: 'POST',
                path: '/api/new',
                headers: { 'X-Req': '1' },
            });
            expect(payload.resp_cfg).toEqual({
                status_code: '200',
                headers: { 'X-Resp': '2' },
            });
            expect(payload.cfg_header.req_body_type).toBe('json');
            expect(payload.cfg_header.resp_body_type).toBe('text');
            expect(payload.request_body).toBe('{"req":true}');
            expect(payload.response_body).toBe('ok');
        });
    });

    /**
     * 测试思路：TCP 协议项表单提交新结构，只输出 { function_code, fields }。
     * 示例：功能码在“头部字段值”中填写为 H1000，普通字段 byte_pos=4 的值为 H00000209，payload 不应含旧 function_code_filed_value/common_fields。
     */
    it('表单页新增 TCP payload 输出 function_code 和 fields', async () => {
        const dom = new JSDOM(`<!doctype html><html><body>
            <a id="back-protocol-list"></a>
            <h2 id="protocol-form-title"></h2>
            <p id="protocol-form-subtitle"></p>
            <div id="protocol-form-error"></div>
            <div id="protocol-form-project-context"></div>
            <form id="protocol-item-form">
                <input id="protocol-item-name">
                <div id="protocol-type-fields"></div>
                <button type="button" class="body-switch-btn is-active" data-body-tab="request">校验请求Body</button>
                <button type="button" class="body-switch-btn" data-body-tab="response">目标响应Body</button>
                <div id="protocol-body-editor-host"></div>
                <button id="save-protocol-form" type="submit"></button>
                <button id="cancel-protocol-form" type="button"></button>
            </form>
        </body></html>`, {
            url: 'http://localhost/html/protocol_item_form.html?apiMode=mock&projectId=2',
        });
        const context = vm.createContext(dom.window);
        context.console = console;
        context.fetch = vi.fn();
        context.TextEncoder = TextEncoder;
        context.TextDecoder = TextDecoder;

        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        const state = context.KitProxy.protocolItemForm.pageState;
        context.document.getElementById('protocol-item-name').value = '新增TCP';
        expect(context.document.getElementById('req-function-code-value')).toBeNull();
        expect(context.document.getElementById('resp-function-code-value')).toBeNull();
        expect(context.document.getElementById('protocol-type-fields').textContent).toContain('校验请求头部字段值');
        expect(context.document.getElementById('protocol-type-fields').textContent).toContain('目标响应头部字段值');

        const reqFunctionField = state.reqPatternFields.find(field => field.role === 'function_code');
        const respFunctionField = state.respPatternFields.find(field => field.role === 'function_code');
        const reqCommonField = state.reqPatternFields.find(field => field.role === 'common' && Number(field.byte_pos) === 4);
        const reqStartField = state.reqPatternFields.find(field => field.role === 'start_magic');
        const reqLengthField = state.reqPatternFields.find(field => field.role === 'body_length');
        expect(reqFunctionField).toBeTruthy();
        expect(respFunctionField).toBeTruthy();
        expect(reqCommonField).toBeTruthy();
        expect(reqStartField.value).toBe('H23232323');
        expect(reqStartField.value_editable).toBe(false);
        expect(reqLengthField.value_editable).toBe(false);

        reqFunctionField.value = 'H1000';
        respFunctionField.value = 'H1080';
        reqCommonField.value = 'H00000209';

        const data = context.KitProxy.protocolItemForm.collectFormData();
        const payload = context.KitProxy.protocolItemForm.buildAddPayload(data);

        expect(payload.cfg_header.type).toBe('TCP');
        expect(payload.req_cfg).toEqual({
            function_code: 'H1000',
            fields: { 4: 'H00000209' },
        });
        expect(payload.resp_cfg).toEqual({
            function_code: 'H1080',
            fields: {},
        });
        expect(payload.req_cfg).not.toHaveProperty('function_code_filed_value');
        expect(payload.req_cfg).not.toHaveProperty('common_fields');
    });

    /**
     * 测试思路：TCP item cfg 校验必须按项目 Pattern 中功能码和普通字段 byte_len 执行。
     * 示例：功能码少 1 字节、普通字段 byte_pos=4 少 1 字节，都应失败。
     */
    it('TCP item cfg 校验拦截功能码和普通字段长度不匹配', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        runScript(context, 'js/tcp_pattern_modal.js');
        const patternInfo = context.KitProxy.mocks.state.patternInfos[2];

        const validation = context.KitProxy.tcpPatternEditor.validateTcpItemCfg(patternInfo, {
            function_code: 'H10',
            fields: { 4: 'H0209' },
        });

        expect(validation.valid).toBe(false);
        expect(validation.errors.join('\n')).toContain('功能码字节数必须等于 2');
        expect(validation.errors.join('\n')).toContain('字段值字节数必须等于 4');
    });

    it('表单页编辑模式只提交变更字段', async () => {
        const dom = new JSDOM(`<!doctype html><html><body>
            <a id="back-protocol-list"></a>
            <h2 id="protocol-form-title"></h2>
            <p id="protocol-form-subtitle"></p>
            <div id="protocol-form-error"></div>
            <div id="protocol-form-project-context"></div>
            <form id="protocol-item-form">
                <input id="protocol-item-name">
                <div id="protocol-type-fields"></div>
                <button type="button" class="body-switch-btn is-active" data-body-tab="request">校验请求Body</button>
                <button type="button" class="body-switch-btn" data-body-tab="response">目标响应Body</button>
                <div id="protocol-body-editor-host"></div>
                <button id="save-protocol-form" type="submit"></button>
                <button id="cancel-protocol-form" type="button"></button>
            </form>
        </body></html>`, {
            url: 'http://localhost/html/protocol_item_form.html?apiMode=mock&projectId=1&protocolId=1',
        });
        const context = vm.createContext(dom.window);
        context.console = console;
        context.fetch = vi.fn();
        context.TextEncoder = TextEncoder;
        context.TextDecoder = TextDecoder;

        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const updateName = vi.spyOn(context.KitProxy.api, 'updateProtocolName');
        const updateCfg = vi.spyOn(context.KitProxy.api, 'updateProtocolCfg');
        const updateBody = vi.spyOn(context.KitProxy.api, 'updateProtocolBody');

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        context.document.addEventListener('protocol-item-form:navigate-back', event => {
            event.preventDefault();
        });

        context.document.getElementById('protocol-item-name').value = 'test1修改';
        context.document.getElementById('request-path').value = '/api/changed';
        context.document.getElementById('protocol-item-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(12);

        expect(updateName).toHaveBeenCalledWith(1, 'test1修改');
        expect(updateCfg).toHaveBeenCalledWith(1, 1, 1, {
            method: 'GET',
            path: '/api/changed',
            headers: {},
        });
        expect(updateCfg).toHaveBeenCalledTimes(1);
        expect(updateBody).not.toHaveBeenCalled();
    });
});
