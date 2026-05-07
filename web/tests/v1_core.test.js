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

    it('服务分页 pageSize 支持 10/20/50 且请求 limit 使用 pageSize + 1', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        [10, 20, 50].forEach(pageSize => {
            const state = context.KitProxy.pagination.createState(pageSize);
            expect(state.pageSize).toBe(pageSize);
            expect(context.KitProxy.pagination.getRequestLimit(state)).toBe(pageSize + 1);
        });
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

    it('TCP Pattern 普通字段能排序并自动计算 byte_pos', () => {
        const editor = context.KitProxy.tcpPatternEditor;
        const sorted = editor.sortPatternFields([
            { name: 'b', idx: 2, byte_len: 4, type: 'UINT32' },
            { name: 'a', idx: 1, byte_len: 2, type: 'UINT16' },
        ]);
        expect(sorted.map(field => field.name)).toEqual(['a', 'b']);

        const serialized = editor.serializePatternInfo({
            least_byte_len: 0,
            special_fields: {},
            common_fields: sorted,
        });
        expect(serialized.common_fields.map(field => field.byte_pos)).toEqual([0, 2]);
    });

    it('TCP Pattern 校验拦截空名称、非法 byte_len 和非法 idx', () => {
        const validation = context.KitProxy.tcpPatternEditor.validatePatternInfo({
            least_byte_len: 0,
            special_fields: {},
            common_fields: [
                { name: '', idx: 'x', byte_len: 0, type: '' },
            ],
        });

        expect(validation.valid).toBe(false);
        expect(validation.errors.join('\n')).toContain('字段名称不能为空');
        expect(validation.errors.join('\n')).toContain('idx');
        expect(validation.errors.join('\n')).toContain('Byte 长度');
    });

    it('TCP Pattern 序列化保持 least_byte_len/special_fields/common_fields 结构', () => {
        const serialized = context.KitProxy.tcpPatternEditor.serializePatternInfo({
            least_byte_len: 8,
            special_fields: {
                start_magic_num_field: {
                    name: '开始魔数',
                    idx: 0,
                    byte_pos: 0,
                    byte_len: 4,
                    type: 'UINT32',
                    value: 'H01020304',
                },
            },
            common_fields: [
                { name: '字段1', idx: 0, byte_len: 2, type: 'UINT16', value: '' },
            ],
        });

        expect(Object.keys(serialized)).toEqual(['least_byte_len', 'special_fields', 'common_fields']);
        expect(serialized.least_byte_len).toBe(8);
        expect(serialized.special_fields.start_magic_num_field.byte_pos).toBe(0);
        expect(serialized.common_fields[0].byte_pos).toBe(0);
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
            pattern_type: 0,
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
});
