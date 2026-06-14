import { describe, expect, it } from 'vitest';
import { createBrowserContext, loadCoreScripts } from './helpers/browser_context.js';

describe('V1.3 service filters and body editor', () => {
    /**
     * 测试思路：服务运行态筛选必须看 runtime_state，而 status 只代表软删除有效性。
     * 示例：status 都是 1 时，只有 runtime_state=1 的 HTTP 服务能命中“开启 + HTTP + 日期”组合。
     */
    it('服务筛选支持 runtime_state 运行态、协议种类和日期范围', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const projects = [
            { id: 1, protocol_type: 1, status: 1, runtime_state: 1, ctime: '2025-08-11 07:55:15' },
            { id: 2, protocol_type: 2, status: 1, runtime_state: 0, ctime: '2025-08-12 07:55:15' },
            { id: 3, protocol_type: 1, status: 1, runtime_state: 0, ctime: '' },
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

    /**
     * 测试思路：Body 校验器需要按类型区分 JSON/XML/Text，并允许空 Body。
     * 示例：非法 JSON 应给出带行号的 JSON 错误，普通 text 则不做结构校验。
     */
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

    /**
     * 测试思路：Body Editor 应把类型切换、语法校验和格式化封装成统一控件能力。
     * 示例：JSON 格式化后出现换行，切到 text 后同样内容不再按 JSON 报错。
     */
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

    /**
     * 测试思路：新增协议项 payload 中的 body 类型要随用户选择写入 cfg_header。
     * 示例：请求 Body 选 xml、响应 Body 选 text 时，FormData 应保留对应类型和值。
     */
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
