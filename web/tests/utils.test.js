import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
    createBrowserContext,
    flushPromises,
    loadCoreScripts,
    readFormDataValueAsText,
} from './helpers/browser_context.js';

describe('V1 utils', () => {
    /**
     * @type {vm.Context}
     */
    let context;

    beforeEach(() => {
        context = createBrowserContext();
        loadCoreScripts(context);
    });

    /**
     * 测试思路：页面很多 DOM id 通过末尾数字关联业务 id，工具函数必须稳定解析。
     * 示例：service-card-12 返回 12，bad-id 这种无数字结尾的字符串返回 -1。
     */
    it('解析以数字结尾的 DOM id', () => {
        expect(context.ExtractId('service-card-12')).toBe(12);
        expect(context.ExtractId('protocol-item-9')).toBe(9);
        expect(context.ExtractId('bad-id')).toBe(-1);
    });

    /**
     * Test idea: API timestamps are UTC instants, while the card must use local date getters.
     * Example: an RFC3339 UTC value is converted to local calendar fields and invalid input is rejected.
     */
    it('formats UTC timestamps in the browser timezone', () => {
        const instant = new Date('2025-08-11T07:55:15Z');
        const expected = `${instant.getFullYear()}-${String(instant.getMonth() + 1).padStart(2, '0')}-${String(instant.getDate()).padStart(2, '0')}`
            + ` ${String(instant.getHours()).padStart(2, '0')}:${String(instant.getMinutes()).padStart(2, '0')}:${String(instant.getSeconds()).padStart(2, '0')}`;

        expect(context.KitProxy.utils.formatUtcTime('2025-08-11T07:55:15Z')).toBe(expected);
        expect(context.KitProxy.utils.formatUtcTime('2025-08-11 07:55:15')).toBe('未知');
        expect(context.KitProxy.utils.formatUtcTime('')).toBe('未知');
    });

    /**
     * 测试思路：基础输入校验应拦截不合法 HTTP path 和超出范围的端口。
     * 示例：/api/test 合法，api/test 非法；端口 1-65535 合法，0 和 65536 非法。
     */
    it('校验 HTTP path 和端口范围', () => {
        expect(context.KitProxy.utils.validateHttpPath('/api/test')).toBe(true);
        expect(context.KitProxy.utils.validateHttpPath('api/test')).toBe(false);
        expect(context.KitProxy.utils.validatePort(1)).toBe(true);
        expect(context.KitProxy.utils.validatePort(65535)).toBe(true);
        expect(context.KitProxy.utils.validatePort(0)).toBe(false);
        expect(context.KitProxy.utils.validatePort(65536)).toBe(false);
    });

    /**
     * 测试思路：全局错误提示不再使用浏览器 alert 或页面内嵌错误区，而是创建顶部弹框。
     * 示例：错误文案包含 HTML 标签时，弹框应显示文本本身，不应插入可执行节点。
     */
    it('全局错误提示使用顶部弹框并转义文案', () => {
        context.KitProxy.utils.showGlobalError('<script>alert(1)</script>', {
            durationMs: 0,
        });

        const popup = context.document.querySelector('.global-error-popup');
        expect(popup).toBeTruthy();
        expect(popup.getAttribute('role')).toBe('alert');
        expect(popup.classList.contains('is-error')).toBe(true);
        expect(popup.querySelector('.global-notification-icon svg')).toBeTruthy();
        expect(popup.textContent).toContain('<script>alert(1)</script>');
        expect(popup.querySelector('script')).toBeNull();
    });

    /**
     * 测试思路：成功提示复用同一套顶部弹框，但切换为成功状态和绿色对钩图标。
     * 示例：保存成功后应创建 status 弹框，并带 is-success 状态类。
     */
    it('全局成功提示复用顶部弹框和图标结构', () => {
        context.KitProxy.utils.showGlobalSuccess('保存成功', {
            durationMs: 0,
        });

        const popup = context.document.querySelector('.global-error-popup');
        expect(popup).toBeTruthy();
        expect(popup.getAttribute('role')).toBe('status');
        expect(popup.classList.contains('is-success')).toBe(true);
        expect(popup.querySelector('.global-notification-icon svg')).toBeTruthy();
        expect(popup.textContent).toContain('保存成功');
    });

    /**
     * 测试思路：JSON 工具既要能判断文本是否合法，也要能格式化合法 JSON。
     * 示例：{"ok":true} 校验通过且格式化后有换行，{bad 校验失败。
     */
    it('校验和格式化 JSON 文本', () => {
        expect(context.KitProxy.utils.validateJsonText('{"ok":true}')).toBe(true);
        expect(context.KitProxy.utils.validateJsonText('{bad')).toBe(false);
        expect(context.KitProxy.utils.formatJsonText('{"ok":true}')).toContain('\n');
    });

    /**
     * 测试思路：新增协议项最终走 multipart FormData，工具函数要把配置和 Body 正确分栏。
     * 示例：cfg_header、req_cfg 转 JSON 字符串，request_body 原样写入 protocol_req_body。
     */
    it('构造新增协议项 FormData payload', async () => {
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

        const cfgHeader = JSON.parse(await readFormDataValueAsText(context, formData.get('protocol_cfg_header')));
        const reqCfg = JSON.parse(await readFormDataValueAsText(context, formData.get('protocol_req_cfg')));
        const requestBody = await readFormDataValueAsText(context, formData.get('protocol_req_body'));
        expect(cfgHeader.name).toBe('接口1');
        expect(reqCfg.path).toBe('/api/test');
        expect(requestBody).toBe('{"a":1}');
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

    /**
     * 测试思路：新增协议的上线态由 cfg_header.config_state 传给后端，保存和保存并上线都要能序列化。
     * 示例：config_state=0 表示只保存，config_state=1 表示保存并上线，两个值都应进入 protocol_cfg_header。
     */
    it('构造新增协议项 FormData 保留 config_state', async () => {
        for (const configState of [0, 1]) {
            const formData = context.KitProxy.utils.createAddProtocolFormData({
                cfg_header: {
                    name: '运行态协议',
                    type: 'HTTP',
                    project_id: 1,
                    req_body_type: 'json',
                    resp_body_type: 'json',
                    config_state: configState,
                },
                req_cfg: {},
                resp_cfg: {},
                request_body: '',
                response_body: '',
            });

            const cfgHeader = JSON.parse(await readFormDataValueAsText(context, formData.get('protocol_cfg_header')));
            expect(cfgHeader.config_state).toBe(configState);
        }
    });

    /**
     * 测试思路：所有插入 innerHTML 的用户文案都依赖 escapeHTML 防止标签注入。
     * 示例：包含 <、>、&、双引号、单引号的字符串应转为对应 HTML 实体。
     */
    it('escapeHTML 转义 HTML 特殊字符', () => {
        expect(context.KitProxy.utils.escapeHTML('<div a="1">&\'</div>'))
            .toBe('&lt;div a=&quot;1&quot;&gt;&amp;&#39;&lt;/div&gt;');
    });

    /**
     * 测试思路：行内标题编辑器要覆盖主要交互分支，避免名称编辑出现半提交状态。
     * 示例：空值显示错误，Enter 保存新名称，Escape 取消，未变化时不调用保存回调。
     */
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
