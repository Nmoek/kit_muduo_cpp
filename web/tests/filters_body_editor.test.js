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
     * 测试思路：Project 筛选只负责把页面条件转换为后端查询字段，不再在当前页数组上执行过滤。
     * 示例：开启 + HTTP + 日期应生成 status=1、runtime_state=1、protocol_type=1 和本地日期边界。
     */
    it('服务筛选支持 runtime_state 运行态、协议种类和日期范围', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const filters = {
            startDate: '2025-08-11',
            endDate: '2025-08-11',
            status: 'active',
            protocolType: '1',
            ownerKeyword: '',
            ownerUserId: null,
            ownerNote: '',
        };
        const request = context.KitProxy.serviceFilters.toRequest(filters);

        expect(request.status).toBe(1);
        expect(request.runtime_state).toBe(1);
        expect(request.protocol_type).toBe(1);
        expect(request.created_to - request.created_from).toBe(24 * 60 * 60 * 1000);

        const timedRequest = context.KitProxy.serviceFilters.toRequest(Object.assign({}, filters, {
            startDate: '2025-08-11',
            endDate: '2025-08-12',
            startTime: '08:15:20',
            endTime: '17:20:30',
        }));
        expect(timedRequest.created_from).toBe(new Date(2025, 7, 11, 8, 15, 20).getTime());
        expect(timedRequest.created_to).toBe(new Date(2025, 7, 12, 17, 20, 31).getTime());

        expect(context.KitProxy.serviceFilters.validate({
            startDate: '2025-08-12',
            endDate: '2025-08-11',
            status: 'all',
            protocolType: 'all',
        }).valid).toBe(false);
        expect(context.KitProxy.serviceFilters.validate({
            startDate: '2025-08-11',
            endDate: '2025-08-11',
            startTime: '18:00:00',
            endTime: '17:00:00',
            status: 'all',
            protocolType: 'all',
        }).valid).toBe(false);
    });

    /**
     * 测试思路：面板中的日历和输入框只维护草稿，必须点击确定才更新已提交的范围。
     * 示例：日历点击开始和结束日期后 read() 仍为空，点击确定才得到 00:00:00 至 23:59:59。
     */
    it('起止时间控件支持日历双次点击、直接输入和默认时分秒', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        const host = context.document.createElement('div');
        host.innerHTML = `
            <div id="filter-create-time-range">
                <input id="filter-create-start" readonly>
                <input id="filter-create-start-clock" readonly>
                <input id="filter-create-end" readonly>
                <input id="filter-create-end-clock" readonly>
                <button id="filter-create-range" type="button">选择日期</button>
                <div id="filter-create-range-panel" hidden>
                    <strong id="filter-create-calendar-title"></strong>
                    <button id="filter-create-calendar-prev" type="button"></button>
                    <button id="filter-create-calendar-next" type="button"></button>
                    <div id="filter-create-calendar-days"></div>
                    <p id="filter-create-calendar-status"></p>
                    <input type="text" id="filter-create-direct-start">
                    <input type="text" id="filter-create-direct-end">
                    <input type="text" id="filter-create-start-time">
                    <input type="text" id="filter-create-end-time">
                    <div id="filter-create-range-error" hidden></div>
                    <button id="filter-create-range-reset" type="button">重置</button>
                    <button id="filter-create-range-confirm" type="button">确定</button>
                </div>
            </div>
        `;
        context.document.body.appendChild(host);
        const range = context.KitProxy.timeRangeFilter.bind(context.document, { prefix: 'filter-create' });

        const trigger = context.document.getElementById('filter-create-range');
        const panel = context.document.getElementById('filter-create-range-panel');
        const confirm = context.document.getElementById('filter-create-range-confirm');
        trigger.click();
        const calendarDays = context.document.querySelectorAll('#filter-create-calendar-days [data-date]');
        const firstDateText = calendarDays[0].dataset.date;
        const secondDateText = calendarDays[4].dataset.date;
        calendarDays[0].click();
        expect(panel.hidden).toBe(false);
        expect(context.document.getElementById('filter-create-start').value).toBe('');
        expect(context.document.getElementById('filter-create-end').value).toBe('');
        expect(context.document.getElementById('filter-create-direct-start').value).toBe(firstDateText);

        const secondDate = context.document.querySelector(`#filter-create-calendar-days [data-date="${secondDateText}"]`);
        secondDate.click();
        expect(panel.hidden).toBe(false);
        expect(context.document.querySelectorAll('#filter-create-calendar-days .is-in-range')).toHaveLength(3);
        expect(range.read()).toEqual({
            startDate: '',
            endDate: '',
            startTime: '',
            endTime: '',
        });

        confirm.click();
        expect(panel.hidden).toBe(true);

        expect(range.read()).toEqual({
            startDate: firstDateText,
            endDate: secondDateText,
            startTime: '00:00:00',
            endTime: '23:59:59',
        });
        expect(context.document.getElementById('filter-create-start-clock').value).toBe('00:00:00');
        expect(context.document.getElementById('filter-create-end-clock').value).toBe('23:59:59');

        const directStart = context.document.getElementById('filter-create-direct-start');
        const directEnd = context.document.getElementById('filter-create-direct-end');
        trigger.click();
        directStart.value = '2025-09-01';
        directStart.dispatchEvent(new context.Event('change', { bubbles: true }));
        directEnd.value = '2025-09-03';
        directEnd.dispatchEvent(new context.Event('change', { bubbles: true }));
        confirm.click();
        expect(range.read().startDate).toBe('2025-09-01');
        expect(range.read().endDate).toBe('2025-09-03');
        expect(range.read().startTime).toBe('00:00:00');
        expect(range.read().endTime).toBe('23:59:59');

        trigger.click();
        directStart.value = '20250904';
        directStart.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(directStart.value).toBe('2025-09-04');

        directStart.value = '20251340';
        directStart.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(directStart.value).toBe('2025-09-04');
        expect(directStart.getAttribute('aria-invalid')).toBe('true');

        const startTime = context.document.getElementById('filter-create-start-time');
        startTime.value = '123456';
        startTime.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(startTime.value).toBe('12:34:56');

        startTime.value = '296099';
        startTime.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(startTime.value).toBe('12:34:56');
        expect(startTime.getAttribute('aria-invalid')).toBe('true');
    });

    /**
     * 测试思路：所有者输入只是候选关键字，未点击真实候选时必须阻止查询；选中后只发送 user_id。
     * 示例：ownerKeyword=adm 且 ownerUserId=null 校验失败，选择 user_id=1 后请求不包含 note 字符串。
     */
    it('所有者筛选必须选择真实候选并转换为 user_id', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const pending = {
            status: 'all',
            protocolType: 'all',
            startDate: '',
            endDate: '',
            ownerKeyword: 'adm',
            ownerUserId: null,
            ownerNote: 'adm',
        };
        expect(context.KitProxy.serviceFilters.validate(pending).valid).toBe(false);

        const selected = Object.assign({}, pending, { ownerUserId: 1, ownerNote: 'admin' });
        expect(context.KitProxy.serviceFilters.validate(selected).valid).toBe(true);
        expect(context.KitProxy.serviceFilters.toRequest(selected)).toEqual({ user_id: 1 });
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
     * 测试思路：请求侧和响应侧 Body 类型保持一致，Multiform 暂不作为可编辑入口暴露。
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
     * 测试思路：Multiform 编辑入口隐藏后，已有 Multiform 数据仍需以只读方式保留，不能被 fallback 改写。
     * 示例：编辑器收到旧 Multiform 类型和值时不显示字段表格，getType/getValue 仍返回原始数据。
     */
    it('隐藏 Multiform Body 编辑入口并保留已有数据', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const editor = context.KitProxy.bodyEditor.create(host, {
            bodyType: 'multiform',
            value: 'legacy multiform body',
            allowedTypes: ['text', 'multiform'],
        });

        expect(Array.from(host.querySelector('.body-editor-type').options).map(option => option.value)).toEqual(['text']);
        expect(host.querySelector('.body-editor-type').disabled).toBe(true);
        expect(host.querySelector('.body-editor-multiform-wrap').classList.contains('is-collapsed')).toBe(true);
        expect(host.querySelector('.body-editor-unsupported-note').hidden).toBe(false);
        expect(host.querySelector('.body-editor-textarea').readOnly).toBe(true);
        expect(editor.getType()).toBe('multiform');
        expect(editor.getValue()).toBe('legacy multiform body');
    });

    /**
     * 测试思路：BodyEditor 按 Body 类型自动折叠文本区，Binary 模式复用 TCP 的普通字段编辑能力，
     * 但不展示角色列；STR 字段使用可编辑的 1~32 长度和文本/十六进制切换。
     * 示例：None/Image 收起文本区；JSON 展开文本区；Binary 中 STR 未选长度默认 1，输入 OK 保存为 H4F4B。
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
        expect(host.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).not.toContain('角色');
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

        expect(readBinaryRows()[0].classList.contains('pattern-field-role-hidden')).toBe(true);
        const typeSelect = host.querySelector('.body-editor-binary-wrap .pattern-field-type');
        typeSelect.value = 'UINT16';
        typeSelect.dispatchEvent(new context.Event('change', { bubbles: true }));
        const byteLenInput = host.querySelector('.body-editor-binary-wrap .pattern-field-byte-len');
        expect(byteLenInput.value).toBe('2');
        expect(byteLenInput.disabled).toBe(true);
        const valueEditor = host.querySelector('.body-editor-binary-wrap .pattern-value-editor-input');
        const nameInput = host.querySelector('.body-editor-binary-wrap .pattern-field-name');
        nameInput.value = '请求标识';
        nameInput.dispatchEvent(new context.Event('input', { bubbles: true }));
        valueEditor.value = '01 02';
        valueEditor.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(editor.getBinaryFields()[0].value).toBe('H0102');
        expect(JSON.parse(editor.getValue())).toEqual({
            fields: [{
                spec: {
                    byte_len: 2,
                    byte_pos: 0,
                    match: 'H0102',
                    name: '请求标识',
                    role: 'common',
                    type: 'UINT16',
                },
                value: 'H0102',
            }],
        });
        expect(context.KitProxy.bodySyntax.normalizeBodyContent(editor.getValue(), 'binary')).toBe(editor.getValue());

        editor.clearBinaryFields();
        const stringField = readBinaryRows()[0];
        const stringTypeSelect = stringField.querySelector('.pattern-field-type');
        const stringByteLen = stringField.querySelector('.pattern-field-byte-len');
        const stringValueEditor = stringField.querySelector('.pattern-value-editor-input');
        const stringDisplayButton = stringField.querySelector('.pattern-field-value-display-btn');
        stringTypeSelect.value = 'STR';
        stringTypeSelect.dispatchEvent(new context.Event('change', { bubbles: true }));

        expect(stringByteLen.value).toBe('1');
        expect(stringByteLen.readOnly).toBe(false);
        expect(stringByteLen.disabled).toBe(false);
        expect(stringByteLen.min).toBe('1');
        expect(stringByteLen.max).toBe('32');
        expect(stringValueEditor.placeholder).toBe('ASCII字符串真值');
        expect(stringDisplayButton.textContent).toBe('S');

        stringByteLen.value = '2';
        stringByteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        stringValueEditor.value = 'OK';
        stringValueEditor.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(editor.getBinaryFields()[0].value).toBe('H4F4B');
        stringDisplayButton.click();
        expect(stringDisplayButton.textContent).toBe('H');
        expect(stringValueEditor.value).toBe('4F 4B');

        stringByteLen.value = '';
        stringByteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(stringByteLen.value).toBe('1');
        stringByteLen.value = '33';
        stringByteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(stringByteLen.value).toBe('32');
        stringByteLen.value = '4';
        stringByteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        stringField.querySelector('.add-field-btn').click();
        expect(readBinaryRows()[1].querySelector('.pattern-field-byte-pos').value).toBe('4');
        expect(editor.getValue()).toBe('');
        expect(context.KitProxy.bodySyntax.validate('旧响应内容', 'binary').valid).toBe(true);
        expect(context.KitProxy.bodySyntax.normalizeBodyContent('旧响应内容', 'binary')).toBe('');
    });

    /**
     * 测试思路：
     * 1. Binary Body 落库格式的字段定义在 spec 内，而实际写入字节单独放在 value；
     *    编辑器重新打开配置时不能把两者展平、混淆或丢失 match。
     * 2. 回填后再次保存必须仍是同一层级的 JSON，且 spec.match 和 value 都保留，
     *    保证前端和后端 FieldValueMapParseFromJson 的契约一致。
     * 3. 当前 UI 用同一个值编辑器配置匹配字节和响应字节，所以保存时两者应规范化
     *    为相同的 H 前缀十六进制字符串。
     *
     * 示例：
     *
     *   fields[0].spec.match = H05060708
     *   fields[0].value      = H05060708
     *             |
     *             v
     *   setValue -> 字段编辑器 -> getValue
     *             |
     *             v
     *   spec/value 层级和同一个字节串均不变
     */
    it('Binary Body 回填嵌套 spec 和独立 value 后保持前后端契约', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        runScript(context, 'js/tcp_pattern_modal.js');

        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const persistedValue = {
            fields: [{
                spec: {
                    byte_len: 4,
                    byte_pos: 0,
                    match: 'H05060708',
                    name: '响应标识',
                    role: 'common',
                    type: 'UINT32',
                },
                value: 'H05060708',
            }],
        };
        const editor = context.KitProxy.bodyEditor.create(host, {
            value: JSON.stringify(persistedValue),
            bodyType: 'binary',
            allowedTypes: ['binary'],
        });

        expect(editor.getBinaryFields()).toEqual([{
            id: expect.any(String),
            name: '响应标识',
            byte_pos: 0,
            byte_len: 4,
            type: 'UINT32',
            role: 'common',
            value: 'H05060708',
        }]);
        expect(JSON.parse(editor.getValue())).toEqual(persistedValue);
        expect(JSON.parse(context.KitProxy.bodySyntax.normalizeBodyContent(
            JSON.stringify(persistedValue),
            'binary',
        ))).toEqual(persistedValue);
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
