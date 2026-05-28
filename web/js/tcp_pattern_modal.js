(function initTcpPatternModal(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const utils = KitProxy.utils || {};
    const escapeHTML = utils.escapeHTML || function(value) {
        return String(value == null ? '' : value)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    };

    const LengthPolicyValue = global.LengthPolicy || {
        BODY_LENGTH: 'body_length',
        TOTAL_LENGTH: 'total_length',
        NO_LENGTH: 'no_length',
    };
    const LengthPolicyText = global.LengthPolicyStr || {
        body_length: 'Body长度依赖',
        total_length: '总长度依赖',
        no_length: '无长度依赖',
    };

    const FIELD_ROLES = [
        { value: 'common', label: '普通字段' },
        { value: 'start_magic', label: '起始标识' },
        { value: 'function_code', label: '功能码' },
        { value: 'body_length', label: 'Body长度' },
        { value: 'total_length', label: '总长度' },
    ];

    const FIXED_TYPE_LENGTHS = Object.freeze({
        INT8: 1,
        UINT8: 1,
        INT16: 2,
        UINT16: 2,
        INT32: 4,
        UINT32: 4,
        FLOAT: 4,
        INT64: 8,
        UINT64: 8,
        DOUBLE: 8,
    });

    function toFiniteNumber(value, fallback = null) {
        if (value === '' || value == null) return fallback;
        const numberValue = Number(value);
        return Number.isFinite(numberValue) ? numberValue : fallback;
    }

    function normalizeLengthPolicyValue(value, fallback = LengthPolicyValue.BODY_LENGTH) {
        if (value === LengthPolicyValue.BODY_LENGTH || value === LengthPolicyValue.TOTAL_LENGTH || value === LengthPolicyValue.NO_LENGTH) {
            return value;
        }
        if (String(value) === '1') return LengthPolicyValue.BODY_LENGTH;
        if (String(value) === '2') return LengthPolicyValue.TOTAL_LENGTH;
        if (String(value) === '3') return LengthPolicyValue.NO_LENGTH;
        return fallback;
    }

    function normalizeByteOrder(value, fallback = 'big') {
        const order = String(value || '').trim();
        return order === 'big' || order === 'little' || order === 'raw' ? order : fallback;
    }

    function roleFromLegacySpecialKey(key) {
        if (key === 'start_magic_num_field' || key === 'start_magic_field') return 'start_magic';
        if (key === 'function_code_field') return 'function_code';
        if (key === 'body_length_field') return 'body_length';
        if (key === 'total_length_field') return 'total_length';
        return 'common';
    }

    function roleClassName(role) {
        return `role-${String(role || 'common').replaceAll('_', '-').replace(/[^a-zA-Z0-9-]/g, '') || 'common'}`;
    }

    function wireHexByteLength(value) {
        const text = String(value || '').trim();
        if (!/^H[0-9a-fA-F]*$/.test(text) || (text.length - 1) % 2 !== 0) return null;
        return (text.length - 1) / 2;
    }

    function isWireHex(value) {
        return wireHexByteLength(value) != null;
    }

    function bytesFromWireHex(value) {
        const text = String(value || '').trim();
        if (!isWireHex(text)) {
            throw new Error('字段值必须是 H 开头且长度为偶数的十六进制字符串');
        }

        const bytes = [];
        for (let index = 1; index < text.length; index += 2) {
            bytes.push(parseInt(text.slice(index, index + 2), 16));
        }
        return bytes;
    }

    function wireHexFromBytes(bytes) {
        return 'H' + Array.from(bytes || [])
            .map(byte => Number(byte).toString(16).padStart(2, '0').toUpperCase())
            .join('');
    }

    function utf8Bytes(text) {
        return Array.from(new TextEncoder().encode(String(text == null ? '' : text)));
    }

    function signedNumberFromBytes(bytes) {
        let value = 0n;
        bytes.forEach(byte => {
            value = (value << 8n) | BigInt(byte);
        });
        const bitCount = BigInt(bytes.length * 8);
        const signBit = 1n << (bitCount - 1n);
        if ((value & signBit) !== 0n) {
            value -= 1n << bitCount;
        }
        return value;
    }

    function unsignedNumberFromBytes(bytes) {
        let value = 0n;
        bytes.forEach(byte => {
            value = (value << 8n) | BigInt(byte);
        });
        return value;
    }

    function numberToBytes(value, byteLen, signed) {
        const bitCount = BigInt(byteLen * 8);
        let bigintValue = BigInt(value);
        if (signed && bigintValue < 0n) {
            bigintValue = (1n << bitCount) + bigintValue;
        }
        const maxValue = 1n << bitCount;
        if (bigintValue < 0n || bigintValue >= maxValue) {
            throw new Error('字段真值超出当前类型长度范围');
        }

        const bytes = new Array(byteLen).fill(0);
        for (let index = byteLen - 1; index >= 0; index -= 1) {
            bytes[index] = Number(bigintValue & 0xffn);
            bigintValue >>= 8n;
        }
        return bytes;
    }

    function floatToBytes(value, byteLen) {
        const buffer = new ArrayBuffer(byteLen);
        const view = new DataView(buffer);
        if (byteLen === 4) {
            view.setFloat32(0, Number(value), false);
        } else {
            view.setFloat64(0, Number(value), false);
        }
        return Array.from(new Uint8Array(buffer));
    }

    function displayFromWireHex(wireHex, type) {
        const bytes = bytesFromWireHex(wireHex);
        const normalizedType = String(type || '').toUpperCase();

        if (normalizedType === 'STR') {
            return new TextDecoder().decode(new Uint8Array(bytes));
        }
        if (normalizedType === 'FLOAT' || normalizedType === 'DOUBLE') {
            const expectedLen = normalizedType === 'FLOAT' ? 4 : 8;
            if (bytes.length !== expectedLen) throw new Error('浮点字段长度不匹配');
            const buffer = new ArrayBuffer(expectedLen);
            new Uint8Array(buffer).set(bytes);
            const view = new DataView(buffer);
            const value = normalizedType === 'FLOAT'
                ? view.getFloat32(0, false)
                : view.getFloat64(0, false);
            return Number.isFinite(value) ? value.toFixed(5) : String(value);
        }
        if (normalizedType.startsWith('INT')) {
            return String(signedNumberFromBytes(bytes));
        }
        if (normalizedType.startsWith('UINT')) {
            return String(unsignedNumberFromBytes(bytes));
        }

        throw new Error('当前字段类型不支持真值显示');
    }

    function wireHexFromDisplay(displayValue, type, byteLen) {
        const normalizedType = String(type || '').toUpperCase();
        const text = String(displayValue == null ? '' : displayValue);

        if (normalizedType === 'STR') {
            return wireHexFromBytes(utf8Bytes(text));
        }
        if (normalizedType === 'FLOAT' || normalizedType === 'DOUBLE') {
            const numberValue = Number(text);
            if (!Number.isFinite(numberValue)) throw new Error('浮点字段真值必须是数字');
            return wireHexFromBytes(floatToBytes(numberValue, normalizedType === 'FLOAT' ? 4 : 8));
        }
        if (normalizedType.startsWith('INT') || normalizedType.startsWith('UINT')) {
            if (!/^-?\d+$/.test(text.trim())) throw new Error('整数字段真值必须是十进制整数');
            return wireHexFromBytes(numberToBytes(text.trim(), byteLen, normalizedType.startsWith('INT')));
        }

        throw new Error('当前字段类型不支持真值转换');
    }

    function getTypeByteLen(type, currentValue) {
        const normalizedType = String(type || '').toUpperCase();
        if (Object.prototype.hasOwnProperty.call(FIXED_TYPE_LENGTHS, normalizedType)) {
            return FIXED_TYPE_LENGTHS[normalizedType];
        }
        if (normalizedType === 'STR') {
            if (isWireHex(currentValue)) return wireHexByteLength(currentValue);
            return utf8Bytes(currentValue || '').length || null;
        }
        return null;
    }

    function fieldFromNode(node) {
        if (!node || typeof node.querySelector !== 'function') return {};

        return {
            name: node.querySelector('.pattern-field-name')?.value || '',
            byte_pos: toFiniteNumber(node.querySelector('.pattern-field-byte-pos')?.value, null),
            byte_len: toFiniteNumber(node.querySelector('.pattern-field-byte-len')?.value, null),
            type: node.querySelector('.pattern-field-type')?.value || '',
            role: node.querySelector('.pattern-field-role')?.value || 'common',
            match: node.querySelector('.pattern-field-value')?.value || '',
            value: node.querySelector('.pattern-field-value')?.value || '',
            value_editable: node.dataset.valueEditable !== 'false',
        };
    }

    function cloneField(field) {
        const source = field && typeof field.querySelector === 'function'
            ? fieldFromNode(field)
            : (field || {});
        const role = source.role || source.field_role || 'common';
        const match = source.match != null
            ? source.match
            : (role === 'start_magic' ? source.value : '');

        return {
            name: String(source.name != null ? source.name : ''),
            byte_pos: toFiniteNumber(source.byte_pos, null),
            byte_len: toFiniteNumber(source.byte_len, null),
            type: String(source.type != null ? source.type : ''),
            role,
            match: String(match != null ? match : ''),
            value: String(source.value != null ? source.value : ''),
            value_editable: source.value_editable === false ? false : true,
        };
    }

    function normalizePatternInfo(input, options = {}) {
        const source = input || {};
        const policy = normalizeLengthPolicyValue(source.length_policy, LengthPolicyValue.BODY_LENGTH);
        const byteOrder = normalizeByteOrder(source.byte_order || source.default_order, policy === LengthPolicyValue.NO_LENGTH ? 'raw' : 'big');
        const fields = [];

        if (Array.isArray(source.fields)) {
            source.fields.forEach(field => fields.push(cloneField(field)));
        } else {
            const specialFields = source.special_fields || {};
            Object.keys(specialFields).forEach(key => {
                const field = cloneField(specialFields[key]);
                field.role = field.role && field.role !== 'common' ? field.role : roleFromLegacySpecialKey(key);
                if (field.role === 'start_magic' && !field.match) {
                    field.match = field.value || '';
                }
                fields.push(field);
            });

            if (Array.isArray(source.common_fields)) {
                source.common_fields.forEach(fieldInfo => {
                    const field = cloneField(fieldInfo);
                    field.role = field.role || 'common';
                    fields.push(field);
                });
            }
        }

        const normalizedFields = fields
            .map(field => {
                const nextField = cloneField(field);
                if (!nextField.role || nextField.role === 'undef') nextField.role = 'common';
                return nextField;
            })
            .sort((left, right) => {
                const leftPos = Number.isFinite(left.byte_pos) ? left.byte_pos : Number.MAX_SAFE_INTEGER;
                const rightPos = Number.isFinite(right.byte_pos) ? right.byte_pos : Number.MAX_SAFE_INTEGER;
                return leftPos - rightPos;
            });

        const maxEnd = normalizedFields.reduce((max, field) => {
            if (!Number.isFinite(field.byte_pos) || !Number.isFinite(field.byte_len)) return max;
            return Math.max(max, field.byte_pos + field.byte_len);
        }, 0);

        return {
            version: 2,
            header_bytes: Number(source.header_bytes || source.least_byte_len || maxEnd || 0),
            byte_order: byteOrder,
            default_order: byteOrder,
            length_policy: policy,
            fields: options.onlyCommon
                ? normalizedFields.filter(field => field.role === 'common')
                : normalizedFields,
        };
    }

    function addGapFields(fields, headerBytes) {
        const sorted = (Array.isArray(fields) ? fields : [])
            .filter(field => Number.isFinite(field.byte_pos) && Number.isFinite(field.byte_len) && field.byte_len > 0)
            .sort((left, right) => left.byte_pos - right.byte_pos);
        const result = [];
        let cursor = 0;

        sorted.forEach(field => {
            if (field.byte_pos > cursor) {
                result.push({
                    name: `保留字段${cursor}`,
                    byte_pos: cursor,
                    byte_len: field.byte_pos - cursor,
                    type: 'STR',
                    role: 'common',
                });
            }
            result.push(field);
            cursor = Math.max(cursor, field.byte_pos + field.byte_len);
        });

        if (headerBytes > cursor) {
            result.push({
                name: `保留字段${cursor}`,
                byte_pos: cursor,
                byte_len: headerBytes - cursor,
                type: 'STR',
                role: 'common',
            });
        }

        return result;
    }

    function toPatternInfoV2(input) {
        const normalized = normalizePatternInfo(input);
        const cleanFields = normalized.fields.map(field => {
            const nextField = {
                name: field.name,
                byte_pos: Number(field.byte_pos),
                byte_len: Number(field.byte_len),
                type: field.type,
                role: field.role || 'common',
            };
            if (nextField.role === 'start_magic') {
                nextField.match = field.match || field.value || '';
            }
            return nextField;
        });
        const maxEnd = cleanFields.reduce((max, field) => Math.max(max, field.byte_pos + field.byte_len), 0);
        const headerBytes = Math.max(maxEnd, Number(normalized.header_bytes || 0));

        return {
            version: 2,
            header_bytes: headerBytes,
            byte_order: normalized.byte_order,
            length_policy: normalized.length_policy,
            fields: addGapFields(cleanFields, headerBytes),
        };
    }

    function serializePatternInfo(root) {
        return toPatternInfoV2(root);
    }

    function sortPatternFields(fields) {
        return (Array.isArray(fields) ? fields : [])
            .map(cloneField)
            .sort((left, right) => {
                const leftPos = Number.isFinite(left.byte_pos) ? left.byte_pos : Number.MAX_SAFE_INTEGER;
                const rightPos = Number.isFinite(right.byte_pos) ? right.byte_pos : Number.MAX_SAFE_INTEGER;
                return leftPos - rightPos;
            });
    }

    function computeCommonFieldBytePositions(fields) {
        return sortPatternFields(fields);
    }

    function validateProjectPattern(patternInfo) {
        const normalized = normalizePatternInfo(patternInfo);
        const errors = [];
        const roleCounts = {};
        const spans = [];

        if (!normalized.length_policy) {
            errors.push('长度策略不能为空');
        }

        normalized.fields.forEach((field, index) => {
            const label = field.name || `字段${index + 1}`;
            if (!String(field.name || '').trim()) errors.push(`${label}：字段名称不能为空`);
            if (!Number.isInteger(field.byte_pos) || field.byte_pos < 0) errors.push(`${label}：Byte 起始位置必须是非负整数`);
            if (!Number.isInteger(field.byte_len) || field.byte_len <= 0) errors.push(`${label}：Byte 长度必须是大于 0 的整数`);
            if (!String(field.type || '').trim()) errors.push(`${label}：类型不能为空`);
            if (!String(field.role || '').trim()) errors.push(`${label}：角色不能为空`);

            const expectedLen = getTypeByteLen(field.type, field.role === 'start_magic' ? field.match : field.value);
            if (Number.isFinite(expectedLen) && Number(field.byte_len) !== Number(expectedLen)) {
                errors.push(`${label}：Byte 长度必须和字段类型 ${field.type} 匹配`);
            }

            roleCounts[field.role] = (roleCounts[field.role] || 0) + 1;
            if (Number.isInteger(field.byte_pos) && Number.isInteger(field.byte_len) && field.byte_len > 0) {
                spans.push({
                    name: label,
                    start: field.byte_pos,
                    end: field.byte_pos + field.byte_len,
                });
            }

            if (field.role === 'start_magic') {
                if (!field.match) {
                    errors.push(`${label}：起始标识必须填写固定值`);
                } else if (!isWireHex(field.match)) {
                    errors.push(`${label}：固定值必须是 H 开头的十六进制字节串`);
                } else if (wireHexByteLength(field.match) !== field.byte_len) {
                    errors.push(`${label}：固定值字节数必须等于 Byte 长度`);
                }
            } else if (field.match) {
                errors.push(`${label}：只有起始标识允许填写固定值`);
            }
        });

        spans.sort((left, right) => left.start - right.start);
        for (let index = 1; index < spans.length; index += 1) {
            if (spans[index].start < spans[index - 1].end) {
                errors.push(`${spans[index].name} 与 ${spans[index - 1].name} 字节范围重叠`);
            }
        }

        if ((roleCounts.start_magic || 0) !== 1) errors.push('必须存在且只允许一个起始标识字段');
        if ((roleCounts.function_code || 0) !== 1) errors.push('必须存在且只允许一个功能码字段');
        if (normalized.length_policy === LengthPolicyValue.BODY_LENGTH) {
            if ((roleCounts.body_length || 0) !== 1) errors.push('Body长度依赖模式必须存在且只允许一个 Body长度字段');
            if ((roleCounts.total_length || 0) > 0) errors.push('Body长度依赖模式不允许总长度字段');
        } else if (normalized.length_policy === LengthPolicyValue.TOTAL_LENGTH) {
            if ((roleCounts.total_length || 0) !== 1) errors.push('总长度依赖模式必须存在且只允许一个 总长度字段');
            if ((roleCounts.body_length || 0) > 0) errors.push('总长度依赖模式不允许 Body长度字段');
        } else if (normalized.length_policy === LengthPolicyValue.NO_LENGTH) {
            if ((roleCounts.body_length || 0) > 0 || (roleCounts.total_length || 0) > 0) {
                errors.push('无长度依赖模式不允许 Body长度或总长度字段');
            }
        }

        return {
            valid: errors.length === 0,
            errors,
        };
    }

    function validateItemFields(fields, options = {}) {
        const errors = [];
        if (options.requireFunctionCode) {
            const functionField = (Array.isArray(fields) ? fields : []).find(field => field.role === 'function_code');
            if (!functionField || !functionField.value) {
                errors.push('功能码必须配置');
            }
        }

        (Array.isArray(fields) ? fields : []).forEach(field => {
            if (!field.value) return;
            const label = field.name || `byte_pos ${field.byte_pos}`;
            if (!isWireHex(field.value)) {
                errors.push(`${label}：字段值必须是 H 开头的十六进制字节串`);
            } else if (wireHexByteLength(field.value) !== Number(field.byte_len)) {
                errors.push(`${label}：字段值字节数必须等于 ${field.byte_len}`);
            }
        });
        return {
            valid: errors.length === 0,
            errors,
        };
    }

    function validatePatternInfo(patternInfo, options = {}) {
        return options.mode === 'item'
            ? validateItemFields(normalizePatternInfo(patternInfo).fields)
            : validateProjectPattern(patternInfo);
    }

    function patternFieldTypeOptionsHTML(selectedType) {
        const fieldTypes = global.PatternFiledTypeStr || global.PatternFieldTypeStr || {};
        const options = ['<option value="">请选择字段类型</option>'];

        Object.keys(fieldTypes).forEach(key => {
            const type = fieldTypes[key];
            options.push(`<option value="${escapeHTML(type)}" ${String(selectedType || '') === String(type) ? 'selected' : ''}>${escapeHTML(type)}</option>`);
        });

        return options.join('');
    }

    function roleOptionsHTML(selectedRole) {
        return FIELD_ROLES.map(role => {
            return `<option value="${role.value}" ${String(selectedRole || 'common') === role.value ? 'selected' : ''}>${escapeHTML(role.label)}</option>`;
        }).join('');
    }

    function hexDigitsFromValue(value) {
        const text = String(value || '').trim();
        const withoutPrefix = /^[Hh]/.test(text) ? text.slice(1) : text;
        return withoutPrefix.replace(/[^0-9a-fA-F]/g, '').toUpperCase();
    }

    function formatHexDigits(digits) {
        return String(digits || '').replace(/(.{2})(?=.)/g, '$1 ').trim();
    }

    function wireHexFromDigits(digits) {
        const cleanDigits = hexDigitsFromValue(digits);
        return cleanDigits ? `H${cleanDigits}` : '';
    }

    function fieldByteLen(fieldNode) {
        const byteLen = Number(fieldNode.querySelector('.pattern-field-byte-len')?.value || 0);
        return Number.isInteger(byteLen) && byteLen > 0 ? byteLen : 0;
    }

    function hexDigitLimit(fieldNode) {
        const byteLen = fieldByteLen(fieldNode);
        return byteLen > 0 ? byteLen * 2 : null;
    }

    function limitedHexDigits(value, limit) {
        const digits = hexDigitsFromValue(value);
        return Number.isInteger(limit) && limit > 0 ? digits.slice(0, limit) : digits;
    }

    function hexPlaceholder(byteLen) {
        const count = Number.isInteger(byteLen) && byteLen > 0 ? Math.min(byteLen, 16) : 2;
        const placeholder = new Array(count).fill('00').join(' ');
        return Number.isInteger(byteLen) && byteLen > 16 ? `${placeholder} ...` : placeholder;
    }

    function displayModeFromType(type) {
        const normalizedType = String(type || '').toUpperCase();
        if (normalizedType === 'STR') return 'S';
        if (normalizedType === 'FLOAT' || normalizedType === 'DOUBLE') return 'F';
        if (normalizedType.startsWith('INT') || normalizedType.startsWith('UINT')) return 'D';
        return 'H';
    }

    function displayButtonClass(type, mode) {
        const normalizedType = String(type || '').toUpperCase();
        if (mode === 'H') return 'value-display-hex';
        if (normalizedType === 'STR') return 'value-display-str';
        if (normalizedType === 'FLOAT' || normalizedType === 'DOUBLE') return 'value-display-float';
        if (normalizedType.startsWith('UINT')) return 'value-display-uint';
        if (normalizedType.startsWith('INT')) return 'value-display-int';
        return 'value-display-display';
    }

    function updateValueDisplayButton(fieldNode) {
        const input = fieldNode.querySelector('.pattern-field-value');
        const button = fieldNode.querySelector('.pattern-field-value-display-btn');
        const type = fieldNode.querySelector('.pattern-field-type')?.value || '';
        if (!input || !button) return;

        const mode = input.dataset.displayMode || 'H';
        button.textContent = mode;
        button.classList.remove(
            'value-display-hex',
            'value-display-int',
            'value-display-uint',
            'value-display-float',
            'value-display-str',
            'value-display-display',
        );
        button.classList.add(displayButtonClass(type, mode));
    }

    function syncValueEditorFromHidden(fieldNode) {
        const hiddenInput = fieldNode.querySelector('.pattern-field-value');
        const editor = fieldNode.querySelector('.pattern-value-editor-input');
        const wireInput = fieldNode.querySelector('.pattern-wire-hex-input');
        const mode = hiddenInput?.dataset.displayMode || 'H';
        if (!hiddenInput || !editor || !wireInput) return;

        wireInput.classList.toggle('is-display-value', mode !== 'H');
        if (mode === 'H') {
            const limit = hexDigitLimit(fieldNode);
            const digits = limitedHexDigits(hiddenInput.value, limit);
            hiddenInput.value = wireHexFromDigits(digits);
            editor.value = formatHexDigits(digits);
            editor.placeholder = hexPlaceholder(fieldByteLen(fieldNode));
            const groupedLimit = Number.isInteger(limit) && limit > 0
                ? formatHexDigits('A'.repeat(limit)).length
                : 64;
            editor.maxLength = String(groupedLimit);
        } else {
            editor.value = hiddenInput.value;
            editor.placeholder = mode === 'S' ? '字符串真值' : (mode === 'F' ? '小数真值' : '十进制真值');
            editor.removeAttribute('maxlength');
        }

        updateValueDisplayButton(fieldNode);
    }

    function syncHiddenValueFromEditor(fieldNode) {
        const hiddenInput = fieldNode.querySelector('.pattern-field-value');
        const editor = fieldNode.querySelector('.pattern-value-editor-input');
        if (!hiddenInput || !editor) return;

        if ((hiddenInput.dataset.displayMode || 'H') === 'H') {
            const digits = limitedHexDigits(editor.value, hexDigitLimit(fieldNode));
            editor.value = formatHexDigits(digits);
            hiddenInput.value = wireHexFromDigits(digits);
        } else {
            hiddenInput.value = editor.value;
        }
        updateValueDisplayButton(fieldNode);
    }

    function getFieldWireInputValue(field, isProjectMode) {
        if (isProjectMode && field.role === 'start_magic') return field.match || field.value || '';
        return field.value || '';
    }

    function updateByteLenByType(fieldNode) {
        const typeSelect = fieldNode.querySelector('.pattern-field-type');
        const valueInput = fieldNode.querySelector('.pattern-field-value');
        const byteLenInput = fieldNode.querySelector('.pattern-field-byte-len');
        const byteLen = getTypeByteLen(typeSelect.value, valueInput.value);

        if (Number.isFinite(byteLen)) {
            byteLenInput.value = String(byteLen);
        }
    }

    function syncValueAvailability(fieldNode, isProjectMode) {
        const role = fieldNode.querySelector('.pattern-field-role')?.value || 'common';
        const valueInput = fieldNode.querySelector('.pattern-field-value');
        const valueEditor = fieldNode.querySelector('.pattern-value-editor-input');
        const wireInput = fieldNode.querySelector('.pattern-wire-hex-input');
        const displayButton = fieldNode.querySelector('.pattern-field-value-display-btn');
        const fixedValueButton = fieldNode.querySelector('.pattern-fixed-value-btn');
        const label = fieldNode.querySelector('.pattern-cell-value label');

        if (isProjectMode) {
            const enabled = role === 'start_magic';
            valueInput.disabled = !enabled;
            if (valueEditor) valueEditor.disabled = !enabled;
            if (!enabled) {
                valueInput.value = '';
                valueInput.dataset.displayMode = 'H';
                delete valueInput.dataset.wireValue;
            }
            displayButton.disabled = !enabled;
            if (fixedValueButton) {
                fixedValueButton.hidden = !enabled;
                fixedValueButton.disabled = !enabled;
                fixedValueButton.classList.toggle('has-fixed-value', Boolean(enabled && valueInput.value));
            }
            if (label) label.textContent = '固定值';
        } else {
            const enabled = fieldNode.dataset.valueEditable !== 'false';
            valueInput.disabled = !enabled;
            if (valueEditor) valueEditor.disabled = !enabled;
            if (wireInput) wireInput.classList.toggle('is-readonly', !enabled);
            displayButton.disabled = !enabled;
            if (fixedValueButton) fixedValueButton.hidden = true;
            if (label) label.textContent = '字段值';
        }
        syncValueEditorFromHidden(fieldNode);
    }

    function createPatternField(namePlaceholder = '', specialName = '', fieldInfo = null) {
        const field = cloneField(fieldInfo || {});
        if (specialName && (!field.role || field.role === 'common')) {
            field.role = roleFromLegacySpecialKey(specialName);
        }

        const fieldDiv = document.createElement('div');
        fieldDiv.className = 'pattern-field-container';
        fieldDiv.dataset.fieldScope = specialName ? 'special' : 'common';
        if (specialName) {
            fieldDiv.id = specialName;
            fieldDiv.dataset.specialKey = specialName;
        }

        fieldDiv.innerHTML = `
            <div class="pattern-field">
                <div class="pattern-cell pattern-cell-byte-pos">
                    <label>Byte起始位置</label>
                    <input type="number" class="pattern-field-byte-pos" value="${escapeHTML(field.byte_pos ?? '')}" min="0" max="65535" placeholder="0" required>
                </div>
                <div class="pattern-cell pattern-cell-name">
                    <label>名称</label>
                    <input type="text" class="pattern-field-name" value="${escapeHTML(field.name)}" placeholder="${escapeHTML(namePlaceholder)}" required>
                </div>
                <div class="pattern-cell pattern-cell-byte-len">
                    <label>Byte长度</label>
                    <input type="number" class="pattern-field-byte-len" value="${escapeHTML(field.byte_len ?? '')}" min="1" max="65535" placeholder="自动" readonly required>
                </div>
                <div class="pattern-cell pattern-cell-type">
                    <label>类型</label>
                    <select class="pattern-field-type" required>${patternFieldTypeOptionsHTML(field.type)}</select>
                </div>
                <div class="pattern-cell pattern-cell-role">
                    <label>角色</label>
                    <select class="pattern-field-role" required>${roleOptionsHTML(field.role)}</select>
                </div>
                <div class="pattern-cell pattern-cell-value">
                    <label>字段值</label>
                    <div class="pattern-field-value-container">
                        <input type="hidden" class="pattern-field-value" value="${escapeHTML(getFieldWireInputValue(field, false))}">
                        <div class="pattern-wire-hex-input">
                            <span class="pattern-hex-prefix">H</span>
                            <input type="text" class="pattern-value-editor-input" inputmode="text" autocomplete="off" spellcheck="false" aria-label="字段值十六进制字节">
                        </div>
                        <button type="button" class="pattern-field-value-display-btn" title="切换 wire hex/真值显示">H</button>
                    </div>
                </div>
                <div class="pattern-cell pattern-cell-actions">
                    <label>操作</label>
                    <div class="pattern-field-actions">
                        <button type="button" class="pattern-fixed-value-btn" title="填写固定值" hidden>固定值</button>
                        <button type="button" class="pattern-field-move-up" title="上移">上移</button>
                        <button type="button" class="pattern-field-move-down" title="下移">下移</button>
                        <button type="button" class="del-field-btn" title="删除字段">删除</button>
                    </div>
                </div>
            </div>
        `;

        const valueInput = fieldDiv.querySelector('.pattern-field-value');
        valueInput.dataset.displayMode = 'H';
        const nameInput = fieldDiv.querySelector('.pattern-field-name');
        nameInput.addEventListener('dblclick', function() {
            if (!this.value && this.placeholder) {
                this.value = this.placeholder;
                this.dispatchEvent(new Event('input', { bubbles: true }));
            }
        });

        updateByteLenByType(fieldDiv);
        syncValueEditorFromHidden(fieldDiv);
        return fieldDiv;
    }

    function updatePatternField(fieldNode, fieldInfo, isSpecial = false) {
        const field = cloneField(fieldInfo || {});
        if (!fieldNode) return fieldNode;

        fieldNode.querySelector('.pattern-field-name').value = field.name;
        fieldNode.querySelector('.pattern-field-byte-pos').value = field.byte_pos ?? '';
        fieldNode.querySelector('.pattern-field-byte-len').value = field.byte_len ?? '';
        fieldNode.querySelector('.pattern-field-type').value = field.type;
        fieldNode.querySelector('.pattern-field-role').value = field.role || 'common';
        fieldNode.querySelector('.pattern-field-value').value = isSpecial && field.role === 'start_magic'
            ? (field.match || field.value || '')
            : (field.value || '');
        fieldNode.querySelector('.pattern-field-value').dataset.displayMode = 'H';
        setFieldValueEditable(fieldNode, field.value_editable !== false);
        updateByteLenByType(fieldNode);
        syncValueEditorFromHidden(fieldNode);
        return fieldNode;
    }

    function patternListSortByIdx(patternList) {
        const children = Array.isArray(patternList)
            ? patternList
            : Array.from(patternList && patternList.children ? patternList.children : []);
        return children.sort((left, right) => {
            const leftPos = toFiniteNumber(left.querySelector('.pattern-field-byte-pos')?.value, Number.MAX_SAFE_INTEGER);
            const rightPos = toFiniteNumber(right.querySelector('.pattern-field-byte-pos')?.value, Number.MAX_SAFE_INTEGER);
            return leftPos - rightPos;
        });
    }

    function setFieldMetadataReadonly(fieldNode, readonly) {
        ['.pattern-field-name', '.pattern-field-byte-pos', '.pattern-field-byte-len', '.pattern-field-type', '.pattern-field-role'].forEach(selector => {
            const input = fieldNode.querySelector(selector);
            if (input) input.disabled = Boolean(readonly);
        });
        fieldNode.classList.toggle('is-readonly', Boolean(readonly));
    }

    function setFieldValueEditable(fieldNode, editable) {
        const nextEditable = Boolean(editable);
        const hiddenInput = fieldNode.querySelector('.pattern-field-value');
        const editorInput = fieldNode.querySelector('.pattern-value-editor-input');
        const wireInput = fieldNode.querySelector('.pattern-wire-hex-input');
        const displayButton = fieldNode.querySelector('.pattern-field-value-display-btn');

        fieldNode.dataset.valueEditable = nextEditable ? 'true' : 'false';
        if (hiddenInput) hiddenInput.disabled = !nextEditable;
        if (editorInput) editorInput.disabled = !nextEditable;
        if (displayButton) displayButton.disabled = !nextEditable;
        if (wireInput) wireInput.classList.toggle('is-readonly', !nextEditable);
    }

    function readFieldValueInput(fieldNode) {
        const input = fieldNode.querySelector('.pattern-field-value');
        const type = fieldNode.querySelector('.pattern-field-type')?.value || '';
        const byteLen = Number(fieldNode.querySelector('.pattern-field-byte-len')?.value || 0);

        if (!input || !input.value) return '';
        if (input.dataset.displayMode && input.dataset.displayMode !== 'H') {
            return wireHexFromDisplay(input.value, type, byteLen);
        }
        return input.value.trim();
    }

    function readPatternInfoFromDOM(modal, isProjectMode) {
        const fields = [];
        modal.querySelectorAll('.pattern-field-container').forEach(fieldNode => {
            const field = fieldFromNode(fieldNode);
            const value = readFieldValueInput(fieldNode);
            field.byte_pos = Number(field.byte_pos);
            field.byte_len = Number(field.byte_len);
            if (isProjectMode) {
                field.match = field.role === 'start_magic' ? value : '';
                field.value = '';
            } else {
                field.value = value;
                field.match = '';
            }
            fields.push(field);
        });

        return {
            version: 2,
            header_bytes: fields.reduce((max, field) => {
                if (!Number.isFinite(field.byte_pos) || !Number.isFinite(field.byte_len)) return max;
                return Math.max(max, field.byte_pos + field.byte_len);
            }, 0),
            byte_order: modal.querySelector('.pattern-byte-order')?.value || modal.dataset.byteOrder || 'big',
            length_policy: modal.querySelector('.pattern-length-policy')?.value || modal.dataset.lengthPolicy || LengthPolicyValue.BODY_LENGTH,
            fields,
        };
    }

    function renderSummary(modal, model, validation, isProjectMode) {
        const summary = modal.querySelector('.pattern-summary');
        if (!summary) return;

        const normalized = normalizePatternInfo(model);
        const fieldCount = normalized.fields.length;
        const totalLength = normalized.fields.reduce((sum, field) => {
            return sum + Math.max(0, Number(field.byte_len) || 0);
        }, 0);

        summary.innerHTML = `
            <div class="pattern-summary-item">
                <span>配置对象</span>
                <strong>${isProjectMode ? '项目格式字段' : '协议项字段值'}</strong>
            </div>
            <div class="pattern-summary-item">
                <span>长度策略</span>
                <strong>${escapeHTML(LengthPolicyText[normalized.length_policy] || '未知')}</strong>
            </div>
            <div class="pattern-summary-item">
                <span>字段数量</span>
                <strong>${fieldCount}</strong>
            </div>
            <div class="pattern-summary-item">
                <span>头部长度</span>
                <strong>${totalLength} Byte</strong>
            </div>
            <div class="pattern-summary-item ${validation.valid ? 'is-ok' : 'is-error'}">
                <span>校验状态</span>
                <strong>${validation.valid ? '无错误' : `${validation.errors.length} 个错误`}</strong>
            </div>
        `;
    }

    function previewFieldValueText(field, isProjectMode) {
        if (isProjectMode) {
            return `${field.type || '未选类型'} · ${field.byte_len ?? '?'} Byte`;
        }
        return field.value || field.match || '未配置';
    }

    function renderPreview(modal, model, isProjectMode) {
        const preview = modal.querySelector('.pattern-byte-layout');
        if (!preview) return;
        const fields = normalizePatternInfo(model).fields;

        if (!fields.length) {
            preview.innerHTML = '<div class="pattern-layout-empty">暂无字段</div>';
            return;
        }

        preview.innerHTML = fields.map((field, index) => {
            const valid = String(field.name || '').trim()
                && Number.isFinite(field.byte_pos)
                && Number.isFinite(field.byte_len)
                && field.byte_len > 0
                && String(field.type || '').trim();
            const roleClass = roleClassName(field.role);
            return `
                <div class="pattern-byte-block ${roleClass} ${valid ? '' : 'is-error'}" data-role="${escapeHTML(field.role || 'common')}" style="--pattern-span:${Math.max(1, Number(field.byte_len) || 1)}">
                    <span class="pattern-byte-offset">offset ${escapeHTML(field.byte_pos ?? '?')}</span>
                    <strong>${escapeHTML(field.name || `字段${index + 1}`)}</strong>
                    <span>${escapeHTML(previewFieldValueText(field, isProjectMode))}</span>
                </div>
            `;
        }).join('');
    }

    function renderErrors(modal, validation) {
        const errorBox = modal.querySelector('.pattern-validation-errors');
        if (!errorBox) return;

        if (validation.valid) {
            errorBox.style.display = 'none';
            errorBox.innerHTML = '';
            return;
        }

        errorBox.style.display = 'block';
        errorBox.innerHTML = validation.errors.map(error => `<div>${escapeHTML(error)}</div>`).join('');
    }

    function refreshPatternModal(modal, isProjectMode) {
        const model = readPatternInfoFromDOM(modal, isProjectMode);
        const isHeaderValueMode = modal.dataset.itemValueScope === 'header';
        const validation = isProjectMode
            ? validateProjectPattern(model)
            : validateItemFields(model.fields, { requireFunctionCode: isHeaderValueMode });

        renderSummary(modal, model, validation, isProjectMode);
        renderPreview(modal, model, isProjectMode);
        renderErrors(modal, validation);
        return { model, validation };
    }

    function bindValueDisplayToggle(fieldNode, modal, isProjectMode) {
        const input = fieldNode.querySelector('.pattern-field-value');
        const button = fieldNode.querySelector('.pattern-field-value-display-btn');
        const typeSelect = fieldNode.querySelector('.pattern-field-type');
        if (!input || !button || !typeSelect) return;

        button.addEventListener('click', function(event) {
            event.preventDefault();
            event.stopPropagation();

            try {
                if ((input.dataset.displayMode || 'H') === 'H') {
                    const wireValue = input.value.trim();
                    const displayValue = displayFromWireHex(wireValue, typeSelect.value);
                    input.dataset.displayMode = displayModeFromType(typeSelect.value);
                    input.dataset.wireValue = wireValue;
                    input.value = displayValue;
                } else {
                    const byteLen = Number(fieldNode.querySelector('.pattern-field-byte-len')?.value || 0);
                    const wireValue = wireHexFromDisplay(input.value, typeSelect.value, byteLen);
                    input.dataset.displayMode = 'H';
                    input.dataset.wireValue = wireValue;
                    input.value = wireValue;
                }
                updateByteLenByType(fieldNode);
                syncValueEditorFromHidden(fieldNode);
                refreshPatternModal(modal, isProjectMode);
            } catch (error) {
                renderErrors(modal, {
                    valid: false,
                    errors: [error.message],
                });
            }
        });
    }

    function syncFixedValueInputFromEditor(popover, byteLen) {
        const hiddenInput = popover.querySelector('.pattern-fixed-value-input');
        const editor = popover.querySelector('.pattern-fixed-value-hex-digits');
        if (!hiddenInput || !editor) return;

        const limit = Number.isInteger(byteLen) && byteLen > 0 ? byteLen * 2 : null;
        const digits = limitedHexDigits(editor.value, limit);
        editor.value = formatHexDigits(digits);
        hiddenInput.value = wireHexFromDigits(digits);
    }

    function closeFixedValuePopovers(modal, exceptPopover = null) {
        modal.querySelectorAll('.pattern-fixed-value-popover').forEach(popover => {
            if (popover !== exceptPopover) popover.remove();
        });
    }

    function bindFixedValuePopover(fieldNode, modal, isProjectMode) {
        const button = fieldNode.querySelector('.pattern-fixed-value-btn');
        const hiddenInput = fieldNode.querySelector('.pattern-field-value');
        if (!button || !hiddenInput) return;

        button.addEventListener('click', function(event) {
            event.preventDefault();
            event.stopPropagation();
            if (!isProjectMode || button.disabled) return;

            const existing = fieldNode.querySelector('.pattern-fixed-value-popover');
            closeFixedValuePopovers(modal, existing);
            if (existing) {
                existing.remove();
                return;
            }

            const popover = document.createElement('div');
            popover.className = 'pattern-fixed-value-popover';
            const fixedByteLen = fieldByteLen(fieldNode);
            const fixedDigits = limitedHexDigits(hiddenInput.value, fixedByteLen > 0 ? fixedByteLen * 2 : null);
            popover.innerHTML = `
                <label>固定值</label>
                <input type="hidden" class="pattern-fixed-value-input" value="${escapeHTML(wireHexFromDigits(fixedDigits))}">
                <div class="pattern-wire-hex-input pattern-fixed-value-wire-input">
                    <span class="pattern-hex-prefix">H</span>
                    <input type="text" class="pattern-fixed-value-hex-digits" value="${escapeHTML(formatHexDigits(fixedDigits))}" placeholder="${escapeHTML(hexPlaceholder(fixedByteLen))}" inputmode="text" autocomplete="off" spellcheck="false" aria-label="固定值十六进制字节">
                </div>
                <div class="pattern-fixed-value-hint">左侧 H 固定显示，只填写十六进制字节，字节数必须等于 Byte 长度</div>
                <div class="pattern-fixed-value-actions">
                    <button type="button" class="pattern-fixed-value-save">保存</button>
                    <button type="button" class="pattern-fixed-value-cancel">取消</button>
                </div>
            `;

            const actions = fieldNode.querySelector('.pattern-cell-actions');
            actions.appendChild(popover);
            const input = popover.querySelector('.pattern-fixed-value-hex-digits');
            input.focus();
            if (typeof input.select === 'function') input.select();

            popover.addEventListener('click', function(popoverEvent) {
                popoverEvent.stopPropagation();
            });
            input.addEventListener('input', function() {
                syncFixedValueInputFromEditor(popover, fixedByteLen);
            });
            popover.querySelector('.pattern-fixed-value-save').addEventListener('click', function(saveEvent) {
                saveEvent.preventDefault();
                saveEvent.stopPropagation();
                syncFixedValueInputFromEditor(popover, fixedByteLen);
                hiddenInput.value = popover.querySelector('.pattern-fixed-value-input').value;
                hiddenInput.dataset.displayMode = 'H';
                updateByteLenByType(fieldNode);
                syncValueEditorFromHidden(fieldNode);
                syncValueAvailability(fieldNode, isProjectMode);
                popover.remove();
                refreshPatternModal(modal, isProjectMode);
            });
            popover.querySelector('.pattern-fixed-value-cancel').addEventListener('click', function(cancelEvent) {
                cancelEvent.preventDefault();
                cancelEvent.stopPropagation();
                popover.remove();
            });
            input.addEventListener('keydown', function(keyEvent) {
                if (keyEvent.key === 'Enter') {
                    keyEvent.preventDefault();
                    popover.querySelector('.pattern-fixed-value-save').click();
                } else if (keyEvent.key === 'Escape') {
                    keyEvent.preventDefault();
                    popover.remove();
                }
            });
        });
    }

    function bindPatternFieldNodeActions(fieldNode, patternList, modal, isProjectMode) {
        syncValueAvailability(fieldNode, isProjectMode);
        bindValueDisplayToggle(fieldNode, modal, isProjectMode);
        bindFixedValuePopover(fieldNode, modal, isProjectMode);

        fieldNode.querySelector('.del-field-btn')?.addEventListener('click', function() {
            if (!isProjectMode) return;
            closeFixedValuePopovers(modal);
            fieldNode.remove();
            refreshPatternModal(modal, isProjectMode);
        });

        fieldNode.querySelector('.pattern-field-move-up')?.addEventListener('click', function() {
            if (!isProjectMode) return;
            const previous = fieldNode.previousElementSibling;
            if (previous) {
                closeFixedValuePopovers(modal);
                patternList.insertBefore(fieldNode, previous);
                refreshPatternModal(modal, isProjectMode);
            }
        });

        fieldNode.querySelector('.pattern-field-move-down')?.addEventListener('click', function() {
            if (!isProjectMode) return;
            const next = fieldNode.nextElementSibling;
            if (next) {
                closeFixedValuePopovers(modal);
                patternList.insertBefore(next, fieldNode);
                refreshPatternModal(modal, isProjectMode);
            }
        });

        fieldNode.addEventListener('input', function(event) {
            if (event.target.classList.contains('pattern-value-editor-input')) {
                syncHiddenValueFromEditor(fieldNode);
                updateByteLenByType(fieldNode);
                syncValueEditorFromHidden(fieldNode);
            }
            refreshPatternModal(modal, isProjectMode);
        });

        fieldNode.addEventListener('change', function(event) {
            if (event.target.classList.contains('pattern-field-type')) {
                const input = fieldNode.querySelector('.pattern-field-value');
                if ((input.dataset.displayMode || 'H') !== 'H' && input.dataset.wireValue) {
                    input.value = input.dataset.wireValue;
                }
                input.dataset.displayMode = 'H';
                updateByteLenByType(fieldNode);
                syncValueEditorFromHidden(fieldNode);
            }
            if (event.target.classList.contains('pattern-field-role')) {
                syncValueAvailability(fieldNode, isProjectMode);
            }
            refreshPatternModal(modal, isProjectMode);
        });
    }

    function appendFieldNode(patternList, fieldNode, modal, isProjectMode) {
        fieldNode.querySelector('.pattern-field-byte-len').readOnly = true;

        if (!isProjectMode) {
            setFieldMetadataReadonly(fieldNode, true);
            fieldNode.querySelector('.del-field-btn').disabled = true;
            fieldNode.querySelector('.pattern-field-move-up').disabled = true;
            fieldNode.querySelector('.pattern-field-move-down').disabled = true;
        }

        bindPatternFieldNodeActions(fieldNode, patternList, modal, isProjectMode);
        patternList.appendChild(fieldNode);
        return fieldNode;
    }

    function createLengthPolicyControlHTML(normalized, isProjectMode) {
        if (!isProjectMode) return '';
        return `
            <div class="pattern-modal-options">
                <div class="form-group pattern-length-policy-group">
                    <label for="pattern-length-policy">长度策略</label>
                    <select id="pattern-length-policy" class="pattern-length-policy" required>
                        <option value="${LengthPolicyValue.BODY_LENGTH}" ${normalized.length_policy === LengthPolicyValue.BODY_LENGTH ? 'selected' : ''}>${LengthPolicyText[LengthPolicyValue.BODY_LENGTH]}</option>
                        <option value="${LengthPolicyValue.TOTAL_LENGTH}" ${normalized.length_policy === LengthPolicyValue.TOTAL_LENGTH ? 'selected' : ''}>${LengthPolicyText[LengthPolicyValue.TOTAL_LENGTH]}</option>
                        <option value="${LengthPolicyValue.NO_LENGTH}" ${normalized.length_policy === LengthPolicyValue.NO_LENGTH ? 'selected' : ''}>${LengthPolicyText[LengthPolicyValue.NO_LENGTH]}</option>
                    </select>
                </div>
                <div class="form-group pattern-byte-order-group">
                    <label for="pattern-byte-order">字节序</label>
                    <select id="pattern-byte-order" class="pattern-byte-order" required>
                        <option value="big" ${normalized.byte_order === 'big' ? 'selected' : ''}>big</option>
                        <option value="little" ${normalized.byte_order === 'little' ? 'selected' : ''}>little</option>
                        <option value="raw" ${normalized.byte_order === 'raw' ? 'selected' : ''}>raw</option>
                    </select>
                </div>
            </div>
        `;
    }

    function createCustomTcpPatternModal(targetElement, fieldTitle, patternInfosMap, statusElement, isSpecial = false, userHandleCb = null) {
        const resolvedTargetElement = typeof targetElement === 'string'
            ? document.getElementById(targetElement)
            : targetElement;
        const resolvedStatusElement = typeof statusElement === 'string'
            ? document.getElementById(statusElement)
            : statusElement;
        const isProjectMode = Boolean(isSpecial);
        const isHeaderValueMode = !isProjectMode && patternInfosMap && patternInfosMap.item_value_scope === 'header';
        const normalized = normalizePatternInfo(patternInfosMap, { onlyCommon: !isProjectMode && !isHeaderValueMode });
        const configModal = document.createElement('div');
        configModal.className = 'modal-overlay';
        configModal.dataset.lengthPolicy = normalized.length_policy || '';
        configModal.dataset.byteOrder = normalized.byte_order || '';
        configModal.dataset.itemValueScope = isHeaderValueMode ? 'header' : '';
        configModal.innerHTML = `
            <div class="config-pattern-modal ${isProjectMode ? 'is-project-pattern' : 'is-item-pattern'}">
                <div class="modal-header">
                    <h3>配置TCP格式信息</h3>
                    <button type="button" class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <form id="config-pattern-modal-form" class="config-pattern-modal-form">
                        ${createLengthPolicyControlHTML(normalized, isProjectMode)}
                        <div class="pattern-summary" aria-live="polite"></div>
                        <div class="pattern-layout-section">
                            <div class="pattern-section-title">字节布局预览</div>
                            <div class="pattern-byte-layout"></div>
                        </div>
                        <div class="pattern-field-info" id="special-pattern-fields">
                            <div class="pattern-field-info-header">
                                <label>${escapeHTML(fieldTitle || '字段配置')}</label>
                                <div class="pattern-field-toolbar">
                                    <button type="button" class="add-field-btn" ${isProjectMode ? '' : 'hidden'}>新增字段</button>
                                    <button type="button" class="sort-field-btn" title="按 Byte 起始位置升序排列">按 Byte 排序</button>
                                </div>
                            </div>
                            <div class="pattern-field-grid-labels" aria-hidden="true">
                                <span>Byte起始</span>
                                <span>名称</span>
                                <span>Byte长度</span>
                                <span>类型</span>
                                <span>角色</span>
                                ${isProjectMode ? '' : '<span>字段值</span>'}
                                <span>操作</span>
                            </div>
                            <div class="pattern-list"></div>
                        </div>
                        <div class="pattern-validation-errors" aria-live="polite"></div>
                        <div class="form-actions">
                            <button type="button" class="clear-btn" ${isProjectMode ? '' : 'hidden'}>清除字段</button>
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="submit" class="confirm-btn">确定</button>
                        </div>
                    </form>
                </div>
            </div>
        `;

        document.body.appendChild(configModal);

        const patternList = configModal.querySelector('.pattern-list');
        normalized.fields.forEach(field => {
            const fieldNode = createPatternField('字段名称', '', field);
            updatePatternField(fieldNode, field, isProjectMode);
            appendFieldNode(patternList, fieldNode, configModal, isProjectMode);
        });

        refreshPatternModal(configModal, isProjectMode);

        configModal.querySelector('.pattern-length-policy')?.addEventListener('change', function() {
            configModal.dataset.lengthPolicy = this.value;
            refreshPatternModal(configModal, isProjectMode);
        });
        configModal.querySelector('.pattern-byte-order')?.addEventListener('change', function() {
            configModal.dataset.byteOrder = this.value;
            refreshPatternModal(configModal, isProjectMode);
        });

        configModal.querySelector('.add-field-btn')?.addEventListener('click', function() {
            const fieldNode = createPatternField('字段名称', '', {
                byte_pos: 0,
                byte_len: 1,
                type: 'UINT8',
                role: 'common',
            });
            appendFieldNode(patternList, fieldNode, configModal, isProjectMode);
            refreshPatternModal(configModal, isProjectMode);
        });

        configModal.querySelector('.sort-field-btn')?.addEventListener('click', function() {
            patternList.replaceChildren(...patternListSortByIdx(patternList));
            refreshPatternModal(configModal, isProjectMode);
        });

        configModal.querySelector('.clear-btn')?.addEventListener('click', function(event) {
            event.stopPropagation();
            if (!confirm('确定要清除当前全部字段吗？')) return;

            patternList.innerHTML = '';
            if (resolvedTargetElement && resolvedTargetElement.dataset) {
                delete resolvedTargetElement.dataset.patternInfos;
            }
            if (resolvedStatusElement) {
                resolvedStatusElement.style.display = 'none';
            }
            refreshPatternModal(configModal, isProjectMode);
        });

        function closeModal() {
            utils.removeDomNode ? utils.removeDomNode(configModal) : document.body.removeChild(configModal);
        }

        configModal.querySelector('.cancel-btn')?.addEventListener('click', closeModal);
        configModal.querySelector('.close-modal')?.addEventListener('click', closeModal);

        configModal.querySelector('#config-pattern-modal-form').addEventListener('submit', async function(event) {
            event.preventDefault();
            event.stopPropagation();

            let model;
            try {
                model = readPatternInfoFromDOM(configModal, isProjectMode);
            } catch (error) {
                renderErrors(configModal, {
                    valid: false,
                    errors: [error.message],
                });
                return;
            }

            const validation = isProjectMode
                ? validateProjectPattern(model)
                : validateItemFields(model.fields, { requireFunctionCode: isHeaderValueMode });
            renderSummary(configModal, model, validation, isProjectMode);
            renderPreview(configModal, model, isProjectMode);
            renderErrors(configModal, validation);

            if (!validation.valid) return;

            const serialized = isProjectMode ? toPatternInfoV2(model) : model;
            if (resolvedTargetElement && resolvedTargetElement.dataset) {
                resolvedTargetElement.dataset.patternInfos = JSON.stringify(serialized);
            }
            if (resolvedStatusElement) {
                resolvedStatusElement.style.display = 'inline';
                resolvedStatusElement.textContent = isProjectMode ? '格式已设置' : `已设置 ${model.fields.filter(field => field.value).length} 个字段`;
            }

            if (userHandleCb) {
                await userHandleCb(serialized);
            }

            closeModal();
        });

        return configModal;
    }

    function normalizeTcpItemCfg(cfg) {
        const source = cfg || {};
        const fields = {};
        if (source.fields && typeof source.fields === 'object' && !Array.isArray(source.fields)) {
            Object.keys(source.fields).forEach(key => {
                if (source.fields[key]) fields[String(key)] = String(source.fields[key]);
            });
        } else if (Array.isArray(source.common_fields)) {
            source.common_fields.forEach(field => {
                if (field && field.value && Number.isFinite(Number(field.byte_pos))) {
                    fields[String(Number(field.byte_pos))] = String(field.value);
                }
            });
        }

        return {
            function_code: source.function_code || source.function_code_filed_value || '',
            fields,
        };
    }

    function patternInfoToItemFields(patternInfo, cfg) {
        const normalizedPattern = normalizePatternInfo(patternInfo);
        const normalizedCfg = normalizeTcpItemCfg(cfg);
        return normalizedPattern.fields
            .filter(field => field.role === 'common')
            .map(field => Object.assign({}, field, {
                value: normalizedCfg.fields[String(field.byte_pos)] || '',
                match: '',
            }));
    }

    function patternInfoToHeaderValueFields(patternInfo, cfg) {
        const normalizedPattern = normalizePatternInfo(patternInfo);
        const normalizedCfg = normalizeTcpItemCfg(cfg);
        return normalizedPattern.fields
            .map(field => Object.assign({}, field, {
                value: field.role === 'function_code'
                    ? normalizedCfg.function_code
                    : (field.role === 'common'
                        ? (normalizedCfg.fields[String(field.byte_pos)] || '')
                        : (field.role === 'start_magic' ? field.match : '')),
                match: '',
                value_editable: field.role === 'function_code' || field.role === 'common',
            }));
    }

    function buildTcpItemCfg(functionCode, itemFields) {
        const fields = {};
        (Array.isArray(itemFields) ? itemFields : []).forEach(field => {
            if (field && field.value) {
                fields[String(field.byte_pos)] = field.value;
            }
        });
        return {
            function_code: functionCode || '',
            fields,
        };
    }

    function buildTcpHeaderValueCfg(itemFields) {
        const fields = {};
        let functionCode = '';
        (Array.isArray(itemFields) ? itemFields : []).forEach(field => {
            if (!field || !field.value) return;
            if (field.role === 'function_code') {
                functionCode = field.value;
                return;
            }
            if (field.role === 'common') {
                fields[String(field.byte_pos)] = field.value;
            }
        });
        return {
            function_code: functionCode,
            fields,
        };
    }

    function findFunctionCodeField(patternInfo) {
        return normalizePatternInfo(patternInfo).fields.find(field => field.role === 'function_code') || null;
    }

    function validateTcpItemCfg(patternInfo, cfg) {
        const normalizedPattern = normalizePatternInfo(patternInfo);
        const normalizedCfg = normalizeTcpItemCfg(cfg);
        const errors = [];
        const functionField = normalizedPattern.fields.find(field => field.role === 'function_code');

        if (!normalizedCfg.function_code) {
            errors.push('功能码不能为空');
        } else if (!isWireHex(normalizedCfg.function_code)) {
            errors.push('功能码必须是 H 开头的十六进制字节串');
        } else if (functionField && wireHexByteLength(normalizedCfg.function_code) !== Number(functionField.byte_len)) {
            errors.push(`功能码字节数必须等于 ${functionField.byte_len}`);
        }

        Object.keys(normalizedCfg.fields).forEach(bytePos => {
            const field = normalizedPattern.fields.find(item => String(item.byte_pos) === String(bytePos));
            const value = normalizedCfg.fields[bytePos];
            if (!field) {
                errors.push(`普通字段 ${bytePos} 不存在于项目 TCP 格式`);
                return;
            }
            if (!isWireHex(value)) {
                errors.push(`${field.name || bytePos}：字段值必须是 H 开头的十六进制字节串`);
            } else if (wireHexByteLength(value) !== Number(field.byte_len)) {
                errors.push(`${field.name || bytePos}：字段值字节数必须等于 ${field.byte_len}`);
            }
        });

        return {
            valid: errors.length === 0,
            errors,
        };
    }

    KitProxy.tcpPatternEditor = {
        normalizePatternInfo,
        serializePatternInfo,
        validatePatternInfo,
        sortPatternFields,
        computeCommonFieldBytePositions,
        toPatternInfoV2,
        normalizeTcpItemCfg,
        patternInfoToItemFields,
        patternInfoToHeaderValueFields,
        buildTcpItemCfg,
        buildTcpHeaderValueCfg,
        findFunctionCodeField,
        validateTcpItemCfg,
        isWireHex,
        wireHexByteLength,
        displayFromWireHex,
        wireHexFromDisplay,
        getTypeByteLen,
    };

    global.updatePatternField = updatePatternField;
    global.createPatternField = createPatternField;
    global.patternListSortByIdx = patternListSortByIdx;
    global.createCustomTcpPatternModal = createCustomTcpPatternModal;
})(typeof window !== 'undefined' ? window : globalThis);
