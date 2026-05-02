import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { JSDOM } from 'jsdom';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '..');

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
            pattern_type: 0,
            pattern_info: {},
        });
        const projects = await context.KitProxy.api.getProject(addResult.project_id);

        expect(projects[0].name).toBe('新增服务');
        expect(projects[0].listen_port).toBe(18080);
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
    });
});

describe('V1.3 service filters and body editor', () => {
    it('服务筛选支持状态、协议种类和日期范围', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const projects = [
            { id: 1, protocol_type: 1, status: 1, ctime: '2025-08-11 07:55:15' },
            { id: 2, protocol_type: 2, status: 0, ctime: '2025-08-12 07:55:15' },
            { id: 3, protocol_type: 1, status: 0, ctime: '' },
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
