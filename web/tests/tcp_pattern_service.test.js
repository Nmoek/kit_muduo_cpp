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
     * 示例：弹窗应有 length_policy 下拉框，隐藏 byte_order 控件但保留 big 值；固定值按钮放在角色列并通过冒泡框填写。
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
        expect(modal.querySelector('.pattern-byte-order-group').hidden).toBe(true);
        expect(modal.querySelector('.pattern-byte-order').value).toBe('big');
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
     * 测试思路：项目格式中的 STR 长度由用户明确配置，允许范围为 1~32，
     * 不能留空；0、负号和非数字不能留在输入框内，超过 32 时立即归一为 32。
     * 示例：UINT8 -> STR 后长度输入框可编辑，输入 0 显示 1，输入 33 显示 32。
     */
    it('项目格式 STR 长度可手动配置且限制为 1~32', () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);

        context.createCustomTcpPatternModal(target, '项目格式字段', {
            version: 2,
            header_bytes: 3,
            byte_order: 'raw',
            length_policy: 'no_length',
            fields: [
                { name: '起始标识', byte_pos: 0, byte_len: 1, type: 'UINT8', role: 'start_magic', match: 'H23' },
                { name: '功能码', byte_pos: 1, byte_len: 1, type: 'UINT8', role: 'function_code' },
                { name: '字符串字段', byte_pos: 2, byte_len: 1, type: 'UINT8', role: 'common' },
            ],
        }, null, true);

        const modal = context.document.querySelector('.config-pattern-modal.is-project-pattern');
        const field = Array.from(modal.querySelectorAll('.pattern-field-container'))
            .find(node => node.querySelector('.pattern-field-name').value === '字符串字段');
        const typeSelect = field.querySelector('.pattern-field-type');
        const byteLen = field.querySelector('.pattern-field-byte-len');

        typeSelect.value = 'STR';
        typeSelect.dispatchEvent(new context.Event('change', { bubbles: true }));

        expect(byteLen.value).toBe('1');
        expect(byteLen.placeholder).toBe('1~32');
        expect(byteLen.readOnly).toBe(false);
        expect(byteLen.disabled).toBe(false);
        expect(byteLen.min).toBe('1');
        expect(byteLen.max).toBe('32');

        byteLen.value = '';
        byteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(byteLen.value).toBe('');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toContain('Byte 长度');

        byteLen.value = '0';
        byteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(byteLen.value).toBe('1');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toBe('');

        byteLen.value = '-5';
        byteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(byteLen.value).toBe('5');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toBe('');

        byteLen.value = 'a7b';
        byteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(byteLen.value).toBe('7');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toBe('');

        byteLen.value = '33';
        byteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        const cappedPreview = Array.from(modal.querySelectorAll('.pattern-byte-block'))
            .find(node => node.querySelector('strong')?.textContent === '字符串字段');
        expect(byteLen.value).toBe('32');
        expect(cappedPreview?.textContent).toContain('STR · 32 Byte');
        expect(cappedPreview?.style.getPropertyValue('--pattern-span')).toBe('10');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toBe('');

        byteLen.value = '5';
        byteLen.dispatchEvent(new context.Event('input', { bubbles: true }));
        const preview = Array.from(modal.querySelectorAll('.pattern-byte-block'))
            .find(node => node.querySelector('strong')?.textContent === '字符串字段');
        expect(preview?.textContent).toContain('STR · 5 Byte');
        expect(modal.querySelector('.pattern-summary-item.is-ok')).toBeTruthy();
        expect(modal.querySelector('.pattern-validation-errors').textContent).toBe('');
    });

    /**
     * 测试思路：长度角色由运行时按 Body 或报文总长度回填，项目格式和协议项配置都不能展示伪造的零值。
     * 示例：body_length/total_length 即使历史数据带 H0000，值输入框也只显示“自动填充”，隐藏值清空且不可编辑。
     */
    it('Body长度和总长度字段值显示自动填充', () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);
        const pattern = {
            version: 2,
            header_bytes: 6,
            byte_order: 'big',
            length_policy: 'body_length',
            fields: [
                { name: 'Body长度', byte_pos: 0, byte_len: 2, type: 'UINT16', role: 'body_length', value: 'H0000' },
                { name: '总长度', byte_pos: 2, byte_len: 4, type: 'UINT32', role: 'total_length', value: 'H00000000' },
            ],
        };

        context.createCustomTcpPatternModal(target, '项目格式字段', pattern, null, true);
        let rows = context.document.querySelectorAll('.config-pattern-modal .pattern-field-container');
        rows.forEach(row => {
            expect(row.querySelector('.pattern-value-editor-input').value).toBe('');
            expect(row.querySelector('.pattern-value-editor-input').placeholder).toBe('自动填充');
            expect(row.querySelector('.pattern-value-editor-input').disabled).toBe(true);
            expect(row.querySelector('.pattern-field-value').value).toBe('');
            expect(row.querySelector('.pattern-field-value-display-btn').disabled).toBe(true);
        });
        context.document.querySelector('.config-pattern-modal .cancel-btn').click();

        context.createCustomTcpPatternModal(target, '协议项字段值', Object.assign({}, pattern, {
            item_value_scope: 'header',
        }), null, false);
        rows = context.document.querySelectorAll('.config-pattern-modal .pattern-field-container');
        expect(rows).toHaveLength(2);
        rows.forEach(row => {
            expect(row.querySelector('.pattern-value-editor-input').value).toBe('');
            expect(row.querySelector('.pattern-value-editor-input').placeholder).toBe('自动填充');
            expect(row.querySelector('.pattern-value-editor-input').disabled).toBe(true);
            expect(row.querySelector('.pattern-field-value').value).toBe('');
        });
    });

    /**
     * 测试思路：字节序暂时隐藏仅影响界面，不能从 API 契约中删除；无输入时统一使用 big，已有显式值继续保留。
     * 示例：缺省配置序列化为 byte_order=big，历史 little 配置再次保存仍输出 little。
     */
    it('隐藏字节序控件并保留默认和显式API值', () => {
        const editor = context.KitProxy.tcpPatternEditor;
        const defaultOrder = editor.serializePatternInfo({
            version: 2,
            header_bytes: 0,
            length_policy: 'no_length',
            fields: [],
        });
        const explicitOrder = editor.serializePatternInfo({
            version: 2,
            header_bytes: 0,
            byte_order: 'little',
            length_policy: 'body_length',
            fields: [],
        });

        expect(defaultOrder.byte_order).toBe('big');
        expect(explicitOrder.byte_order).toBe('little');
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
     * 测试思路：STR 字段进入协议项模态框后默认使用 ASCII 真值编辑，避免
     * 用户没有点击按钮时把连续字符串送入十六进制过滤器；提交语义仍保持
     * H 开头的 wire hex。
     * 示例：配置长度为 3，H313233 -> 默认显示 123 -> 输入 1234 仍截断为 123。
     */
    it('STR 字段默认使用 ASCII 真值显示并可切回 wire hex', () => {
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
        expect(editor.placeholder).toBe('ASCII字符串真值');
        expect(editor.value).toBe('123');
        expect(editor.maxLength).toBe(3);
        expect(button.textContent).toBe('S');
        expect(button.classList.contains('value-display-str')).toBe(true);
        editor.value = '1234';
        editor.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(input.value).toBe('123');
        expect(editor.value).toBe('123');
        expect(byteLen.value).toBe('3');
        expect(context.document.querySelector('.pattern-validation-errors').textContent).toBe('');

        button.click();
        expect(button.textContent).toBe('H');
        expect(input.value).toBe('H313233');
        expect(editor.value).toBe('31 32 33');
        expect(button.classList.contains('value-display-hex')).toBe(true);
    });

    /**
     * 测试思路：STR 真值输入只允许 ASCII 且不能超过 32 字节，长度使用
     * 项目格式中已经配置的值，不因输入内容变化而改写。
     * 并且粘贴或输入非 ASCII 内容时不能进入字段值模型。
     * 示例：配置长度为 32，输入 33 个 A 和一个中文字符 -> 编辑器只保留前 32 个 A。
     */
    it('STR 真值输入限制为 ASCII 且最多 32 字节', () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);

        context.createCustomTcpPatternModal(target, '协议项字段', {
            version: 2,
            header_bytes: 0,
            byte_order: 'raw',
            length_policy: 'no_length',
            fields: [
                { name: '字符串字段', byte_pos: 0, byte_len: 32, type: 'STR', role: 'common', value: '' },
            ],
        }, null, false);

        const field = context.document.querySelector('.pattern-field-container');
        const input = field.querySelector('.pattern-field-value');
        const editor = field.querySelector('.pattern-value-editor-input');
        const byteLen = field.querySelector('.pattern-field-byte-len');
        const text = `${'A'.repeat(33)}中`;

        editor.value = '';
        editor.dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(byteLen.value).toBe('32');

        editor.value = text;
        editor.dispatchEvent(new context.Event('input', { bubbles: true }));

        expect(editor.value).toBe('A'.repeat(32));
        expect(input.value).toBe('A'.repeat(32));
        expect(byteLen.value).toBe('32');
        expect(context.document.querySelector('.pattern-validation-errors').textContent).toBe('');
    });

    /**
     * 测试思路：协议项字段值中的 STR 必须由用户提供实际内容，空值不能像
     * 普通数值字段一样被当作“未配置”放过。
     * 示例：空 STR 字段打开配置框时立即显示错误，编辑器带 required 属性，提交回调不执行。
     */
    it('协议项 STR 字段值不能为空', async () => {
        const target = context.document.createElement('button');
        context.document.body.appendChild(target);
        let callbackCalled = false;

        context.createCustomTcpPatternModal(target, '协议项字段', {
            version: 2,
            header_bytes: 0,
            byte_order: 'raw',
            length_policy: 'no_length',
            fields: [
                { name: '字符串字段', byte_pos: 0, byte_len: 0, type: 'STR', role: 'common', value: '' },
            ],
        }, null, false, () => {
            callbackCalled = true;
        });

        const modal = context.document.querySelector('.config-pattern-modal.is-item-pattern');
        const editor = modal.querySelector('.pattern-value-editor-input');
        expect(editor.required).toBe(true);
        expect(modal.querySelector('.pattern-validation-errors').textContent).toContain('STR字段值不能为空');

        modal.querySelector('#config-pattern-modal-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(4);

        expect(callbackCalled).toBe(false);
        expect(context.document.querySelector('.config-pattern-modal.is-item-pattern')).toBeTruthy();
    });

    /**
     * 测试思路：STR 的输入值必须同时满足非空、ASCII、32 字节上限和项目格式
     * 指定的固定长度。
     * 示例：项目长度为 1，输入 H414243 应失败，输入 H41 才能通过；不填值也失败。
     */
    it('TCP item cfg 对 STR 按配置长度校验并拒绝空值', () => {
        const patternInfo = {
            version: 2,
            header_bytes: 3,
            byte_order: 'raw',
            length_policy: 'no_length',
            fields: [
                { name: '起始标识', byte_pos: 0, byte_len: 1, type: 'UINT8', role: 'start_magic', match: 'H23' },
                { name: '功能码', byte_pos: 1, byte_len: 1, type: 'UINT8', role: 'function_code' },
                { name: '字符串字段', byte_pos: 2, byte_len: 1, type: 'STR', role: 'common' },
            ],
        };
        const validate = context.KitProxy.tcpPatternEditor.validateTcpItemCfg;

        const emptyValidation = validate(patternInfo, {
            function_code: 'H01',
            fields: {},
        });
        expect(emptyValidation.valid).toBe(false);
        expect(emptyValidation.errors.join('\n')).toContain('STR字段值不能为空');

        const mismatchValidation = validate(patternInfo, {
            function_code: 'H01',
            fields: { 2: 'H414243' },
        });
        expect(mismatchValidation.valid).toBe(false);
        expect(mismatchValidation.errors.join('\n')).toContain('字段值字节数必须等于 1');

        const valueValidation = validate(patternInfo, {
            function_code: 'H01',
            fields: { 2: 'H41' },
        });
        expect(valueValidation.valid).toBe(true);
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
            ctime: '2025-08-11T07:55:15.000Z',
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
     * Test idea: IDs are administrative metadata and must not leak into normal-user cards.
     * Example: the same project card has a project-id pill for admin and no pill for normal users.
     */
    it('服务卡片只向管理员展示服务 ID，并将创建时间转换为本地时间', () => {
        const project = {
            id: 12,
            name: '时间服务',
            protocol_type: context.ProtocolType.HTTP,
            listen_port: 18080,
            mode: context.ProjectMode.SERVER,
            status: 1,
            ctime: '2025-08-11T07:55:15Z',
        };
        const instant = new Date(project.ctime);
        const expected = `${instant.getFullYear()}-${String(instant.getMonth() + 1).padStart(2, '0')}-${String(instant.getDate()).padStart(2, '0')}`
            + ` ${String(instant.getHours()).padStart(2, '0')}:${String(instant.getMinutes()).padStart(2, '0')}:${String(instant.getSeconds()).padStart(2, '0')}`;

        const normalCard = context.addServiceCard(project);
        expect(normalCard.querySelector('.project-id')).toBeNull();
        expect(normalCard.querySelector('.project-create-time .meta-value').textContent).toBe(expected);

        context.KitProxy.auth.applyCurrentUser({ id: 1, note: 'admin', role: 'admin' });
        const adminCard = context.addServiceCard(project);
        expect(adminCard.querySelector('.project-id .meta-value').textContent).toBe('12');
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
            ctime: '2025-08-11T07:55:15.000Z',
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
