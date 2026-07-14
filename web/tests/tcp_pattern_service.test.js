import { beforeEach, describe, expect, it, vi } from 'vitest';
import { createBrowserContext, flushPromises, loadCoreScripts, readRepoFile, runScript } from './helpers/browser_context.js';

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
     * 示例：弹窗应有 length_policy 下拉框，不再有“最小解析长度”；固定值按钮放在角色列并通过冒泡框填写。
     */
    it('config-pattern-modal 内置长度策略、移除最小解析长度并用角色列填写固定值', () => {
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
        const bytePosInput = firstField.querySelector('.pattern-field-byte-pos');
        const byteLenInput = firstField.querySelector('.pattern-field-byte-len');
        const typeSelect = firstField.querySelector('.pattern-field-type');
        const fixedValueButton = firstField.querySelector('.pattern-fixed-value-btn');
        const actionButtons = Array.from(firstField.querySelectorAll('.pattern-cell-actions button'));
        expect(bytePosInput.readOnly).toBe(true);
        expect(byteLenInput.readOnly).toBe(true);
        expect(byteLenInput.disabled).toBe(true);
        expect(fixedValueButton.hidden).toBe(false);
        expect(firstField.querySelector('.pattern-cell-role .pattern-fixed-value-btn')).toBe(fixedValueButton);
        expect(firstField.querySelector('.pattern-cell-role').classList.contains('has-fixed-value-control')).toBe(true);
        expect(firstField.querySelector('.pattern-cell-actions .pattern-fixed-value-btn')).toBeNull();
        expect(actionButtons).toHaveLength(4);
        expect(actionButtons.map(button => button.textContent.trim())).toEqual(['', '', '', '']);
        expect(actionButtons.every(button => button.querySelector('.pattern-action-icon'))).toBe(true);
        expect(fixedValueButton.textContent.trim()).toBe('固');
        expect(fixedValueButton.querySelector('.pattern-action-icon-fixed-value')).toBeNull();
        expect(fixedValueButton.classList.contains('has-fixed-value')).toBe(true);
        expect(fixedValueButton.getAttribute('aria-label')).toBe('固定值已配置');
        expect(firstField.querySelector('.pattern-field-value').value).toBe('H23232323');
        expect(firstField.querySelector('.pattern-hex-prefix').textContent).toBe('H');
        expect(firstField.querySelector('.pattern-value-editor-input').value).toBe('23 23 23 23');

        fixedValueButton.click();
        const popover = firstField.querySelector('.pattern-fixed-value-popover');
        expect(popover).toBeTruthy();
        expect(firstField.classList.contains('is-fixed-value-popover-open')).toBe(true);
        expect(popover.querySelector('label').textContent).toBe('固定值');
        expect(popover.querySelector('.pattern-hex-prefix').textContent).toBe('H');
        expect(popover.querySelector('.pattern-fixed-value-hex-digits').placeholder).toBe('00 00 00 00');
        popover.querySelector('.pattern-fixed-value-hex-digits').value = '01020304FF';
        popover.querySelector('.pattern-fixed-value-hex-digits').dispatchEvent(new context.Event('input', { bubbles: true }));
        popover.querySelector('.pattern-fixed-value-save').click();
        expect(firstField.querySelector('.pattern-field-value').value).toBe('H01020304');
        expect(firstField.querySelector('.pattern-value-editor-input').value).toBe('01 02 03 04');
        expect(firstField.querySelector('.pattern-fixed-value-popover')).toBeNull();
        expect(firstField.classList.contains('is-fixed-value-popover-open')).toBe(false);
        expect(fixedValueButton.classList.contains('has-fixed-value')).toBe(true);
        expect(fixedValueButton.getAttribute('aria-label')).toBe('固定值已配置');

        typeSelect.value = 'UINT16';
        typeSelect.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(byteLenInput.value).toBe('2');

        firstField.querySelector('.pattern-field-role').value = 'common';
        firstField.querySelector('.pattern-field-role').dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(fixedValueButton.hidden).toBe(true);
        expect(firstField.querySelector('.pattern-cell-role').classList.contains('has-fixed-value-control')).toBe(false);

        firstField.querySelector('.pattern-field-role').value = 'start_magic';
        firstField.querySelector('.pattern-field-role').dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(fixedValueButton.hidden).toBe(false);
        expect(fixedValueButton.classList.contains('has-fixed-value')).toBe(false);
        expect(fixedValueButton.getAttribute('aria-label')).toBe('固定值未配置');

        context.confirm = () => true;
        modal.querySelector('.clear-btn').click();
        const resetFields = modal.querySelectorAll('.pattern-field-container');
        expect(resetFields).toHaveLength(1);
        expect(modal.querySelector('.pattern-empty-field-row')).toBeNull();
        expect(resetFields[0].querySelector('.pattern-field-name').value).toBe('');
        expect(resetFields[0].querySelector('.pattern-field-byte-pos').value).toBe('0');
        expect(resetFields[0].querySelector('.pattern-field-byte-len').value).toBe('');
        expect(resetFields[0].querySelector('.pattern-field-type').value).toBe('');
        expect(resetFields[0].querySelector('.pattern-field-role').value).toBe('common');
        expect(resetFields[0].querySelector('.del-field-btn').disabled).toBe(true);
        resetFields[0].querySelector('.del-field-btn').click();
        expect(modal.querySelectorAll('.pattern-field-container')).toHaveLength(1);

        resetFields[0].querySelector('.add-field-btn').click();
        const twoFields = Array.from(modal.querySelectorAll('.pattern-field-container'));
        expect(twoFields).toHaveLength(2);
        expect(twoFields.every(field => field.querySelector('.del-field-btn').disabled === false)).toBe(true);
    });

    /**
     * 测试思路：项目 TCP 格式字段列表的新增按钮位于每行操作列，结构变化后按行顺序重算 Byte 起始。
     * 示例：在 4 字节起始标识下方新增 1 字节字段后，后续 2 字节功能码应从 byte_pos=5 开始；下移、删除后继续自动回填。
     */
    it('项目字段行内新增、移动和删除后自动回填 Byte 起始', async () => {
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
        const readPositions = () => Array.from(modal.querySelectorAll('.pattern-field-container .pattern-field-byte-pos'))
            .map(input => Number(input.value));
        const readFields = () => Array.from(modal.querySelectorAll('.pattern-field-container'));

        expect(modal.querySelector('.pattern-field-info-header .add-field-btn')).toBeNull();
        expect(modal.querySelector('.sort-field-btn')).toBeNull();
        expect(readFields()[0].querySelector('.pattern-cell-actions .add-field-btn')).toBeTruthy();
        expect(readFields()[0].querySelector('.pattern-cell-actions .add-field-btn').textContent.trim()).toBe('');
        expect(readFields()[0].querySelector('.pattern-cell-actions .add-field-btn .pattern-action-icon-add')).toBeTruthy();
        expect(readFields().every(field => field.querySelector('.pattern-field-byte-pos').readOnly)).toBe(true);

        readFields()[0].querySelector('.add-field-btn').click();
        expect(readFields()).toHaveLength(4);
        expect(readFields()[1].querySelector('.pattern-field-name').value).toBe('');
        expect(readFields()[1].classList.contains('is-added')).toBe(true);
        expect(readPositions()).toEqual([0, 4, 5, 7]);

        readFields()[1].querySelector('.pattern-field-move-down').click();
        expect(readFields()[2].querySelector('.pattern-field-name').value).toBe('');
        expect(readFields()[2].classList.contains('is-moved')).toBe(true);
        expect(readPositions()).toEqual([0, 4, 6, 7]);

        const deletingField = readFields()[1];
        deletingField.querySelector('.del-field-btn').click();
        expect(deletingField.classList.contains('is-delete-marked')).toBe(true);
        expect(deletingField.classList.contains('is-deleting')).toBe(false);
        expect(readFields()).toHaveLength(4);
        await new Promise(resolve => setTimeout(resolve, 280));
        expect(deletingField.classList.contains('is-deleting')).toBe(true);
        await new Promise(resolve => setTimeout(resolve, 700));
        expect(readFields()).toHaveLength(3);
        expect(readPositions()).toEqual([0, 4, 5]);
    });

    /**
     * 测试思路：新增字段的高亮动画不能覆盖随后的删除反馈。
     * 示例：刚新增的字段立刻删除时，应先清除 is-added，再进入红色删除确认态和收起流程。
     */
    it('新增字段后立即删除时删除动画接管新增高亮', async () => {
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
        const readFields = () => Array.from(modal.querySelectorAll('.pattern-field-container'));

        readFields()[0].querySelector('.add-field-btn').click();
        const addedField = readFields()[1];
        expect(addedField.classList.contains('is-added')).toBe(true);

        addedField.querySelector('.del-field-btn').click();
        expect(addedField.classList.contains('is-added')).toBe(false);
        expect(addedField.classList.contains('is-delete-marked')).toBe(true);
        await new Promise(resolve => setTimeout(resolve, 280));
        expect(addedField.classList.contains('is-deleting')).toBe(true);
        await new Promise(resolve => setTimeout(resolve, 700));
        expect(readFields()).toHaveLength(3);
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

    /**
     * 测试思路：Body 高亮器应按类型输出 token，Text 类型只做安全转义。
     * 示例：JSON 生成 key/boolean/number/null token，XML 生成 tag/attr/comment token，Text 不生成 tag token。
     */
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

    /**
     * 测试思路：Body Editor 需要保留旧 textarea id 供现有代码查找，同时新增高亮层。
     * 示例：idPrefix=protocol-body 时应存在 #protocol-body-content，大内容超过阈值后禁用高亮。
     */
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

    /**
     * 测试思路：行号、输入层和高亮层必须共用滚动尺寸，避免多行内容错位。
     * 示例：3 行文本生成 3 个行号，更新为 4 行后高度变量同步变为 104px。
     */
    it('Body 编辑器使用统一滚动容器承载行号、输入层和高亮层', () => {
        const host = context.document.createElement('div');
        context.document.body.appendChild(host);
        const editor = context.KitProxy.bodyEditor.create(host, {
            idPrefix: 'protocol-body',
            value: '第一行\n第二行\n第三行',
            bodyType: 'text',
        });
        const css = readRepoFile('css/main.css');

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

    /**
     * 测试思路：导入 Body 的弹窗也要复用 Body Editor，保证校验和高亮体验一致。
     * 示例：开启 useBodyEditor 后，弹窗内应出现 body-editor-host 和高亮后的 JSON key。
     */
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

    /**
     * 测试思路：服务卡片点击区域不能误触页面跳转，只有查看协议项按钮负责导航。
     * 示例：点击卡片或标题不改变 href，点击 view-protocols-btn 才派发目标 URL。
     */
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
     * 测试思路：服务卡片状态按钮应调用 setProjectRuntimeState，并用返回端口刷新卡片。
     * 示例：runtime_state=0 的服务器模式服务点击后变为“开启”，监听端口显示 Mock 返回值。
     */
    it('服务卡片状态开关调用 setProjectRuntimeState 并刷新 runtime_state 与端口', async () => {
        const project = {
            id: 9,
            name: '待启动服务',
            protocol_type: context.ProtocolType.HTTP,
            listen_port: 0,
            mode: context.ProjectMode.SERVER,
            status: 1,
            runtime_state: 0,
            ctime: '2025-08-11 07:55:15',
        };
        const setRuntimeState = vi.spyOn(context.KitProxy.api, 'setProjectRuntimeState')
            .mockResolvedValue({ runtime_state: 1, listen_port: 39009 });

        const card = context.addServiceCard(project);
        card.querySelector('.service-active-toggle').click();
        await flushPromises(8);

        expect(setRuntimeState).toHaveBeenCalledWith(9, true);
        expect(card.querySelector('.project-status .field-value').textContent).toBe('开启');
        expect(card.querySelector('.project-listen-port .field-value').textContent).toBe('39009');
    });
});
