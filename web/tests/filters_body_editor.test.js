import { describe, expect, it } from 'vitest';
import { createBrowserContext, loadCoreScripts, runScript } from './helpers/browser_context.js';

function readFormDataValueAsText(context, value) {
    if (typeof value === 'string') return Promise.resolve(value);

    return new Promise((resolve, reject) => {
        const reader = new context.FileReader();
        reader.onload = () => resolve(String(reader.result || ''));
        reader.onerror = () => reject(reader.error);
        reader.readAsText(value);
    });
}

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
     * 测试思路：请求侧和响应侧 Body 类型保持一致，避免响应侧缺少 None/Empty/Image/Binary。
     * 示例：两侧都包含 None/Empty/JSON/XML/Text/Image/Binary，Binary 可选。
     */
    it('请求侧和响应侧 Body 类型选项保持一致', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        // 这里直接运行脚本是为了拿到协议类型注册表，避免只验证 BodyEditor 默认列表。
        ['js/tcp_pattern_modal.js', 'js/protocol_item.js', 'js/protocol_registry.js']
            .forEach(filePath => runScript(context, filePath));

        const requestOptions = context.KitProxy.protocolTypes.getRequestBodyTypeOptions(context.ProtocolType.HTTP);
        const responseOptions = context.KitProxy.protocolTypes.getResponseBodyTypeOptions(context.ProtocolType.HTTP);

        expect(requestOptions.map(option => option.value)).toEqual([
            'none',
            'empty',
            'json',
            'xml',
            'text',
            'image',
            'binary',
        ]);
        expect(requestOptions.find(option => option.value === 'binary').enabled).toBe(true);
        expect(responseOptions.map(option => option.value)).toEqual(requestOptions.map(option => option.value));
        expect(responseOptions.find(option => option.value === 'binary').enabled).toBe(true);
        expect(responseOptions.find(option => option.value === 'none').enabled).toBe(true);
    });

    /**
     * 测试思路：BodyEditor 按 Body 类型自动折叠文本区，Binary 模式展示 TCP 风格普通字段配置。
     * 示例：None/Image 收起文本区；JSON 展开文本区；Binary 收起文本区并显示 pattern-layout-section/pattern-field-info。
     */
    it('Body 输入组件支持类型驱动折叠和 Binary 普通字段配置', async () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        runScript(context, 'js/tcp_pattern_modal.js');

        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const editor = context.KitProxy.bodyEditor.create(host, {
            value: '旧内容',
            bodyType: 'none',
            allowedTypes: [
                { value: 'none', label: 'None', enabled: true },
                { value: 'json', label: 'JSON', enabled: true },
                { value: 'image', label: 'Image', enabled: true },
                { value: 'binary', label: 'Binary', enabled: true },
            ],
            typeLabel: '期望Body类型',
            validate: context.KitProxy.bodySyntax.validateRequest,
        });

        expect(host.querySelector('.body-editor-type-label').textContent).toBe('期望Body类型');
        expect(host.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);
        expect(host.querySelector('.body-editor-format').hidden).toBe(true);
        expect(host.querySelector('.body-editor-clear').hidden).toBe(true);
        expect(editor.validate().valid).toBe(true);
        expect(context.KitProxy.bodySyntax.normalizeRequestBodyContent('旧内容', 'none')).toBe('');

        editor.setType('json');
        editor.setValue('{"ok":true}');
        expect(host.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(false);
        expect(host.querySelector('.body-editor-format').hidden).toBe(false);
        expect(editor.validate().valid).toBe(true);

        editor.setType('image');
        editor.setValue('图片占位内容');
        expect(host.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);
        expect(host.querySelector('.body-editor-format').hidden).toBe(true);
        expect(editor.getValue()).toBe('');

        editor.setType('binary');
        expect(host.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);
        expect(host.querySelector('.body-editor').classList.contains('is-binary-mode')).toBe(true);
        expect(host.querySelector('.body-editor-binary-wrap').classList.contains('is-collapsed')).toBe(false);
        expect(host.querySelector('.body-editor-binary-wrap').classList.contains('config-pattern-modal')).toBe(true);
        expect(host.querySelector('.body-editor-binary-wrap').classList.contains('body-editor-binary-pattern-scope')).toBe(true);
        expect(typeof context.KitProxy.tcpPatternEditor.createPatternFieldEditorSectionHTML).toBe('function');
        expect(host.querySelector('.body-editor-binary-wrap .pattern-layout-section')).toBeTruthy();
        expect(host.querySelector('.body-editor-binary-wrap .pattern-field-info')).toBeTruthy();
        expect(host.querySelector('.body-editor-binary-wrap .body-editor-binary-add-field')).toBeNull();
        expect(host.querySelector('.body-editor-binary-wrap .pattern-section-title').textContent).toContain('字节布局预览');
        expect(host.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).toContain('名称');
        expect(host.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).toContain('类型');
        expect(host.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).toContain('角色');
        expect(host.querySelector('.body-editor-binary-wrap .pattern-wire-hex-input')).toBeTruthy();
        expect(host.querySelector('.body-editor-binary-wrap .pattern-value-editor-input')).toBeTruthy();
        expect(host.querySelector('.body-editor-binary-wrap .pattern-field-value-display-btn')).toBeTruthy();
        const readBinaryRows = () => Array.from(host.querySelectorAll('.body-editor-binary-wrap .pattern-field-container'));
        expect(readBinaryRows()).toHaveLength(1);
        expect(readBinaryRows()[0].querySelector('.del-field-btn').disabled).toBe(true);

        readBinaryRows()[0].querySelector('.add-field-btn').click();
        expect(readBinaryRows()).toHaveLength(2);
        expect(readBinaryRows().every(field => field.querySelector('.del-field-btn').disabled === false)).toBe(true);
        const deletingField = readBinaryRows()[1];
        deletingField.querySelector('.del-field-btn').click();
        expect(deletingField.classList.contains('is-delete-marked')).toBe(true);
        expect(deletingField.classList.contains('is-deleting')).toBe(false);
        expect(readBinaryRows()[0].querySelector('.del-field-btn').disabled).toBe(true);
        await new Promise(resolve => setTimeout(resolve, 280));
        expect(deletingField.classList.contains('is-deleting')).toBe(true);
        await new Promise(resolve => setTimeout(resolve, 700));
        expect(readBinaryRows()).toHaveLength(1);
        expect(readBinaryRows()[0].querySelector('.del-field-btn').disabled).toBe(true);

        const roleSelect = host.querySelector('.body-editor-binary-wrap .pattern-field-role');
        expect(roleSelect.disabled).toBe(true);
        expect(Array.from(roleSelect.options).map(option => option.value)).toEqual(['common']);
        const typeSelect = host.querySelector('.body-editor-binary-wrap .pattern-field-type');
        typeSelect.value = 'UINT16';
        typeSelect.dispatchEvent(new context.Event('change', { bubbles: true }));
        const byteLenInput = host.querySelector('.body-editor-binary-wrap .pattern-field-byte-len');
        expect(byteLenInput.value).toBe('2');
        expect(byteLenInput.disabled).toBe(true);
        const valueEditor = host.querySelector('.body-editor-binary-wrap .pattern-value-editor-input');
        valueEditor.value = '01 02';
        valueEditor.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(editor.getBinaryFields()[0].value).toBe('H0102');
        expect(editor.getValue()).toBe('');
        expect(context.KitProxy.bodySyntax.validate('旧响应内容', 'binary').valid).toBe(true);
        expect(context.KitProxy.bodySyntax.normalizeBodyContent('旧响应内容', 'binary')).toBe('');
    });

    /**
     * 测试思路：新增协议项 payload 中的 body 类型要随用户选择写入 cfg_header。
     * 示例：请求 Body 选 xml、响应 Body 选 text 时，FormData 应保留对应类型和值。
     */
    it('新增协议项 FormData 支持 XML 和 Text Body 类型', async () => {
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

        const cfgHeaderText = await readFormDataValueAsText(context, formData.get('protocol_cfg_header'));
        const reqBodyText = await readFormDataValueAsText(context, formData.get('protocol_req_body'));
        const respBodyText = await readFormDataValueAsText(context, formData.get('protocol_resp_body'));

        expect(JSON.parse(cfgHeaderText).req_body_type).toBe('xml');
        expect(reqBodyText).toContain('<root>');
        expect(reqBodyText).toContain('<ok');
        expect(respBodyText).toBe('done');
    });
});
