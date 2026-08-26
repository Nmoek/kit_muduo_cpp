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
    const STRING_MAX_BYTES = 32;
    const PATTERN_BYTE_LAYOUT_MAX_SPAN = 10;
    const FIELD_REORDER_ANIMATION_MS = 200;
    const FIELD_DELETE_HOLD_MS = 220;
    const FIELD_DELETE_ANIMATION_MS = 420;
    const FIELD_FEEDBACK_MS = 820;

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

    function isAutoFilledLengthRole(role) {
        return role === 'body_length' || role === 'total_length';
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

    /**
     * 字段行按钮使用 CSS mask 加载 assets/icons 下的 SVG，按钮文本只保留在 aria-label/title 中。
     * @param {string} name 图标名称。
     * @returns {string} 图标 HTML。
     */
    function patternActionIconHTML(name) {
        return `<span class="pattern-action-icon pattern-action-icon-${escapeHTML(name)}" aria-hidden="true"></span>`;
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

    function isAsciiBytes(bytes) {
        return Array.from(bytes || []).every(byte => Number(byte) >= 0 && Number(byte) <= 0x7f);
    }

    function assertAsciiString(text) {
        const value = String(text == null ? '' : text);
        if (!/^[\x00-\x7f]*$/.test(value)) {
            throw new Error('STR字段值只能包含ASCII字符');
        }
        if (utf8Bytes(value).length > STRING_MAX_BYTES) {
            throw new Error(`STR字段值长度不能超过${STRING_MAX_BYTES}字节`);
        }
        return value;
    }

    function sanitizeAsciiString(text) {
        return String(text == null ? '' : text)
            .replace(/[^\x00-\x7f]/g, '')
            .slice(0, STRING_MAX_BYTES);
    }

    function asciiStringFromBytes(bytes) {
        if (bytes.length > STRING_MAX_BYTES) {
            throw new Error(`STR字段值长度不能超过${STRING_MAX_BYTES}字节`);
        }
        if (!isAsciiBytes(bytes)) {
            throw new Error('STR字段值只能包含ASCII字符');
        }
        return String.fromCharCode(...bytes);
    }

    function validateStringWireValue(value, label, expectedByteLen = null) {
        if (!value || (isWireHex(value) && wireHexByteLength(value) === 0)) {
            return [`${label}：STR字段值不能为空`];
        }
        if (!isWireHex(value)) {
            return [`${label}：字段值必须是 H 开头的十六进制字节串`];
        }

        const bytes = bytesFromWireHex(value);
        if (bytes.length > STRING_MAX_BYTES) {
            return [`${label}：STR字段值长度不能超过${STRING_MAX_BYTES}字节`];
        }
        if (!isAsciiBytes(bytes)) {
            return [`${label}：STR字段值只能包含ASCII字符`];
        }
        if (Number.isInteger(Number(expectedByteLen))
            && bytes.length !== Number(expectedByteLen)) {
            return [`${label}：字段值字节数必须等于 ${expectedByteLen}`];
        }
        return [];
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
            return asciiStringFromBytes(bytes);
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
            return wireHexFromBytes(utf8Bytes(assertAsciiString(text)));
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
            return null;
        }
        return null;
    }

    function fieldFromNode(node) {
        if (!node || typeof node.querySelector !== 'function') return {};

        return {
            name: node.querySelector('.pattern-field-name')?.value || '',
            byte_pos: toFiniteNumber(node.querySelector('.pattern-field-byte-pos')?.value, null),
            byte_len: readFieldByteLenValue(node),
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
        const byteOrder = normalizeByteOrder(source.byte_order || source.default_order, 'big');
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
            const isStringField = String(field.type || '').toUpperCase() === 'STR';
            if (!String(field.name || '').trim()) errors.push(`${label}：字段名称不能为空`);
            if (!Number.isInteger(field.byte_pos) || field.byte_pos < 0) errors.push(`${label}：Byte 起始位置必须是非负整数`);
            if (!Number.isInteger(field.byte_len) || field.byte_len <= 0) {
                errors.push(isStringField
                    ? `${label}：STR Byte 长度必须是 1~${STRING_MAX_BYTES} 的整数`
                    : `${label}：Byte 长度必须是大于 0 的整数`);
            } else if (isStringField && field.byte_len > STRING_MAX_BYTES) {
                errors.push(`${label}：STR Byte 长度必须是 1~${STRING_MAX_BYTES} 的整数`);
            }
            if (!String(field.type || '').trim()) errors.push(`${label}：类型不能为空`);
            if (!String(field.role || '').trim()) errors.push(`${label}：角色不能为空`);

            const expectedLen = getTypeByteLen(field.type, field.role === 'start_magic' ? field.match : field.value);
            if (!isStringField && Number.isFinite(expectedLen) && Number(field.byte_len) !== Number(expectedLen)) {
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
            const label = field.name || `byte_pos ${field.byte_pos}`;
            const isStringField = String(field.type || '').toUpperCase() === 'STR';
            if (isStringField) {
                errors.push(...validateStringWireValue(field.value, label, field.byte_len));
                return;
            }
            if (!field.value) {
                return;
            }
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

    function roleOptionsHTML(selectedRole, roles = FIELD_ROLES) {
        const normalizedRoles = Array.isArray(roles) && roles.length ? roles : FIELD_ROLES;
        return normalizedRoles.map(role => {
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
        const byteLen = readFieldByteLenValue(fieldNode);
        return Number.isInteger(byteLen) && byteLen > 0 ? byteLen : 0;
    }

    function readFieldByteLenValue(fieldNode) {
        const input = fieldNode?.querySelector('.pattern-field-byte-len');
        if (!input) return null;

        const value = toFiniteNumber(input.value, null);
        return Number.isFinite(value) ? value : null;
    }

    /**
     * STR Byte 长度只保留数字，并把可显示值限制在 1~STRING_MAX_BYTES。
     * @param {HTMLElement} fieldNode 字段行节点。
     */
    function sanitizeStringByteLenInput(fieldNode) {
        const input = fieldNode?.querySelector('.pattern-field-byte-len');
        const type = fieldNode?.querySelector('.pattern-field-type')?.value || '';
        if (!input || String(type).toUpperCase() !== 'STR') return;

        const digits = String(input.value || '').replace(/\D/g, '');
        if (!digits) {
            input.value = '';
            return;
        }

        const parsed = Number.parseInt(digits, 10);
        const clamped = Number.isFinite(parsed)
            ? Math.min(Math.max(parsed, 1), STRING_MAX_BYTES)
            : STRING_MAX_BYTES;
        input.value = String(clamped);
    }

    function patternByteLayoutSpan(byteLen) {
        return Math.min(PATTERN_BYTE_LAYOUT_MAX_SPAN, Math.max(1, Number(byteLen) || 1));
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
        const role = fieldNode.querySelector('.pattern-field-role')?.value || 'common';
        const mode = hiddenInput?.dataset.displayMode || 'H';
        if (!hiddenInput || !editor || !wireInput) return;

        if (isAutoFilledLengthRole(role)) {
            hiddenInput.value = '';
            hiddenInput.dataset.displayMode = 'H';
            delete hiddenInput.dataset.wireValue;
            editor.value = '';
            editor.placeholder = '自动填充';
            editor.removeAttribute('maxlength');
            wireInput.classList.remove('is-display-value', 'is-display-str');
            updateValueDisplayButton(fieldNode);
            return;
        }

        wireInput.classList.toggle('is-display-value', mode !== 'H');
        wireInput.classList.toggle('is-display-str', mode === 'S');
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
            editor.placeholder = mode === 'S' ? 'ASCII字符串真值' : (mode === 'F' ? '小数真值' : '十进制真值');
            if (mode === 'S') {
                const byteLen = fieldByteLen(fieldNode);
                editor.maxLength = String(byteLen > 0 ? Math.min(byteLen, STRING_MAX_BYTES) : STRING_MAX_BYTES);
            } else {
                editor.removeAttribute('maxlength');
            }
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
        } else if ((hiddenInput.dataset.displayMode || 'H') === 'S') {
            const byteLen = fieldByteLen(fieldNode);
            const value = sanitizeAsciiString(editor.value)
                .slice(0, byteLen > 0 ? Math.min(byteLen, STRING_MAX_BYTES) : STRING_MAX_BYTES);
            editor.value = value;
            hiddenInput.value = value;
        } else {
            hiddenInput.value = editor.value;
        }
        updateValueDisplayButton(fieldNode);
    }

    function getFieldWireInputValue(field, isProjectMode) {
        if (isProjectMode && field.role === 'start_magic') return field.match || field.value || '';
        return field.value || '';
    }

    function updateByteLenByType(fieldNode, options = {}) {
        const typeSelect = fieldNode.querySelector('.pattern-field-type');
        const valueInput = fieldNode.querySelector('.pattern-field-value');
        const byteLenInput = fieldNode.querySelector('.pattern-field-byte-len');
        const isStringField = String(typeSelect?.value || '').toUpperCase() === 'STR';
        if (isStringField) {
            byteLenInput.min = '1';
            byteLenInput.max = String(STRING_MAX_BYTES);
            byteLenInput.placeholder = '1~32';
            byteLenInput.setAttribute('inputmode', 'numeric');
            byteLenInput.setAttribute('pattern', '[0-9]*');
            return;
        }

        byteLenInput.min = '1';
        byteLenInput.max = '65535';
        byteLenInput.placeholder = '自动';
        const byteLen = getTypeByteLen(typeSelect.value, valueInput.value);

        if (Number.isFinite(byteLen)) {
            byteLenInput.value = String(byteLen);
        }
    }

    function syncByteLengthAvailability(fieldNode, isProjectMode, options = {}) {
        const byteLenInput = fieldNode.querySelector('.pattern-field-byte-len');
        const type = fieldNode.querySelector('.pattern-field-type')?.value || '';
        if (!byteLenInput) return;

        const isStringField = String(type).toUpperCase() === 'STR';
        const editable = Boolean(isProjectMode)
            && fieldNode.dataset.structureEditable !== 'false'
            && isStringField;
        byteLenInput.readOnly = !editable;
        byteLenInput.disabled = !editable;
        byteLenInput.required = Boolean(isProjectMode && isStringField);
        if (editable) {
            sanitizeStringByteLenInput(fieldNode);
            const defaultByteLen = Number(options.defaultStringByteLen);
            if (!byteLenInput.value && Number.isInteger(defaultByteLen)
                && defaultByteLen >= 1 && defaultByteLen <= STRING_MAX_BYTES) {
                byteLenInput.value = String(defaultByteLen);
            }
        }
    }

    function initializeStringDisplayMode(fieldNode) {
        const hiddenInput = fieldNode.querySelector('.pattern-field-value');
        const type = fieldNode.querySelector('.pattern-field-type')?.value || '';
        if (!hiddenInput || String(type).toUpperCase() !== 'STR') return;

        const sourceValue = hiddenInput.dataset.displayMode !== 'H' && hiddenInput.dataset.wireValue
            ? hiddenInput.dataset.wireValue
            : hiddenInput.value.trim();
        try {
            if (!sourceValue) {
                hiddenInput.value = '';
            } else if (isWireHex(sourceValue)) {
                hiddenInput.dataset.wireValue = sourceValue;
                hiddenInput.value = displayFromWireHex(sourceValue, 'STR');
            } else {
                hiddenInput.value = assertAsciiString(sourceValue);
                delete hiddenInput.dataset.wireValue;
            }
            hiddenInput.dataset.displayMode = 'S';
        } catch (error) {
            hiddenInput.dataset.displayMode = 'H';
            hiddenInput.value = sourceValue;
        }
        updateByteLenByType(fieldNode);
        syncValueEditorFromHidden(fieldNode);
    }

    function syncValueAvailability(fieldNode, isProjectMode) {
        const role = fieldNode.querySelector('.pattern-field-role')?.value || 'common';
        const autoFilled = isAutoFilledLengthRole(role);
        const valueInput = fieldNode.querySelector('.pattern-field-value');
        const valueEditor = fieldNode.querySelector('.pattern-value-editor-input');
        const wireInput = fieldNode.querySelector('.pattern-wire-hex-input');
        const displayButton = fieldNode.querySelector('.pattern-field-value-display-btn');
        const fixedValueButton = fieldNode.querySelector('.pattern-fixed-value-btn');
        const roleCell = fieldNode.querySelector('.pattern-cell-role');
        const label = fieldNode.querySelector('.pattern-cell-value label');

        if (isProjectMode) {
            const enabled = role === 'start_magic';
            if (roleCell) roleCell.classList.toggle('has-fixed-value-control', enabled);
            valueInput.disabled = !enabled;
            if (valueEditor) valueEditor.disabled = !enabled;
            if (!enabled) {
                valueInput.value = '';
                valueInput.dataset.displayMode = 'H';
                delete valueInput.dataset.wireValue;
            }
            displayButton.disabled = !enabled;
            if (fixedValueButton) {
                const hasFixedValue = Boolean(enabled && valueInput.value);
                fixedValueButton.hidden = !enabled;
                fixedValueButton.disabled = !enabled;
                fixedValueButton.classList.toggle('has-fixed-value', hasFixedValue);
                fixedValueButton.title = hasFixedValue ? '固定值已配置' : '固定值未配置';
                fixedValueButton.setAttribute('aria-label', fixedValueButton.title);
            }
            if (label) label.textContent = '固定值';
        } else {
            const enabled = !autoFilled && fieldNode.dataset.valueEditable !== 'false';
            valueInput.disabled = !enabled;
            if (valueEditor) valueEditor.disabled = !enabled;
            if (wireInput) wireInput.classList.toggle('is-readonly', !enabled);
            displayButton.disabled = !enabled;
            if (fixedValueButton) fixedValueButton.hidden = true;
            if (roleCell) roleCell.classList.remove('has-fixed-value-control');
            if (label) label.textContent = '字段值';
        }
        if (valueEditor) {
            valueEditor.required = !isProjectMode
                && !valueEditor.disabled
                && String(fieldNode.querySelector('.pattern-field-type')?.value || '').toUpperCase() === 'STR';
            valueEditor.setAttribute('aria-label', autoFilled ? '字段值自动填充' : '字段值十六进制字节');
        }
        syncValueEditorFromHidden(fieldNode);
    }

    function createDefaultPatternFieldInfo() {
        return {
            byte_pos: 0,
            byte_len: 1,
            type: 'UINT8',
            role: 'common',
        };
    }

    function createBlankPatternFieldInfo() {
        return {
            byte_pos: 0,
            byte_len: '',
            type: '',
            role: 'common',
            value: '',
            match: '',
        };
    }

    function patternFieldRows(patternList) {
        return Array.from(patternList?.children || [])
            .filter(child => child.classList && child.classList.contains('pattern-field-container'));
    }

    function activePatternFieldRows(patternList) {
        return patternFieldRows(patternList).filter(fieldNode => {
            return fieldNode.dataset.deleting !== 'true'
                && !fieldNode.classList.contains('is-delete-marked')
                && !fieldNode.classList.contains('is-deleting');
        });
    }

    function canRemovePatternField(patternList) {
        return activePatternFieldRows(patternList).length > 1;
    }

    /**
     * 字段列表至少保留一条占位字段，删除按钮状态由列表统一刷新，避免 TCP 和 Body Binary 出现不同步。
     * @param {HTMLElement} patternList 字段列表容器。
     */
    function updatePatternFieldDeleteButtonStates(patternList) {
        if (!patternList) return;

        const rows = patternFieldRows(patternList);
        const disableDelete = activePatternFieldRows(patternList).length <= 1;
        rows.forEach(fieldNode => {
            const deleteButton = fieldNode.querySelector('.del-field-btn');
            if (!deleteButton) return;

            const structureEditable = fieldNode.dataset.structureEditable !== 'false' && !deleteButton.hidden;
            const deleting = fieldNode.dataset.deleting === 'true'
                || fieldNode.classList.contains('is-delete-marked')
                || fieldNode.classList.contains('is-deleting');
            deleteButton.disabled = !structureEditable || deleting || disableDelete;
            deleteButton.title = disableDelete ? '至少保留一个字段' : '删除字段';
        });
    }

    function scheduleAnimationFrame(callback) {
        if (typeof global.requestAnimationFrame === 'function') {
            global.requestAnimationFrame(callback);
            return;
        }
        global.setTimeout(callback, 0);
    }

    function prefersReducedMotion() {
        return typeof global.matchMedia === 'function'
            && global.matchMedia('(prefers-reduced-motion: reduce)').matches;
    }

    function animatePatternListReorder(patternList, mutate) {
        if (!patternList || typeof mutate !== 'function') return;

        const beforeRects = new Map();
        patternFieldRows(patternList).forEach(row => {
            beforeRects.set(row, row.getBoundingClientRect());
        });

        mutate();

        patternFieldRows(patternList).forEach(row => {
            const beforeRect = beforeRects.get(row);
            if (!beforeRect) return;

            const afterRect = row.getBoundingClientRect();
            const offsetX = beforeRect.left - afterRect.left;
            const offsetY = beforeRect.top - afterRect.top;
            if (!offsetX && !offsetY) return;

            row.style.transition = 'none';
            row.style.transform = `translate(${offsetX}px, ${offsetY}px)`;
            row.style.willChange = 'transform';

            scheduleAnimationFrame(() => {
                row.style.transition = `transform ${FIELD_REORDER_ANIMATION_MS}ms ease`;
                row.style.transform = '';
                global.setTimeout(() => {
                    row.style.transition = '';
                    row.style.willChange = '';
                }, FIELD_REORDER_ANIMATION_MS);
            });
        });
    }

    function markPatternFieldChanged(fieldNode, changeType) {
        if (!fieldNode) return;

        clearPatternFieldFeedback(fieldNode);
        void fieldNode.offsetWidth;
        fieldNode.classList.add(changeType === 'added' ? 'is-added' : 'is-moved');

        const timer = global.setTimeout(() => {
            fieldNode.classList.remove('is-added', 'is-moved');
            fieldNode.__patternFeedbackTimer = null;
        }, FIELD_FEEDBACK_MS);
        fieldNode.__patternFeedbackTimer = timer;
    }

    function clearPatternFieldFeedback(fieldNode) {
        if (!fieldNode) return;

        if (fieldNode.__patternFeedbackTimer) {
            global.clearTimeout(fieldNode.__patternFeedbackTimer);
            fieldNode.__patternFeedbackTimer = null;
        }

        fieldNode.classList.remove('is-added', 'is-moved');
    }

    function cancelPatternFieldAnimations(fieldNode) {
        if (!fieldNode || typeof fieldNode.getAnimations !== 'function') return;

        fieldNode.getAnimations({ subtree: true }).forEach(animation => {
            try {
                animation.cancel();
            } catch (error) {
                // 已完成或被浏览器回收的动画可以忽略。
            }
        });
    }

    /**
     * 根据字段在列表中的展示顺序，按 Byte 长度连续回填 Byte 起始位置。
     * @param {HTMLElement} patternList 字段列表容器。
     */
    function recalculatePatternFieldBytePositions(patternList) {
        if (!patternList) return;

        let byteCursor = 0;
        patternList.querySelectorAll('.pattern-field-container').forEach(fieldNode => {
            if (fieldNode.dataset.deleting === 'true' || fieldNode.classList.contains('is-deleting')) return;
            const bytePosInput = fieldNode.querySelector('.pattern-field-byte-pos');
            const byteLen = toFiniteNumber(fieldNode.querySelector('.pattern-field-byte-len')?.value, 0);
            if (bytePosInput) bytePosInput.value = String(byteCursor);
            byteCursor += Math.max(0, Number(byteLen) || 0);
        });
    }

    /**
     * 字段列表被清空时保留一个可新增的空状态行。
     * @param {HTMLElement} patternList 字段列表容器。
     * @param {HTMLElement} modal 当前 TCP 格式配置弹窗。
     * @param {boolean} isProjectMode 是否为项目格式字段编辑。
     */
    function syncPatternListEmptyState(patternList, modal, isProjectMode) {
        if (!patternList) return;

        let emptyRow = null;
        Array.from(patternList.children).forEach(child => {
            if (child.classList && child.classList.contains('pattern-empty-field-row')) {
                emptyRow = child;
            }
        });

        const hasFields = Boolean(patternList.querySelector('.pattern-field-container'));
        if (hasFields) {
            if (emptyRow) emptyRow.remove();
            return;
        }

        if (emptyRow) return;

        emptyRow = document.createElement('div');
        emptyRow.className = 'pattern-empty-field-row';
        emptyRow.innerHTML = `
            <div class="pattern-field pattern-empty-field">
                <div class="pattern-cell pattern-empty-message">
                    <span>暂无字段</span>
                </div>
                <div class="pattern-cell pattern-cell-actions">
                    <label>操作</label>
                    <div class="pattern-field-actions">
                        <button type="button" class="add-field-btn" title="新增字段" aria-label="新增字段" ${isProjectMode ? '' : 'hidden disabled'}>${patternActionIconHTML('add')}</button>
                    </div>
                </div>
            </div>
        `;
        emptyRow.querySelector('.add-field-btn')?.addEventListener('click', function() {
            if (!isProjectMode) return;
            insertNewPatternFieldAfter(patternList, null, modal, isProjectMode);
        });
        patternList.appendChild(emptyRow);
    }

    /**
     * 在指定字段行下方新增字段，并触发 Byte 起始位置重算。
     * @param {HTMLElement} patternList 字段列表容器。
     * @param {?HTMLElement} anchorFieldNode 作为插入锚点的字段行；为空时追加到列表。
     * @param {HTMLElement} modal 当前 TCP 格式配置弹窗。
     * @param {boolean} isProjectMode 是否为项目格式字段编辑。
     * @returns {?HTMLElement} 新增的字段行。
     */
    function insertNewPatternFieldAfter(patternList, anchorFieldNode, modal, isProjectMode) {
        if (!isProjectMode || !patternList) return null;

        closeFixedValuePopovers(modal);
        const fieldNode = createPatternField('字段名称', '', createDefaultPatternFieldInfo());
        animatePatternListReorder(patternList, () => {
            appendFieldNode(patternList, fieldNode, modal, isProjectMode, anchorFieldNode);
        });
        recalculatePatternFieldBytePositions(patternList);
        refreshPatternModal(modal, isProjectMode);
        markPatternFieldChanged(fieldNode, 'added');

        const nameInput = fieldNode.querySelector('.pattern-field-name');
        if (nameInput && typeof nameInput.focus === 'function') {
            nameInput.focus();
        }
        return fieldNode;
    }

    function createPatternField(namePlaceholder = '', specialName = '', fieldInfo = null, options = {}) {
        const field = cloneField(fieldInfo || {});
        if (specialName && (!field.role || field.role === 'common')) {
            field.role = roleFromLegacySpecialKey(specialName);
        }
        const roleOptions = options.roleOptions || FIELD_ROLES;

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
                    <input type="text" class="pattern-field-byte-len" value="${escapeHTML(field.byte_len ?? '')}" min="1" max="65535" inputmode="numeric" pattern="[0-9]*" placeholder="自动" readonly required>
                </div>
                <div class="pattern-cell pattern-cell-type">
                    <label>类型</label>
                    <select class="pattern-field-type" required>${patternFieldTypeOptionsHTML(field.type)}</select>
                </div>
                <div class="pattern-cell pattern-cell-role">
                    <label>角色</label>
                    <div class="pattern-role-control">
                        <select class="pattern-field-role" required>${roleOptionsHTML(field.role, roleOptions)}</select>
                        <button type="button" class="pattern-fixed-value-btn" title="固定值未配置" aria-label="固定值未配置" hidden>固</button>
                    </div>
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
                        <button type="button" class="add-field-btn" title="在下方新增字段" aria-label="在下方新增字段">${patternActionIconHTML('add')}</button>
                        <button type="button" class="pattern-field-move-up" title="上移" aria-label="上移">${patternActionIconHTML('move-up')}</button>
                        <button type="button" class="pattern-field-move-down" title="下移" aria-label="下移">${patternActionIconHTML('move-down')}</button>
                        <button type="button" class="del-field-btn" title="删除字段" aria-label="删除字段">${patternActionIconHTML('delete')}</button>
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
        updateByteLenByType(fieldNode, { isProjectMode: Boolean(isSpecial) });
        syncValueEditorFromHidden(fieldNode);
        return fieldNode;
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
            if (fieldNode.dataset.deleting === 'true' || fieldNode.classList.contains('is-deleting')) return;
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
                <div class="pattern-byte-block ${roleClass} ${valid ? '' : 'is-error'}" data-role="${escapeHTML(field.role || 'common')}" style="--pattern-span:${patternByteLayoutSpan(field.byte_len)}">
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
        updatePatternFieldDeleteButtonStates(modal?.querySelector('.pattern-list'));
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

    function bindValueDisplayToggle(fieldNode, modal, isProjectMode, options = {}) {
        const input = fieldNode.querySelector('.pattern-field-value');
        const button = fieldNode.querySelector('.pattern-field-value-display-btn');
        const typeSelect = fieldNode.querySelector('.pattern-field-type');
        const onRefresh = typeof options.onRefresh === 'function'
            ? options.onRefresh
            : () => refreshPatternModal(modal, isProjectMode);
        const onError = typeof options.onError === 'function'
            ? options.onError
            : error => renderErrors(modal, {
                valid: false,
                errors: [error.message],
            });
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
                updateByteLenByType(fieldNode, { isProjectMode });
                syncValueEditorFromHidden(fieldNode);
                onRefresh();
            } catch (error) {
                onError(error);
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
        modal.querySelectorAll('.pattern-field-container.is-fixed-value-popover-open').forEach(fieldNode => {
            if (!exceptPopover || fieldNode.querySelector('.pattern-fixed-value-popover') !== exceptPopover) {
                fieldNode.classList.remove('is-fixed-value-popover-open');
            }
        });
    }

    function bindFixedValuePopover(fieldNode, patternList, modal, isProjectMode) {
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
                fieldNode.classList.remove('is-fixed-value-popover-open');
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

            const popoverHost = button.closest('.pattern-cell-role') || fieldNode.querySelector('.pattern-cell-actions');
            popoverHost.appendChild(popover);
            fieldNode.classList.add('is-fixed-value-popover-open');
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
                updateByteLenByType(fieldNode, { isProjectMode });
                syncValueEditorFromHidden(fieldNode);
                syncValueAvailability(fieldNode, isProjectMode);
                popover.remove();
                fieldNode.classList.remove('is-fixed-value-popover-open');
                recalculatePatternFieldBytePositions(patternList);
                refreshPatternModal(modal, isProjectMode);
            });
            popover.querySelector('.pattern-fixed-value-cancel').addEventListener('click', function(cancelEvent) {
                cancelEvent.preventDefault();
                cancelEvent.stopPropagation();
                fieldNode.classList.remove('is-fixed-value-popover-open');
                popover.remove();
            });
            input.addEventListener('keydown', function(keyEvent) {
                if (keyEvent.key === 'Enter') {
                    keyEvent.preventDefault();
                    popover.querySelector('.pattern-fixed-value-save').click();
                } else if (keyEvent.key === 'Escape') {
                    keyEvent.preventDefault();
                    fieldNode.classList.remove('is-fixed-value-popover-open');
                    popover.remove();
                }
            });
        });
    }

    function animatePatternFieldRemovalCore(fieldNode, patternList, options = {}) {
        if (!fieldNode || !patternList || fieldNode.dataset.deleting === 'true') return false;
        if (!canRemovePatternField(patternList)) {
            updatePatternFieldDeleteButtonStates(patternList);
            return false;
        }

        fieldNode.dataset.deleting = 'true';
        closeFixedValuePopovers(options.popoverRoot || patternList);
        fieldNode.querySelectorAll('button, input, select').forEach(control => {
            control.disabled = true;
        });
        updatePatternFieldDeleteButtonStates(patternList);
        clearPatternFieldFeedback(fieldNode);
        cancelPatternFieldAnimations(fieldNode);
        fieldNode.style.transform = '';
        fieldNode.style.willChange = '';

        const rowHeight = Math.max(
            fieldNode.getBoundingClientRect().height,
            fieldNode.querySelector('.pattern-field')?.getBoundingClientRect().height || 0,
            fieldNode.scrollHeight || 0,
        );
        fieldNode.style.overflow = 'hidden';
        if (rowHeight > 0) {
            fieldNode.style.height = `${rowHeight}px`;
        }
        fieldNode.style.transition = '';
        fieldNode.style.opacity = '1';
        fieldNode.classList.add('is-delete-marked');
        void fieldNode.offsetHeight;

        const holdMs = prefersReducedMotion() ? 0 : FIELD_DELETE_HOLD_MS;
        const collapseMs = prefersReducedMotion() ? 0 : FIELD_DELETE_ANIMATION_MS;
        let removalFinished = false;
        function finishRemoval() {
            if (removalFinished) return;
            removalFinished = true;
            if (fieldNode.parentNode === patternList) {
                fieldNode.remove();
            }
            if (typeof options.onFinish === 'function') {
                options.onFinish(fieldNode);
            }
            updatePatternFieldDeleteButtonStates(patternList);
        }

        global.setTimeout(() => {
            fieldNode.classList.add('is-deleting');

            if (collapseMs > 0 && typeof fieldNode.animate === 'function') {
                fieldNode.style.transition = 'none';
                fieldNode.style.height = `${rowHeight}px`;
                fieldNode.style.opacity = '1';
                const deleteAnimation = fieldNode.animate([
                    { height: `${rowHeight}px`, opacity: 1 },
                    { height: '0px', opacity: 0 },
                ], {
                    duration: collapseMs,
                    easing: 'ease',
                    fill: 'forwards',
                });
                fieldNode.querySelector('.pattern-field')?.animate([
                    { transform: 'scale(1)' },
                    { transform: 'scale(0.995)' },
                ], {
                    duration: collapseMs,
                    easing: 'ease',
                    fill: 'forwards',
                });
                deleteAnimation.finished.then(finishRemoval).catch(finishRemoval);
                global.setTimeout(finishRemoval, collapseMs + 80);
                return;
            }

            fieldNode.style.transition = collapseMs > 0
                ? `height ${collapseMs}ms ease, opacity ${Math.min(collapseMs, 360)}ms ease`
                : 'none';
            void fieldNode.offsetHeight;
            scheduleAnimationFrame(() => {
                fieldNode.style.opacity = '0';
                fieldNode.style.height = '0px';
            });
            global.setTimeout(finishRemoval, collapseMs + 40);
        }, holdMs);

        return true;
    }

    function animatePatternFieldRemoval(fieldNode, patternList, modal, isProjectMode) {
        animatePatternFieldRemovalCore(fieldNode, patternList, {
            popoverRoot: modal,
            onFinish: function() {
                recalculatePatternFieldBytePositions(patternList);
                syncPatternListEmptyState(patternList, modal, isProjectMode);
                refreshPatternModal(modal, isProjectMode);
            },
        });
    }

    function bindPatternFieldNodeActions(fieldNode, patternList, modal, isProjectMode, options = {}) {
        const canEditStructure = options.canEditStructure != null
            ? Boolean(options.canEditStructure)
            : Boolean(isProjectMode);
        const autoRecalculateBytePositions = options.autoRecalculateBytePositions !== false && canEditStructure;
        const valueAvailabilityProjectMode = options.valueAvailabilityProjectMode != null
            ? Boolean(options.valueAvailabilityProjectMode)
            : Boolean(isProjectMode);
        const allowStringLengthEdit = options.allowStringLengthEdit != null
            ? Boolean(options.allowStringLengthEdit)
            : Boolean(isProjectMode);
        const byteLengthOptions = {
            defaultStringByteLen: options.defaultStringByteLen,
        };
        const onRefresh = typeof options.onRefresh === 'function'
            ? options.onRefresh
            : () => refreshPatternModal(modal, isProjectMode);
        const onAddAfter = typeof options.onAddAfter === 'function'
            ? options.onAddAfter
            : () => insertNewPatternFieldAfter(patternList, fieldNode, modal, isProjectMode);
        const onDelete = typeof options.onDelete === 'function'
            ? options.onDelete
            : () => animatePatternFieldRemoval(fieldNode, patternList, modal, isProjectMode);

        syncValueAvailability(fieldNode, valueAvailabilityProjectMode);
        bindValueDisplayToggle(fieldNode, modal, isProjectMode, options);
        bindFixedValuePopover(fieldNode, patternList, modal, isProjectMode);

        fieldNode.querySelector('.pattern-field-byte-len')?.addEventListener('beforeinput', function(event) {
            const type = fieldNode.querySelector('.pattern-field-type')?.value || '';
            if (String(type).toUpperCase() !== 'STR') return;
            if (event.data && /\D/.test(event.data)) {
                event.preventDefault();
            }
        });

        fieldNode.querySelector('.add-field-btn')?.addEventListener('click', function() {
            if (!canEditStructure) return;
            onAddAfter(fieldNode);
        });

        fieldNode.querySelector('.del-field-btn')?.addEventListener('click', function() {
            if (!canEditStructure) return;
            if (!canRemovePatternField(patternList)) {
                updatePatternFieldDeleteButtonStates(patternList);
                return;
            }
            onDelete(fieldNode);
        });

        fieldNode.querySelector('.pattern-field-move-up')?.addEventListener('click', function() {
            if (!canEditStructure) return;
            const previous = fieldNode.previousElementSibling;
            if (previous && previous.classList.contains('pattern-field-container')) {
                closeFixedValuePopovers(modal);
                animatePatternListReorder(patternList, () => {
                    patternList.insertBefore(fieldNode, previous);
                });
                if (autoRecalculateBytePositions) recalculatePatternFieldBytePositions(patternList);
                onRefresh();
                markPatternFieldChanged(fieldNode, 'moved');
            }
        });

        fieldNode.querySelector('.pattern-field-move-down')?.addEventListener('click', function() {
            if (!canEditStructure) return;
            const next = fieldNode.nextElementSibling;
            if (next && next.classList.contains('pattern-field-container')) {
                closeFixedValuePopovers(modal);
                animatePatternListReorder(patternList, () => {
                    patternList.insertBefore(next, fieldNode);
                });
                if (autoRecalculateBytePositions) recalculatePatternFieldBytePositions(patternList);
                onRefresh();
                markPatternFieldChanged(fieldNode, 'moved');
            }
        });

        fieldNode.addEventListener('input', function(event) {
            if (event.target.classList.contains('pattern-value-editor-input')) {
                syncHiddenValueFromEditor(fieldNode);
                updateByteLenByType(fieldNode, { isProjectMode });
                syncValueEditorFromHidden(fieldNode);
                if (autoRecalculateBytePositions) {
                    recalculatePatternFieldBytePositions(patternList);
                }
            }
            if (event.target.classList.contains('pattern-field-byte-len') && allowStringLengthEdit) {
                sanitizeStringByteLenInput(fieldNode);
                syncByteLengthAvailability(fieldNode, allowStringLengthEdit, byteLengthOptions);
                syncValueEditorFromHidden(fieldNode);
                if (autoRecalculateBytePositions) {
                    recalculatePatternFieldBytePositions(patternList);
                }
            }
            onRefresh();
        });

        fieldNode.addEventListener('change', function(event) {
            if (event.target.classList.contains('pattern-field-type')) {
                const input = fieldNode.querySelector('.pattern-field-value');
                if ((input.dataset.displayMode || 'H') !== 'H' && input.dataset.wireValue) {
                    input.value = input.dataset.wireValue;
                }
                input.dataset.displayMode = 'H';
                if (!isProjectMode && String(event.target.value || '').toUpperCase() === 'STR') {
                    initializeStringDisplayMode(fieldNode);
                }
                updateByteLenByType(fieldNode, { isProjectMode });
                syncByteLengthAvailability(fieldNode, allowStringLengthEdit, byteLengthOptions);
                syncValueAvailability(fieldNode, valueAvailabilityProjectMode);
                syncValueEditorFromHidden(fieldNode);
                if (autoRecalculateBytePositions) {
                    recalculatePatternFieldBytePositions(patternList);
                }
            }
            if (event.target.classList.contains('pattern-field-byte-len') && allowStringLengthEdit) {
                sanitizeStringByteLenInput(fieldNode);
                syncByteLengthAvailability(fieldNode, allowStringLengthEdit, byteLengthOptions);
                syncValueEditorFromHidden(fieldNode);
                if (autoRecalculateBytePositions) {
                    recalculatePatternFieldBytePositions(patternList);
                }
            }
            if (event.target.classList.contains('pattern-field-role')) {
                syncValueAvailability(fieldNode, valueAvailabilityProjectMode);
            }
            onRefresh();
        });
    }

    function appendFieldNode(patternList, fieldNode, modal, isProjectMode, afterNode = null, options = {}) {
        const canEditStructure = options.canEditStructure != null
            ? Boolean(options.canEditStructure)
            : Boolean(isProjectMode);
        const canEditValues = options.canEditValues !== false;
        const lockRole = options.lockRole === true;
        const shouldOverrideValueEditable = Object.prototype.hasOwnProperty.call(options, 'canEditValues');
        const allowStringLengthEdit = options.allowStringLengthEdit != null
            ? Boolean(options.allowStringLengthEdit)
            : Boolean(isProjectMode);

        fieldNode.dataset.structureEditable = canEditStructure ? 'true' : 'false';
        fieldNode.querySelector('.pattern-field-byte-pos').readOnly = Boolean(options.autoRecalculateBytePositions !== false && canEditStructure);
        syncByteLengthAvailability(fieldNode, allowStringLengthEdit, {
            defaultStringByteLen: options.defaultStringByteLen,
        });

        if (!canEditStructure) {
            setFieldMetadataReadonly(fieldNode, true);
            fieldNode.querySelector('.add-field-btn').hidden = true;
            fieldNode.querySelector('.add-field-btn').disabled = true;
            fieldNode.querySelector('.del-field-btn').disabled = true;
            fieldNode.querySelector('.pattern-field-move-up').disabled = true;
            fieldNode.querySelector('.pattern-field-move-down').disabled = true;
        }
        if (canEditStructure && options.showMoveActions === false) {
            fieldNode.querySelector('.pattern-field-move-up').hidden = true;
            fieldNode.querySelector('.pattern-field-move-down').hidden = true;
        }
        if (lockRole) {
            const roleSelect = fieldNode.querySelector('.pattern-field-role');
            if (roleSelect) {
                roleSelect.value = options.role || 'common';
                roleSelect.disabled = true;
            }
        }
        if (shouldOverrideValueEditable) {
            setFieldValueEditable(fieldNode, canEditValues);
        }

        if (options.defaultStringDisplay) {
            initializeStringDisplayMode(fieldNode);
        }

        bindPatternFieldNodeActions(fieldNode, patternList, modal, isProjectMode, options);
        if (afterNode && afterNode.parentNode === patternList) {
            patternList.insertBefore(fieldNode, afterNode.nextElementSibling);
        } else {
            patternList.appendChild(fieldNode);
        }
        syncPatternListEmptyState(patternList, modal, isProjectMode);
        updatePatternFieldDeleteButtonStates(patternList);
        return fieldNode;
    }

    /**
     * 创建可嵌入的 TCP 字段列表编辑器，供 TCP 弹窗以外的控件复用同一套字段行和值编辑逻辑。
     * @param {HTMLElement} root 字段列表所在根容器。
     * @param {Object} options 编辑器配置。
     * @returns {{ setFields: Function; getFields: Function; addField: Function; resetFields: Function; refresh: Function; destroy: Function; }}
     */
    function createPatternFieldListEditor(root, options = {}) {
        if (!root || typeof root.querySelector !== 'function') {
            throw new Error('TCP 字段列表编辑器缺少挂载容器');
        }

        const fieldList = root.querySelector(options.listSelector || '.pattern-list');
        const preview = root.querySelector(options.previewSelector || '.pattern-byte-layout');
        const countElement = root.querySelector(options.countSelector || '');
        const addButtonSelector = options.addButtonSelector || '';
        const addButton = addButtonSelector ? root.querySelector(addButtonSelector) : null;
        if (!fieldList) {
            throw new Error('TCP 字段列表编辑器缺少字段列表容器');
        }

        const editorRoot = root;
        const mode = options.mode || 'project';
        const isProjectMode = options.isProjectMode != null
            ? Boolean(options.isProjectMode)
            : mode === 'project';
        const roleOptions = options.roleOptions || FIELD_ROLES;
        const fixedRole = options.fixedRole || null;
        const editableStructure = options.editableStructure !== false;
        const editableValues = options.editableValues !== false;
        const autoRecalculateBytePositions = options.autoRecalculateBytePositions !== false;
        const showMoveActions = options.showMoveActions !== false;
        const allowStringLengthEdit = options.allowStringLengthEdit != null
            ? Boolean(options.allowStringLengthEdit)
            : isProjectMode;
        const defaultStringByteLen = options.defaultStringByteLen;
        const hideRoleColumn = Boolean(options.hideRoleColumn);
        const emptyPreviewText = options.emptyPreviewText || '暂无字段';
        const countLabel = options.countLabel || '字段';
        const fieldNamePlaceholder = options.fieldNamePlaceholder || '字段名称';
        let destroyed = false;

        function normalizeEditorFields(fields) {
            return (Array.isArray(fields) ? fields : []).map(field => {
                const nextField = cloneField(field);
                if (fixedRole) nextField.role = fixedRole;
                if (!nextField.role) nextField.role = 'common';
                return nextField;
            });
        }

        function getFieldValue(fieldNode) {
            try {
                return readFieldValueInput(fieldNode);
            } catch (error) {
                return fieldNode.querySelector('.pattern-field-value')?.value || '';
            }
        }

        function getFields() {
            const fields = [];
            fieldList.querySelectorAll('.pattern-field-container').forEach((fieldNode, index) => {
                if (fieldNode.dataset.deleting === 'true' || fieldNode.classList.contains('is-deleting')) return;
                const field = fieldFromNode(fieldNode);
                field.role = fixedRole || field.role || 'common';
                field.byte_pos = toFiniteNumber(field.byte_pos, index);
                field.byte_len = toFiniteNumber(field.byte_len, null);
                field.value = getFieldValue(fieldNode);
                field.match = '';
                fields.push(field);
            });
            return fields;
        }

        function renderEmbeddedPreview(fields) {
            if (!preview) return;
            const normalized = normalizePatternInfo({ fields }, { onlyCommon: fixedRole === 'common' }).fields;

            if (!normalized.length) {
                preview.innerHTML = `<div class="pattern-layout-empty">${escapeHTML(emptyPreviewText)}</div>`;
                return;
            }

            preview.innerHTML = normalized.map((field, index) => {
                const valid = String(field.name || '').trim()
                    && Number.isFinite(field.byte_pos)
                    && Number.isFinite(field.byte_len)
                    && field.byte_len > 0
                    && String(field.type || '').trim();
                const byteLen = patternByteLayoutSpan(field.byte_len);
                const roleClass = roleClassName(field.role || 'common');
                const offsetText = Number.isFinite(field.byte_pos) ? field.byte_pos : '?';
                const name = field.name || `字段${index + 1}`;
                return `
                    <div class="pattern-byte-block ${roleClass} ${valid ? '' : 'is-error'}" data-role="${escapeHTML(field.role || 'common')}" style="--pattern-span:${byteLen}">
                        <span class="pattern-byte-offset">offset ${escapeHTML(offsetText)}</span>
                        <strong>${escapeHTML(name)}</strong>
                        <span>${escapeHTML(previewFieldValueText(field, isProjectMode))}</span>
                    </div>
                `;
            }).join('');
        }

        function refresh() {
            if (destroyed) {
                return {
                    fields: [],
                    validation: { valid: true, errors: [] },
                };
            }
            updatePatternFieldDeleteButtonStates(fieldList);
            const fields = getFields();
            if (countElement) countElement.textContent = `${fields.length} 个${countLabel}`;
            renderEmbeddedPreview(fields);
            if (typeof options.onChange === 'function') {
                options.onChange(fields);
            }
            return {
                fields,
                validation: { valid: true, errors: [] },
            };
        }

        function createFieldNode(field) {
            const normalizedField = normalizeEditorFields([field || createDefaultPatternFieldInfo()])[0];
            const fieldNode = createPatternField(fieldNamePlaceholder, '', normalizedField, { roleOptions });
            updatePatternField(fieldNode, normalizedField, isProjectMode);
            fieldNode.classList.toggle('pattern-field-role-hidden', hideRoleColumn);
            if (fixedRole) {
                fieldNode.querySelector('.pattern-field-role').value = fixedRole;
            }
            return fieldNode;
        }

        function removeFieldNode(fieldNode) {
            animatePatternFieldRemovalCore(fieldNode, fieldList, {
                popoverRoot: editorRoot,
                onFinish: function() {
                    if (autoRecalculateBytePositions) recalculatePatternFieldBytePositions(fieldList);
                    refresh();
                },
            });
        }

        function appendEditorField(field, afterNode = null) {
            const fieldNode = createFieldNode(field);
            appendFieldNode(fieldList, fieldNode, editorRoot, isProjectMode, afterNode, {
                canEditStructure: editableStructure,
                canEditValues: editableValues,
                autoRecalculateBytePositions,
                showMoveActions,
                lockRole: Boolean(fixedRole),
                role: fixedRole || undefined,
                roleOptions,
                valueAvailabilityProjectMode: isProjectMode,
                allowStringLengthEdit,
                defaultStringByteLen,
                defaultStringDisplay: options.defaultStringDisplay,
                onRefresh: refresh,
                onAddAfter: function(anchorNode) {
                    appendEditorField(createDefaultPatternFieldInfo(), anchorNode);
                    if (autoRecalculateBytePositions) recalculatePatternFieldBytePositions(fieldList);
                    refresh();
                    markPatternFieldChanged(anchorNode.nextElementSibling, 'added');
                },
                onDelete: removeFieldNode,
            });
            if (autoRecalculateBytePositions) recalculatePatternFieldBytePositions(fieldList);
            refresh();
            return fieldNode;
        }

        function setFields(fields) {
            fieldList.innerHTML = '';
            const normalizedFields = normalizeEditorFields(fields);
            normalizedFields.forEach(field => appendEditorField(field));
            refresh();
        }

        function resetFields(defaultField) {
            fieldList.innerHTML = '';
            return appendEditorField(defaultField || createDefaultPatternFieldInfo());
        }

        if (addButton) {
            addButton.addEventListener('click', function() {
                const fieldNode = appendEditorField(createDefaultPatternFieldInfo());
                markPatternFieldChanged(fieldNode, 'added');
                const nameInput = fieldNode.querySelector('.pattern-field-name');
                if (nameInput && typeof nameInput.focus === 'function') {
                    nameInput.focus();
                }
            });
        }

        setFields(options.fields || []);

        return {
            setFields,
            getFields,
            addField: function(field) {
                return appendEditorField(field || createDefaultPatternFieldInfo());
            },
            resetFields,
            refresh,
            destroy: function() {
                destroyed = true;
                fieldList.innerHTML = '';
                if (preview) preview.innerHTML = '';
            },
        };
    }

    /**
     * 创建 TCP 字节布局预览和字段列表区 HTML。TCP 弹窗和 Body Binary 共用这段结构，避免两边维护两套控件。
     * @param {Object} options 渲染配置。
     * @returns {string} 字段编辑区 HTML。
     */
    function createPatternFieldEditorSectionHTML(options = {}) {
        const isProjectMode = Boolean(options.isProjectMode);
        const fieldInfoId = options.fieldInfoId ? ` id="${escapeHTML(options.fieldInfoId)}"` : '';
        const layoutSectionClass = options.layoutSectionClass ? ` ${escapeHTML(options.layoutSectionClass)}` : '';
        const previewClass = options.previewClass ? ` ${escapeHTML(options.previewClass)}` : '';
        const fieldInfoClass = options.fieldInfoClass ? ` ${escapeHTML(options.fieldInfoClass)}` : '';
        const labelsClass = options.labelsClass ? ` ${escapeHTML(options.labelsClass)}` : '';
        const listClass = options.listClass ? ` ${escapeHTML(options.listClass)}` : '';
        const addButtonClass = options.addButtonClass ? ` ${escapeHTML(options.addButtonClass)}` : '';
        const countClass = options.countClass ? ` ${escapeHTML(options.countClass)}` : '';
        const titleText = options.layoutTitle || '字节布局预览';
        const fieldTitle = options.fieldTitle || '字段配置';
        const countText = options.countText || '';
        const showAddButton = Boolean(options.showAddButton);
        const hideRoleColumn = Boolean(options.hideRoleColumn);
        const addButtonText = options.addButtonText || '新增字段';
        const fieldValueLabel = options.fieldValueLabel || '字段值';

        return `
            <div class="pattern-layout-section${layoutSectionClass}">
                <div class="pattern-section-title">
                    <span>${escapeHTML(titleText)}</span>
                    ${countText ? `<span class="${countClass || 'pattern-section-count'}">${escapeHTML(countText)}</span>` : ''}
                </div>
                <div class="pattern-byte-layout${previewClass}"></div>
            </div>
            <div class="pattern-field-info${fieldInfoClass}"${fieldInfoId}>
                <div class="pattern-field-info-header">
                    <label>${escapeHTML(fieldTitle)}</label>
                    ${showAddButton ? `<button type="button" class="add-field-btn${addButtonClass}">${escapeHTML(addButtonText)}</button>` : ''}
                </div>
                <div class="pattern-field-grid-labels${labelsClass}" aria-hidden="true">
                    <span>Byte起始</span>
                    <span>名称</span>
                    <span>Byte长度</span>
                    <span>类型</span>
                    ${hideRoleColumn ? '' : '<span>角色</span>'}
                    ${isProjectMode ? '' : `<span>${escapeHTML(fieldValueLabel)}</span>`}
                    <span>操作</span>
                </div>
                <div class="pattern-list${listClass}"></div>
            </div>
        `;
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
                <div class="form-group pattern-byte-order-group" hidden>
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
                        ${createPatternFieldEditorSectionHTML({
                            isProjectMode,
                            fieldInfoId: 'special-pattern-fields',
                            fieldTitle: fieldTitle || '字段配置',
                        })}
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
            appendFieldNode(patternList, fieldNode, configModal, isProjectMode, null, {
                defaultStringDisplay: !isProjectMode,
            });
        });
        syncPatternListEmptyState(patternList, configModal, isProjectMode);

        refreshPatternModal(configModal, isProjectMode);

        function resetPatternModalFields() {
            patternList.innerHTML = '';
            const blankField = createBlankPatternFieldInfo();
            const fieldNode = createPatternField('字段名称', '', blankField);
            updatePatternField(fieldNode, blankField, isProjectMode);
            appendFieldNode(patternList, fieldNode, configModal, isProjectMode, null, {
                defaultStringDisplay: !isProjectMode,
            });
            markPatternFieldChanged(fieldNode, 'added');
            return fieldNode;
        }

        configModal.querySelector('.pattern-length-policy')?.addEventListener('change', function() {
            configModal.dataset.lengthPolicy = this.value;
            refreshPatternModal(configModal, isProjectMode);
        });
        configModal.querySelector('.pattern-byte-order')?.addEventListener('change', function() {
            configModal.dataset.byteOrder = this.value;
            refreshPatternModal(configModal, isProjectMode);
        });

        configModal.querySelector('.clear-btn')?.addEventListener('click', function(event) {
            event.stopPropagation();
            if (!confirm('确定要清除当前全部字段吗？')) return;

            resetPatternModalFields();
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
            const submitButton = configModal.querySelector('.confirm-btn');
            if (submitButton && submitButton.disabled) return;

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
            if (submitButton) submitButton.disabled = true;

            try {
            if (resolvedTargetElement && resolvedTargetElement.dataset) {
                resolvedTargetElement.dataset.patternInfos = JSON.stringify(serialized);
            }
            if (resolvedStatusElement) {
                resolvedStatusElement.style.display = 'inline';
                resolvedStatusElement.textContent = isProjectMode ? '格式已设置' : `已设置 ${model.fields.filter(field => field.value).length} 个字段`;
            }

            if (userHandleCb) {
                const result = await userHandleCb(serialized);
                if (result === false) return;
            }

            closeModal();
            } finally {
                if (submitButton) submitButton.disabled = false;
            }
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
        } else if (String(functionField?.type || '').toUpperCase() === 'STR') {
            errors.push(...validateStringWireValue(
                normalizedCfg.function_code,
                '功能码',
                functionField?.byte_len,
            ));
        } else if (functionField && wireHexByteLength(normalizedCfg.function_code) !== Number(functionField.byte_len)) {
            errors.push(`功能码字节数必须等于 ${functionField.byte_len}`);
        }

        normalizedPattern.fields
            .filter(field => field.role === 'common' && String(field.type || '').toUpperCase() === 'STR')
            .forEach(field => {
                const value = normalizedCfg.fields[String(field.byte_pos)] || '';
                errors.push(...validateStringWireValue(value, field.name || field.byte_pos, field.byte_len));
            });

        Object.keys(normalizedCfg.fields).forEach(bytePos => {
            const field = normalizedPattern.fields.find(item => String(item.byte_pos) === String(bytePos));
            const value = normalizedCfg.fields[bytePos];
            if (!field) {
                errors.push(`普通字段 ${bytePos} 不存在于项目 TCP 格式`);
                return;
            }
            if (String(field.type || '').toUpperCase() === 'STR') {
                errors.push(...validateStringWireValue(value, field.name || bytePos, field.byte_len));
            } else if (!isWireHex(value)) {
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
        createPatternFieldEditorSectionHTML,
        createPatternFieldListEditor,
        isWireHex,
        wireHexByteLength,
        displayFromWireHex,
        wireHexFromDisplay,
        getTypeByteLen,
    };

    global.updatePatternField = updatePatternField;
    global.createPatternField = createPatternField;
    global.createCustomTcpPatternModal = createCustomTcpPatternModal;
})(typeof window !== 'undefined' ? window : globalThis);
