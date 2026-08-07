(function initKitProxyBodyEditor(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const utils = KitProxy.utils || {};

    const BODY_TYPE_OPTIONS = Object.freeze([
        { value: 'none', label: 'None', enabled: true },
        { value: 'empty', label: 'Empty', enabled: true },
        { value: 'json', label: 'JSON', enabled: true },
        { value: 'xml', label: 'XML', enabled: true },
        { value: 'text', label: 'Text', enabled: true },
        { value: 'image', label: 'Image', enabled: true },
        { value: 'binary', label: 'Binary', enabled: true },
    ]);
    const NO_CONTENT_BODY_TYPES = Object.freeze(['none', 'empty']);
    const TEXT_INPUT_COLLAPSED_BODY_TYPES = Object.freeze(['none', 'empty', 'image', 'binary', 'multiform']);
    const REQUEST_TEXTLESS_BODY_TYPES = TEXT_INPUT_COLLAPSED_BODY_TYPES;
    const CONTENT_NORMALIZATION_EMPTY_BODY_TYPES = Object.freeze(['none', 'empty', 'image', 'binary']);
    const MULTIFORM_BODY_TYPE = 'multiform';
    const HIDDEN_BODY_TYPES = Object.freeze([MULTIFORM_BODY_TYPE]);
    const HIGHLIGHT_SIZE_LIMIT = 100 * 1024;
    const EDITOR_LINE_HEIGHT = 20;
    const EDITOR_VERTICAL_PADDING = 24;
    const EDITOR_TAB_SIZE = 2;

    function escapeHtml(value) {
        return String(value == null ? '' : value)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    function positionToLineColumn(text, position) {
        const safeText = String(text || '');
        const safePosition = Math.max(0, Math.min(Number(position) || 0, safeText.length));
        const before = safeText.slice(0, safePosition);
        const lines = before.split('\n');

        return {
            line: lines.length,
            column: lines[lines.length - 1].length + 1,
        };
    }

    function normalizeErrorMessage(message) {
        return String(message || '语法错误').replace(/\s+/g, ' ').trim();
    }

    function parseJsonError(text, error) {
        const message = normalizeErrorMessage(error && error.message);
        const positionMatch = message.match(/position\s+(\d+)/i);

        if (positionMatch) {
            const location = positionToLineColumn(text, Number(positionMatch[1]));
            return {
                valid: false,
                line: location.line,
                column: location.column,
                message: `第 ${location.line} 行，第 ${location.column} 列：JSON 格式错误，${message}`,
            };
        }

        return {
            valid: false,
            line: 1,
            column: 1,
            message: `第 1 行，第 1 列：JSON 格式错误，${message}`,
        };
    }

    function parseXmlLocation(parserErrorText) {
        const text = String(parserErrorText || '');
        const lineColumnMatch = text.match(/(\d+):(\d+)/);
        if (lineColumnMatch) {
            return {
                line: Number(lineColumnMatch[1]),
                column: Number(lineColumnMatch[2]),
            };
        }

        const lineMatch = text.match(/line(?:\s+number)?\s*[:=]?\s*(\d+)/i);
        const columnMatch = text.match(/col(?:umn)?(?:\s+number)?\s*[:=]?\s*(\d+)/i);

        if (lineMatch || columnMatch) {
            return {
                line: lineMatch ? Number(lineMatch[1]) : 1,
                column: columnMatch ? Number(columnMatch[1]) : 1,
            };
        }

        return {
            line: 1,
            column: 1,
        };
    }

    function validateJson(text) {
        if (!text) {
            return {
                valid: true,
                line: null,
                column: null,
                message: '空 Body 将按未设置处理',
            };
        }

        try {
            JSON.parse(text);
            return {
                valid: true,
                line: null,
                column: null,
                message: 'JSON 格式正确',
            };
        } catch (error) {
            return parseJsonError(text, error);
        }
    }

    function validateXml(text) {
        if (!text) {
            return {
                valid: true,
                line: null,
                column: null,
                message: '空 Body 将按未设置处理',
            };
        }

        const parser = new global.DOMParser();
        const xmlDoc = parser.parseFromString(text, 'application/xml');
        const parserError = xmlDoc.querySelector('parsererror');

        if (!parserError) {
            return {
                valid: true,
                line: null,
                column: null,
                message: 'XML 格式正确',
            };
        }

        const parserErrorText = normalizeErrorMessage(parserError.textContent);
        const location = parseXmlLocation(parserErrorText);

        return {
            valid: false,
            line: location.line,
            column: location.column,
            message: `第 ${location.line} 行，第 ${location.column} 列：XML 格式错误，${parserErrorText}`,
        };
    }

    function validate(text, bodyType) {
        const normalizedType = String(bodyType || 'text').toLowerCase();

        if (NO_CONTENT_BODY_TYPES.includes(normalizedType)) {
            return {
                valid: true,
                line: null,
                column: null,
                message: '该 Body 类型不需要文本内容',
            };
        }

        if (normalizedType === 'binary') {
            return {
                valid: true,
                line: null,
                column: null,
                message: 'Binary Body 使用二进制字段配置',
            };
        }

        if (normalizedType === MULTIFORM_BODY_TYPE) {
            return {
                valid: true,
                line: null,
                column: null,
                message: 'Multiform 使用字段表格配置',
            };
        }

        if (normalizedType === 'json') {
            return validateJson(text);
        }

        if (normalizedType === 'xml') {
            return validateXml(text);
        }

        return {
            valid: true,
            line: null,
            column: null,
            message: text ? 'Text Body 不做语法校验' : '空 Body 将按未设置处理',
        };
    }

    /**
     * 判断 Body 类型是否不需要普通文本输入。
     * @param {string} bodyType Body 类型。
     * @returns {boolean}
     */
    function isTextlessRequestBodyType(bodyType) {
        return REQUEST_TEXTLESS_BODY_TYPES.includes(String(bodyType || '').toLowerCase());
    }

    /**
     * 请求侧 Body 校验：None/Empty/Binary 不要求文本 Body。
     * @param {string} text Body 文本内容。
     * @param {string} bodyType Body 类型。
     * @returns {{ valid: boolean; line: number | null; column: number | null; message: string; }}
     */
    function validateRequest(text, bodyType) {
        if (isTextlessRequestBodyType(bodyType)) {
            return {
                valid: true,
                line: null,
                column: null,
                message: '该 Body 类型不需要文本内容',
            };
        }

        return validate(text, bodyType);
    }

    /**
     * 特殊 Body 类型提交时统一清空文本内容。
     * @param {string} text Body 文本内容。
     * @param {string} bodyType Body 类型。
     * @returns {string}
     */
    function normalizeBodyContent(text, bodyType) {
        if (String(bodyType || '').toLowerCase() === 'binary') {
            const fields = tryParseBinaryFields(text);
            return fields ? serializeBinaryFields(fields) : '';
        }
        return CONTENT_NORMALIZATION_EMPTY_BODY_TYPES.includes(String(bodyType || '').toLowerCase())
            ? ''
            : String(text == null ? '' : text);
    }

    /**
     * 请求侧历史 API 名称保留，内部复用通用 Body 内容归一化。
     * @param {string} text Body 文本内容。
     * @param {string} bodyType Body 类型。
     * @returns {string}
     */
    function normalizeRequestBodyContent(text, bodyType) {
        return normalizeBodyContent(text, bodyType);
    }

    function format(text, bodyType) {
        const normalizedType = String(bodyType || 'text').toLowerCase();

        if (normalizedType === 'json') {
            if (!String(text || '').trim()) return '';
            return JSON.stringify(JSON.parse(text), null, 2);
        }

        if (normalizedType === 'xml') {
            if (!String(text || '').trim()) return '';
            if (typeof utils.formatXMLHelper === 'function') {
                return utils.formatXMLHelper(text);
            }
            return text;
        }

        if (normalizedType === 'binary') {
            throw new Error('Binary Body 暂不支持格式化');
        }

        return text || '';
    }

    function highlightJson(text) {
        const source = String(text == null ? '' : text);
        const tokenRegex = /("(?:\\.|[^"\\])*"\s*:)|("(?:\\.|[^"\\])*")|(-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)|\b(true|false)\b|\bnull\b|([{}\[\],:])/g;
        let output = '';
        let lastIndex = 0;
        let match;
        tokenRegex.lastIndex = 0;

        while ((match = tokenRegex.exec(source)) !== null) {
            output += escapeHtml(source.slice(lastIndex, match.index));
            const token = match[0];
            if (match[1]) {
                const colonIndex = token.lastIndexOf(':');
                output += `<span class="body-token-key">${escapeHtml(token.slice(0, colonIndex))}</span><span class="body-token-punctuation">:</span>`;
            } else if (match[2]) {
                output += `<span class="body-token-string">${escapeHtml(token)}</span>`;
            } else if (match[3]) {
                output += `<span class="body-token-number">${escapeHtml(token)}</span>`;
            } else if (match[4]) {
                output += `<span class="body-token-boolean">${escapeHtml(token)}</span>`;
            } else if (/\bnull\b/.test(token)) {
                output += `<span class="body-token-null">${escapeHtml(token)}</span>`;
            } else {
                output += `<span class="body-token-punctuation">${escapeHtml(token)}</span>`;
            }
            lastIndex = tokenRegex.lastIndex;
        }

        output += escapeHtml(source.slice(lastIndex));
        return output || '<br>';
    }

    function highlightXmlTag(rawTag) {
        const tag = String(rawTag || '');
        if (/^<!--/.test(tag)) {
            return `<span class="body-token-comment">${escapeHtml(tag)}</span>`;
        }
        if (/^<!\[CDATA\[/.test(tag)) {
            return `<span class="body-token-cdata">${escapeHtml(tag)}</span>`;
        }
        if (/^<\?/.test(tag) || /^<!/.test(tag)) {
            return `<span class="body-token-bracket">${escapeHtml(tag)}</span>`;
        }

        const match = tag.match(/^<(\/?)([^\s/>]+)([\s\S]*?)(\/?)>$/);
        if (!match) {
            return `<span class="body-token-bracket">${escapeHtml(tag)}</span>`;
        }

        const [, slash, tagName, attrText, selfClose] = match;
        let output = `<span class="body-token-bracket">&lt;${escapeHtml(slash)}</span><span class="body-token-tag">${escapeHtml(tagName)}</span>`;
        const attrRegex = /(\s+)([^\s=/>]+)(?:\s*=\s*(".*?"|'.*?'|[^\s/>]+))?/g;
        let lastIndex = 0;
        let attrMatch;

        while ((attrMatch = attrRegex.exec(attrText)) !== null) {
            output += escapeHtml(attrText.slice(lastIndex, attrMatch.index));
            output += escapeHtml(attrMatch[1]);
            output += `<span class="body-token-attr">${escapeHtml(attrMatch[2])}</span>`;
            if (attrMatch[3] != null) {
                output += `<span class="body-token-punctuation">=</span><span class="body-token-attr-value">${escapeHtml(attrMatch[3])}</span>`;
            }
            lastIndex = attrRegex.lastIndex;
        }

        output += escapeHtml(attrText.slice(lastIndex));
        output += `<span class="body-token-bracket">${escapeHtml(selfClose || '')}&gt;</span>`;
        return output;
    }

    function highlightXml(text) {
        const source = String(text == null ? '' : text);
        const tagRegex = /(<!--[\s\S]*?-->|<!\[CDATA\[[\s\S]*?\]\]>|<[^>]+>)/g;
        let output = '';
        let lastIndex = 0;
        let match;
        tagRegex.lastIndex = 0;

        while ((match = tagRegex.exec(source)) !== null) {
            output += escapeHtml(source.slice(lastIndex, match.index));
            output += highlightXmlTag(match[0]);
            lastIndex = tagRegex.lastIndex;
        }

        output += escapeHtml(source.slice(lastIndex));
        return output || '<br>';
    }

    function highlight(text, bodyType) {
        const normalizedType = String(bodyType || 'text').toLowerCase();

        if (normalizedType === 'json') {
            return highlightJson(text);
        }

        if (normalizedType === 'xml') {
            return highlightXml(text);
        }

        return escapeHtml(text || '') || '<br>';
    }

    function decodeBodyData(bodyData) {
        if (bodyData == null) return '';
        if (typeof bodyData === 'string') return bodyData;
        if (bodyData instanceof ArrayBuffer) {
            return new TextDecoder().decode(new Uint8Array(bodyData));
        }
        if (ArrayBuffer.isView(bodyData)) {
            return new TextDecoder().decode(bodyData);
        }
        return String(bodyData);
    }

    function debounce(fn, wait) {
        let timer = null;

        return function debounced() {
            const args = arguments;
            clearTimeout(timer);
            timer = setTimeout(function() {
                fn.apply(null, args);
            }, wait);
        };
    }

    function resolveAllowedTypes(options) {
        const allowedTypes = Array.isArray(options.allowedTypes) && options.allowedTypes.length
            ? options.allowedTypes
            : BODY_TYPE_OPTIONS;
        const typeMap = {};

        BODY_TYPE_OPTIONS.forEach(option => {
            typeMap[option.value] = option;
        });

        const resolved = allowedTypes.map(option => {
            if (typeof option === 'string') {
                return typeMap[option] || { value: option, label: option.toUpperCase(), enabled: true };
            }

            return Object.assign({}, typeMap[option.value] || {}, option);
        }).filter(option => !HIDDEN_BODY_TYPES.includes(String(option.value || '').toLowerCase()));

        return resolved.length ? resolved : BODY_TYPE_OPTIONS.map(option => Object.assign({}, option));
    }

    function normalizeMultiformField(field, index) {
        const source = field && typeof field === 'object' ? field : {};
        const type = String(source.type || 'text').toLowerCase() === 'file' ? 'file' : 'text';
        return {
            id: String(source.id || `multiform-field-${Date.now()}-${index}`),
            enabled: source.enabled !== false,
            name: String(source.name || ''),
            value: type === 'text' ? String(source.value == null ? '' : source.value) : '',
            type,
            description: String(source.description || ''),
            filename: String(source.filename || source.file_name || ''),
            content_type: String(source.content_type || source.contentType || ''),
            data_base64: String(source.data_base64 || source.dataBase64 || ''),
        };
    }

    function normalizeMultiformFields(fields) {
        return (Array.isArray(fields) ? fields : []).map(normalizeMultiformField);
    }

    function tryParseMultiformFields(value) {
        if (!value || typeof value !== 'string') return [];
        try {
            const parsed = JSON.parse(value);
            return normalizeMultiformFields(parsed && Array.isArray(parsed.fields) ? parsed.fields : parsed);
        } catch (error) {
            return [];
        }
    }

    function createEmptyMultiformField() {
        return normalizeMultiformField({}, Math.floor(Math.random() * 100000));
    }

    function serializeMultiformFields(fields) {
        const normalized = normalizeMultiformFields(fields);
        return normalized.some(field => field.enabled && field.name.trim())
            ? JSON.stringify({ version: 1, fields: normalized })
            : '';
    }

    /**
     * 将 Binary Body 的字段行转换为 TCP 字段格式。
     * Body 不区分特殊角色，字段值既作为匹配值也作为实际写入值保存。
     * @param {Array<any>} fields Binary Body 字段。
     * @returns {string} 可持久化的 JSON 字符串；没有完整字段时返回空内容。
     */
    function serializeBinaryFields(fields) {
        const serializedFields = normalizeBinaryFields(fields)
            .filter(field => {
                return String(field.name || '').trim()
                    && Number.isInteger(Number(field.byte_pos))
                    && Number(field.byte_pos) >= 0
                    && Number.isInteger(Number(field.byte_len))
                    && Number(field.byte_len) > 0
                    && String(field.type || '').trim()
                    && String(field.value || '').trim();
            })
            .map(field => {
                const wireValue = String(field.value || '').trim();
                return {
                    spec: {
                        byte_len: Number(field.byte_len),
                        byte_pos: Number(field.byte_pos),
                        match: wireValue,
                        name: String(field.name || '').trim(),
                        role: 'common',
                        type: String(field.type || '').trim(),
                    },
                    value: wireValue,
                };
            });

        return serializedFields.length ? JSON.stringify({ fields: serializedFields }) : '';
    }

    function lineNumbersHTML(lineCount) {
        const count = Math.max(1, Number(lineCount) || 1);
        const lines = [];

        for (let idx = 1; idx <= count; idx += 1) {
            lines.push(`<span class="body-editor-line-number">${idx}</span>`);
        }

        return lines.join('');
    }

    /**
     * 计算编辑器可视行数和最长行宽，用同一份指标驱动行号层、代码层和输入层。
     *
     * @param {string} text Body 文本内容。
     * @returns {{lineCount: number, maxColumns: number}}
     */
    function getEditorMetrics(text) {
        const lines = String(text == null ? '' : text).split('\n');
        let maxColumns = 1;

        lines.forEach(line => {
            let columns = 0;
            Array.from(String(line)).forEach(char => {
                if (char === '\t') {
                    const remainder = columns % EDITOR_TAB_SIZE;
                    columns += remainder === 0 ? EDITOR_TAB_SIZE : EDITOR_TAB_SIZE - remainder;
                    return;
                }
                columns += /[^\u0000-\u00ff]/.test(char) ? 2 : 1;
            });
            maxColumns = Math.max(maxColumns, columns + 1);
        });

        return {
            lineCount: Math.max(1, lines.length),
            maxColumns,
        };
    }

    /**
     * Body Binary 模式只复用 TCP 字段配置的普通字段形态，不开放特殊字段角色。
     * @param {any} field 字段原始值。
     * @param {number} index 字段下标。
     * @returns {{ id: string; name: string; byte_pos: number | string; byte_len: number | string; type: string; role: string; value: string; }}
     */
    function normalizeBinaryField(field, index) {
        const source = field && typeof field === 'object' ? field : {};
        const spec = source.spec && typeof source.spec === 'object' ? source.spec : source;
        const type = String(spec.type || '').trim();
        const rawByteLen = spec.byte_len ?? spec.byteLen ?? '';

        return {
            id: String(source.id || `binary-field-${Date.now()}-${index}`),
            name: String(spec.name || ''),
            byte_pos: spec.byte_pos ?? spec.bytePos ?? '',
            byte_len: rawByteLen,
            type,
            role: 'common',
            value: String(source.value ?? spec.match ?? source.match ?? ''),
        };
    }

    /**
     * @param {Array<any>} fields 二进制字段配置。
     * @returns {Array<{ id: string; name: string; byte_pos: number | string; byte_len: number | string; type: string; role: string; value: string; }>}
     */
    function normalizeBinaryFields(fields) {
        return (Array.isArray(fields) ? fields : []).map(normalizeBinaryField);
    }

    /**
     * @returns {{ id: string; name: string; byte_pos: string; byte_len: string; type: string; role: string; value: string; }}
     */
    function createEmptyBinaryField() {
        return normalizeBinaryField({}, Math.floor(Math.random() * 100000));
    }

    /**
     * @param {string} value 待解析内容。
     * @returns {Array<any> | null}
     */
    function tryParseBinaryFields(value) {
        const text = String(value || '').trim();
        if (!text) return null;

        try {
            const parsed = JSON.parse(text);
            if (Array.isArray(parsed)) return parsed;
            if (parsed && Array.isArray(parsed.fields)) return parsed.fields;
        } catch (error) {
            return null;
        }

        return null;
    }

    function create(container, options = {}) {
        if (!container) {
            throw new Error('Body 输入组件缺少挂载容器');
        }

        const idPrefix = options.idPrefix || `body-editor-${Date.now()}`;
        let allowedTypes = resolveAllowedTypes(options);
        const defaultType = allowedTypes.some(option => option.value === 'json')
            ? 'json'
            : (allowedTypes[0]?.value || 'json');
        const initialType = String(options.bodyType || defaultType).toLowerCase();
        const initialValue = decodeBodyData(options.value || '');
        let currentBodyType = initialType;
        let preservedUnsupportedValue = HIDDEN_BODY_TYPES.includes(initialType) ? initialValue : '';
        let contentHidden = Boolean(options.hideContent);
        let validateFn = typeof options.validate === 'function' ? options.validate : validate;

        container.innerHTML = `
            <div class="body-editor" data-body-editor="${escapeHtml(idPrefix)}">
                <div class="body-editor-toolbar">
                    <label class="body-editor-type-label" for="${escapeHtml(idPrefix)}-type">${escapeHtml(options.typeLabel || 'Body类型')}</label>
                    <select id="${escapeHtml(idPrefix)}-type" class="body-editor-type"></select>
                    <button type="button" class="body-editor-format">格式化</button>
                    <button type="button" class="body-editor-clear">清空</button>
                    <span class="body-editor-status" aria-live="polite"></span>
                </div>
                <p class="body-editor-unsupported-note" hidden aria-live="polite"></p>
                <div class="body-editor-input-wrap">
                    <div class="body-editor-scroll-content">
                        <div class="body-editor-lines" aria-hidden="true">1</div>
                        <div class="body-editor-code-wrap">
                            <pre class="body-editor-highlight" aria-hidden="true"><code class="body-editor-highlight-code"></code></pre>
                            <textarea id="${escapeHtml(idPrefix)}-content" class="body-editor-textarea" spellcheck="false" wrap="off" autocomplete="off" autocapitalize="off" autocorrect="off"></textarea>
                        </div>
                    </div>
                </div>
                <div class="body-editor-multiform-wrap is-collapsed" aria-hidden="true">
                    <div class="body-editor-multiform-toolbar">
                        <div>
                            <strong>multipart/form-data</strong>
                            <span>按字段配置文本或文件参数</span>
                        </div>
                        <button type="button" class="body-editor-multiform-add">新增参数</button>
                    </div>
                    <div class="body-editor-multiform-scroll">
                        <table class="body-editor-multiform-table">
                            <thead>
                                <tr><th>启用</th><th>参数名</th><th>值</th><th>类型</th><th>描述</th><th aria-label="操作"></th></tr>
                            </thead>
                            <tbody></tbody>
                        </table>
                    </div>
                    <p class="body-editor-multiform-empty" hidden>暂无参数，点击“新增参数”开始配置。</p>
                </div>
                <div class="body-editor-binary-wrap body-editor-binary-pattern-scope config-pattern-modal is-collapsed" aria-hidden="true"></div>
                <div class="body-editor-error" aria-live="polite"></div>
            </div>
        `;

        const root = container.querySelector('.body-editor');
        const typeLabel = container.querySelector('.body-editor-type-label');
        const typeSelect = container.querySelector('.body-editor-type');
        const textarea = container.querySelector('.body-editor-textarea');
        const scrollViewport = container.querySelector('.body-editor-input-wrap');
        const highlightCode = container.querySelector('.body-editor-highlight-code');
        const lineNumberBox = container.querySelector('.body-editor-lines');
        const formatButton = container.querySelector('.body-editor-format');
        const clearButton = container.querySelector('.body-editor-clear');
        const statusElement = container.querySelector('.body-editor-status');
        const errorElement = container.querySelector('.body-editor-error');
        const unsupportedNote = container.querySelector('.body-editor-unsupported-note');
        const multiformWrap = container.querySelector('.body-editor-multiform-wrap');
        const multiformTableBody = container.querySelector('.body-editor-multiform-table tbody');
        const multiformEmpty = container.querySelector('.body-editor-multiform-empty');
        const multiformAddButton = container.querySelector('.body-editor-multiform-add');
        const binaryConfigWrap = container.querySelector('.body-editor-binary-wrap');
        let binaryFields = normalizeBinaryFields(
            options.binaryFields || tryParseBinaryFields(initialValue) || [],
        );
        let binaryFieldEditor = null;
        let binarySectionMounted = false;
        let multiformFields = normalizeMultiformFields(
            options.multiformFields || tryParseMultiformFields(initialValue),
        );
        const multiformFileReads = new Set();

        /**
         * 重新渲染类型下拉，供请求/响应 Tab 切换时复用同一个编辑器实例。
         * @param {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>} nextAllowedTypes
         * @param {string} preferredType
         */
        function renderTypeOptions(nextAllowedTypes, preferredType) {
            allowedTypes = Array.isArray(nextAllowedTypes) && nextAllowedTypes.length
                ? resolveAllowedTypes({ allowedTypes: nextAllowedTypes })
                : resolveAllowedTypes({ allowedTypes: BODY_TYPE_OPTIONS });
            typeSelect.innerHTML = '';

            allowedTypes.forEach(option => {
                const optionElement = document.createElement('option');
                optionElement.value = option.value;
                optionElement.textContent = option.reserved ? `${option.label}（预留）` : option.label;
                optionElement.disabled = option.enabled === false;
                typeSelect.appendChild(optionElement);
            });

            const requestedType = String(preferredType || allowedTypes[0]?.value || 'json').toLowerCase();
            const unsupported = HIDDEN_BODY_TYPES.includes(requestedType);
            if (unsupported) {
                typeSelect.value = '';
                typeSelect.title = '当前 Body 类型暂不支持编辑';
            } else {
                const visibleType = Array.from(typeSelect.options).some(option => option.value === requestedType)
                    ? requestedType
                    : (allowedTypes[0]?.value || 'json');
                typeSelect.value = visibleType;
                currentBodyType = visibleType;
                typeSelect.title = '';
            }
            typeSelect.disabled = unsupported;
            if (unsupported) currentBodyType = requestedType;
        }

        /**
         * @returns {boolean}
         */
        function isBinaryMode() {
            return currentBodyType === 'binary';
        }

        function isMultiformMode() {
            return currentBodyType === MULTIFORM_BODY_TYPE;
        }

        /**
         * @returns {boolean}
         */
        function shouldCollapseTextInput() {
            return contentHidden || TEXT_INPUT_COLLAPSED_BODY_TYPES.includes(currentBodyType);
        }

        /**
         * @returns {boolean}
         */
        function shouldShowContentActions() {
            return !contentHidden && !TEXT_INPUT_COLLAPSED_BODY_TYPES.includes(currentBodyType)
                && !HIDDEN_BODY_TYPES.includes(currentBodyType);
        }

        function renderMultiformFields() {
            if (!multiformTableBody) return;
            multiformTableBody.innerHTML = '';
            multiformFields.forEach(field => {
                const row = document.createElement('tr');
                row.dataset.fieldId = field.id;
                row.innerHTML = `
                    <td class="body-editor-multiform-enabled"><input type="checkbox" data-field="enabled" aria-label="启用参数"></td>
                    <td><input type="text" data-field="name" placeholder="参数名"></td>
                    <td class="body-editor-multiform-value"></td>
                    <td><select data-field="type"><option value="text">Text</option><option value="file">File</option></select></td>
                    <td><input type="text" data-field="description" placeholder="可选描述"></td>
                    <td><button type="button" class="body-editor-multiform-remove" aria-label="删除参数" title="删除参数">×</button></td>
                `;
                row.querySelector('[data-field="enabled"]').checked = field.enabled;
                row.querySelector('[data-field="name"]').value = field.name;
                row.querySelector('[data-field="type"]').value = field.type;
                row.querySelector('[data-field="description"]').value = field.description;
                const valueCell = row.querySelector('.body-editor-multiform-value');
                if (field.type === 'file') {
                    valueCell.innerHTML = '<input type="file" data-field="file" aria-label="上传文件"><span class="body-editor-multiform-file-name"></span>';
                    const fileName = row.querySelector('.body-editor-multiform-file-name');
                    fileName.textContent = field.filename || '未选择文件';
                    fileName.title = field.filename || '';
                } else {
                    valueCell.innerHTML = '<input type="text" data-field="value" placeholder="字段值">';
                    valueCell.querySelector('[data-field="value"]').value = field.value;
                }
                multiformTableBody.appendChild(row);
            });
            if (multiformEmpty) multiformEmpty.hidden = multiformFields.length > 0;
        }

        function findMultiformField(row) {
            return multiformFields.find(field => field.id === row.dataset.fieldId);
        }

        function readMultiformFile(row, input) {
            const field = findMultiformField(row);
            const file = input && input.files && input.files[0];
            if (!field || !file || typeof global.FileReader !== 'function') return;
            field.filename = file.name || '';
            field.content_type = file.type || 'application/octet-stream';
            const reader = new global.FileReader();
            let readPromise;
            readPromise = new Promise(resolve => {
                reader.onload = function() {
                    const result = String(reader.result || '');
                    field.data_base64 = result.includes(',') ? result.split(',').slice(1).join(',') : result;
                    const fileName = row.querySelector('.body-editor-multiform-file-name');
                    if (fileName) fileName.textContent = field.filename || '未选择文件';
                    multiformFileReads.delete(readPromise);
                    resolve();
                };
                reader.onerror = function() {
                    multiformFileReads.delete(readPromise);
                    resolve();
                };
            });
            multiformFileReads.add(readPromise);
            reader.readAsDataURL(file);
        }

        function bindMultiformEvents() {
            if (!multiformTableBody) return;
            multiformTableBody.addEventListener('input', function(event) {
                const row = event.target.closest('tr');
                const field = row && findMultiformField(row);
                const name = event.target.dataset.field;
                if (!field || !name || name === 'file') return;
                field[name] = name === 'enabled' ? event.target.checked : event.target.value;
            });
            multiformTableBody.addEventListener('change', function(event) {
                const row = event.target.closest('tr');
                const field = row && findMultiformField(row);
                const name = event.target.dataset.field;
                if (!field) return;
                if (name === 'file') {
                    readMultiformFile(row, event.target);
                    return;
                }
                if (name === 'enabled') field.enabled = event.target.checked;
                if (name === 'type') {
                    field.type = event.target.value === 'file' ? 'file' : 'text';
                    if (field.type === 'text') {
                        field.filename = '';
                        field.content_type = '';
                        field.data_base64 = '';
                    } else {
                        field.value = '';
                    }
                    renderMultiformFields();
                }
            });
            multiformTableBody.addEventListener('click', function(event) {
                const button = event.target.closest('.body-editor-multiform-remove');
                if (!button) return;
                const row = button.closest('tr');
                const field = row && findMultiformField(row);
                multiformFields = multiformFields.filter(item => item !== field);
                renderMultiformFields();
                runValidation();
            });
            multiformAddButton?.addEventListener('click', function() {
                multiformFields.push(createEmptyMultiformField());
                renderMultiformFields();
                const rows = multiformTableBody.querySelectorAll('tr');
                rows[rows.length - 1]?.querySelector('[data-field="name"]')?.focus();
            });
        }

        function validateMultiform() {
            const invalid = multiformFields.find(field => field.enabled && !field.name.trim());
            return invalid
                ? { valid: false, line: null, column: null, message: '已启用的 Multiform 参数必须填写参数名' }
                : { valid: true, line: null, column: null, message: multiformFields.length ? 'Multiform 字段配置正确' : '空 Multiform 将按未设置处理' };
        }

        /**
         * Binary 配置区复用 TCP 格式编辑弹窗中的布局预览和字段配置 DOM。
         * @param {any} tcpEditor TCP 字段编辑器命名空间。
         * @returns {boolean}
         */
        function mountBinarySection(tcpEditor) {
            if (binarySectionMounted) return true;
            if (!tcpEditor || typeof tcpEditor.createPatternFieldEditorSectionHTML !== 'function') {
                binaryConfigWrap.innerHTML = '<div class="pattern-layout-empty">TCP 字段编辑器未加载</div>';
                return false;
            }

            binaryConfigWrap.innerHTML = tcpEditor.createPatternFieldEditorSectionHTML({
                isProjectMode: false,
                layoutSectionClass: 'body-editor-binary-layout-section',
                previewClass: 'body-editor-binary-preview',
                fieldInfoClass: 'body-editor-binary-field-info',
                labelsClass: 'body-editor-binary-labels',
                listClass: 'body-editor-binary-field-list',
                countClass: 'body-editor-binary-count',
                layoutTitle: '字节布局预览',
                fieldTitle: '二进制普通字段',
                countText: '0 个普通字段',
                showAddButton: false,
                hideRoleColumn: true,
            });
            binarySectionMounted = true;
            return true;
        }

        /**
         * Binary 模式复用 TCP 字段列表编辑器，保证字段行、值输入、Byte 长度计算来自同一套代码。
         * @returns {any}
         */
        function ensureBinaryFieldEditor() {
            if (binaryFieldEditor) return binaryFieldEditor;

            const tcpEditor = KitProxy.tcpPatternEditor;
            if (!tcpEditor || typeof tcpEditor.createPatternFieldListEditor !== 'function' || !mountBinarySection(tcpEditor)) {
                return null;
            }

            binaryFieldEditor = tcpEditor.createPatternFieldListEditor(binaryConfigWrap, {
                mode: 'body-binary',
                isProjectMode: false,
                fields: binaryFields,
                fixedRole: 'common',
                roleOptions: [{ value: 'common', label: '普通字段' }],
                editableStructure: true,
                editableValues: true,
                autoRecalculateBytePositions: true,
                allowStringLengthEdit: true,
                defaultStringByteLen: 1,
                defaultStringDisplay: true,
                hideRoleColumn: true,
                showMoveActions: true,
                fieldNamePlaceholder: '普通字段',
                listSelector: '.body-editor-binary-field-list',
                previewSelector: '.body-editor-binary-preview',
                countSelector: '.body-editor-binary-count',
                emptyPreviewText: '暂无二进制普通字段',
                countLabel: '普通字段',
                onChange: function(fields) {
                    binaryFields = normalizeBinaryFields(fields);
                },
            });
            return binaryFieldEditor;
        }

        /**
         * 控制内容区及内容操作按钮显示状态。内容区保留 DOM，通过 class 做折叠动画。
         * @param {boolean} hidden 是否强制隐藏内容输入区。
         */
        function applyContentVisibility(hidden) {
            contentHidden = Boolean(hidden);
            const textCollapsed = shouldCollapseTextInput();
            const binaryVisible = !contentHidden && isBinaryMode();
            const multiformVisible = false;
            const unsupported = HIDDEN_BODY_TYPES.includes(currentBodyType);

            root.classList.toggle('is-content-hidden', contentHidden);
            root.classList.toggle('is-text-collapsed', textCollapsed);
            root.classList.toggle('is-binary-mode', binaryVisible);
            root.classList.toggle('is-multiform-mode', multiformVisible);
            root.classList.toggle('is-unsupported-body-type', unsupported);
            root.classList.toggle('is-no-content-mode', NO_CONTENT_BODY_TYPES.includes(currentBodyType));
            binaryConfigWrap.classList.toggle('is-collapsed', !binaryVisible);
            binaryConfigWrap.setAttribute('aria-hidden', binaryVisible ? 'false' : 'true');
            multiformWrap.classList.toggle('is-collapsed', !multiformVisible);
            multiformWrap.setAttribute('aria-hidden', 'true');
            if (unsupportedNote) {
                unsupportedNote.hidden = !unsupported;
                unsupportedNote.textContent = unsupported
                    ? `${currentBodyType.toUpperCase()} Body 编辑功能暂未开放，原内容将保持不变。`
                    : '';
            }
            formatButton.hidden = !shouldShowContentActions();
            clearButton.hidden = !shouldShowContentActions() || unsupported;

            if (binaryVisible && binaryFields.length === 0) {
                binaryFields = [createEmptyBinaryField()];
            }
            if (binaryVisible) {
                const editor = ensureBinaryFieldEditor();
                if (editor) editor.setFields(binaryFields);
            }
            if (multiformVisible) renderMultiformFields();
        }

        function syncBinaryFieldsFromDOM() {
            if (binaryFieldEditor) {
                binaryFields = normalizeBinaryFields(binaryFieldEditor.getFields());
            }
        }

        /**
         * 清除 Binary Body 字段内容后保留一条默认普通字段，和 TCP 格式框保持同一类占位体验。
         */
        function clearBinaryFields() {
            binaryFields = [createEmptyBinaryField()];
            if (binaryFieldEditor && typeof binaryFieldEditor.resetFields === 'function') {
                binaryFieldEditor.resetFields(binaryFields[0]);
            } else if (binaryFieldEditor) {
                binaryFieldEditor.setFields(binaryFields);
            }
        }

        function updateTextInputMode() {
            textarea.readOnly = Boolean(options.readonly || isBinaryMode() || isMultiformMode() || HIDDEN_BODY_TYPES.includes(currentBodyType));
            textarea.placeholder = isBinaryMode()
                ? 'Binary Body 使用二进制字段配置'
                : (isMultiformMode() ? 'Multiform 使用字段表格配置' : (options.placeholder || '输入 Body 内容...'));
        }

        /**
         * 通知外层弹窗当前 Body 类型，方便外层按 Binary 等特殊类型调整尺寸。
         */
        function notifyTypeChange() {
            if (typeof options.onTypeChange === 'function') {
                options.onTypeChange(currentBodyType);
            }
        }

        renderTypeOptions(allowedTypes, initialType);

        textarea.value = initialValue;
        bindMultiformEvents();
        updateTextInputMode();
        applyContentVisibility(contentHidden);

        function updateEditorMetrics() {
            const metrics = getEditorMetrics(textarea.value);
            lineNumberBox.innerHTML = lineNumbersHTML(metrics.lineCount);
            root.style.setProperty('--body-editor-line-count', String(metrics.lineCount));
            root.style.setProperty('--body-editor-max-columns', String(metrics.maxColumns));
            root.style.setProperty('--body-editor-code-min-width', `${metrics.maxColumns + 2}ch`);
            root.style.setProperty(
                '--body-editor-content-height',
                `${metrics.lineCount * EDITOR_LINE_HEIGHT + EDITOR_VERTICAL_PADDING}px`
            );
        }

        function shouldDisableHighlight() {
            return textarea.value.length > HIGHLIGHT_SIZE_LIMIT || typeSelect.value === 'binary';
        }

        function updateHighlight() {
            const disabled = shouldDisableHighlight();
            root.classList.toggle('is-highlight-disabled', disabled);
            if (disabled) {
                highlightCode.innerHTML = '';
                return;
            }

            highlightCode.innerHTML = highlight(textarea.value, typeSelect.value);
        }

        function getEditorLayout() {
            const styles = typeof global.getComputedStyle === 'function'
                ? global.getComputedStyle(textarea)
                : null;
            const fontSize = parseFloat(styles && styles.fontSize) || 14;

            return {
                lineHeight: parseFloat(styles && styles.lineHeight) || EDITOR_LINE_HEIGHT,
                paddingTop: parseFloat(styles && styles.paddingTop) || 12,
                paddingLeft: parseFloat(styles && styles.paddingLeft) || 12,
                gutterWidth: lineNumberBox.offsetWidth || 46,
                charWidth: fontSize * 0.62,
            };
        }

        function transferTextareaScroll() {
            const scrollTop = textarea.scrollTop;
            const scrollLeft = textarea.scrollLeft;

            if (!scrollTop && !scrollLeft) return;

            scrollViewport.scrollTop += scrollTop;
            scrollViewport.scrollLeft += scrollLeft;
            textarea.scrollTop = 0;
            textarea.scrollLeft = 0;
        }

        function scrollCaretIntoView() {
            if (global.document.activeElement !== textarea || typeof textarea.selectionStart !== 'number') {
                return;
            }

            const location = positionToLineColumn(textarea.value, textarea.selectionStart);
            const layout = getEditorLayout();
            const caretTop = layout.paddingTop + (location.line - 1) * layout.lineHeight;
            const caretBottom = caretTop + layout.lineHeight;
            const caretLeft = layout.gutterWidth + layout.paddingLeft + (location.column - 1) * layout.charWidth;
            const viewportTop = scrollViewport.scrollTop;
            const viewportBottom = viewportTop + scrollViewport.clientHeight;
            const viewportLeft = scrollViewport.scrollLeft;
            const viewportRight = viewportLeft + scrollViewport.clientWidth;
            const verticalPadding = 8;
            const horizontalPadding = 32;

            if (caretBottom > viewportBottom - verticalPadding) {
                scrollViewport.scrollTop = caretBottom - scrollViewport.clientHeight + verticalPadding;
            } else if (caretTop < viewportTop + verticalPadding) {
                scrollViewport.scrollTop = Math.max(0, caretTop - verticalPadding);
            }

            if (caretLeft > viewportRight - horizontalPadding) {
                scrollViewport.scrollLeft = caretLeft - scrollViewport.clientWidth + horizontalPadding;
            } else if (caretLeft < viewportLeft + layout.gutterWidth + verticalPadding) {
                scrollViewport.scrollLeft = Math.max(0, caretLeft - layout.gutterWidth - verticalPadding);
            }
        }

        function scheduleCaretSync() {
            const sync = function() {
                transferTextareaScroll();
                scrollCaretIntoView();
            };

            if (typeof global.requestAnimationFrame === 'function') {
                global.requestAnimationFrame(sync);
                return;
            }

            global.setTimeout(sync, 0);
        }

        function dispatchValidity(result) {
            const event = new global.CustomEvent('body-editor:validitychange', {
                bubbles: true,
                detail: result,
            });
            root.dispatchEvent(event);
        }

        function renderValidation(result) {
            root.classList.toggle('is-valid', Boolean(result.valid));
            root.classList.toggle('is-invalid', !result.valid);
            statusElement.textContent = result.valid ? '格式正确' : '格式错误';
            if (result.valid && textarea.value.length > HIGHLIGHT_SIZE_LIMIT) {
                statusElement.textContent = '内容较大，已关闭高亮以保持输入流畅';
            }
            errorElement.textContent = result.valid ? '' : result.message;
            dispatchValidity(result);
            return result;
        }

        function runValidation() {
            return renderValidation(isMultiformMode()
                ? validateMultiform()
                : validateFn(textarea.value, typeSelect.value));
        }

        const debouncedValidate = debounce(runValidation, 180);

        textarea.addEventListener('input', function() {
            updateEditorMetrics();
            updateHighlight();
            debouncedValidate();
            scheduleCaretSync();
        });

        textarea.addEventListener('scroll', function() {
            transferTextareaScroll();
        });

        textarea.addEventListener('wheel', function(event) {
            if (!event.deltaX && !event.deltaY) return;
            scrollViewport.scrollLeft += event.deltaX;
            scrollViewport.scrollTop += event.deltaY;
            event.preventDefault();
        }, { passive: false });

        textarea.addEventListener('keydown', scheduleCaretSync);
        textarea.addEventListener('keyup', scheduleCaretSync);
        textarea.addEventListener('click', scheduleCaretSync);
        textarea.addEventListener('focus', scheduleCaretSync);

        typeSelect.addEventListener('change', function() {
            if (!HIDDEN_BODY_TYPES.includes(currentBodyType)) currentBodyType = typeSelect.value;
            updateTextInputMode();
            applyContentVisibility(contentHidden);
            notifyTypeChange();
            updateEditorMetrics();
            updateHighlight();
            runValidation();
        });

        formatButton.addEventListener('click', function() {
            try {
                textarea.value = format(textarea.value, typeSelect.value);
                updateEditorMetrics();
                updateHighlight();
                runValidation();
                scheduleCaretSync();
            } catch (error) {
                renderValidation({
                    valid: false,
                    line: null,
                    column: null,
                    message: error.message || '格式化失败',
                });
            }
        });

        clearButton.addEventListener('click', function() {
            textarea.value = '';
            updateEditorMetrics();
            updateHighlight();
            runValidation();
            scheduleCaretSync();
        });

        updateEditorMetrics();
        updateHighlight();
        runValidation();
        notifyTypeChange();

        return {
            getValue: function() {
                if (HIDDEN_BODY_TYPES.includes(currentBodyType)) return preservedUnsupportedValue;
                if (isBinaryMode()) {
                    syncBinaryFieldsFromDOM();
                    return serializeBinaryFields(binaryFields);
                }
                return isMultiformMode()
                    ? serializeMultiformFields(multiformFields)
                    : normalizeBodyContent(textarea.value, currentBodyType);
            },
            getValueAsync: async function() {
                if (multiformFileReads.size) await Promise.all(Array.from(multiformFileReads));
                if (HIDDEN_BODY_TYPES.includes(currentBodyType)) return preservedUnsupportedValue;
                if (isBinaryMode()) {
                    syncBinaryFieldsFromDOM();
                    return serializeBinaryFields(binaryFields);
                }
                return isMultiformMode()
                    ? serializeMultiformFields(multiformFields)
                    : normalizeBodyContent(textarea.value, currentBodyType);
            },
            setValue: function(value) {
                textarea.value = decodeBodyData(value || '');
                if (HIDDEN_BODY_TYPES.includes(currentBodyType)) preservedUnsupportedValue = textarea.value;
                const parsedMultiformFields = tryParseMultiformFields(textarea.value);
                if (parsedMultiformFields.length || isMultiformMode()) {
                    multiformFields = parsedMultiformFields;
                    renderMultiformFields();
                }
                const parsedBinaryFields = tryParseBinaryFields(textarea.value);
                if (parsedBinaryFields) {
                    binaryFields = normalizeBinaryFields(parsedBinaryFields);
                    if (binaryFieldEditor) binaryFieldEditor.setFields(binaryFields);
                }
                updateEditorMetrics();
                updateHighlight();
                return runValidation();
            },
            getType: function() {
                return currentBodyType;
            },
            setType: function(type) {
                const nextType = String(type || 'text').toLowerCase();
                renderTypeOptions(allowedTypes, nextType);
                typeSelect.dispatchEvent(new Event('change'));
            },
            setAllowedTypes: function(nextAllowedTypes, preferredType) {
                renderTypeOptions(nextAllowedTypes, preferredType || currentBodyType);
                typeSelect.dispatchEvent(new Event('change'));
            },
            setTypeLabel: function(label) {
                typeLabel.textContent = label || 'Body类型';
            },
            setContentHidden: function(hidden) {
                applyContentVisibility(hidden);
                return runValidation();
            },
            setValidator: function(nextValidateFn) {
                validateFn = typeof nextValidateFn === 'function' ? nextValidateFn : validate;
                return runValidation();
            },
            validate: runValidation,
            focus: function() {
                textarea.focus();
            },
            getBinaryFields: function() {
                syncBinaryFieldsFromDOM();
                return binaryFields.map(field => Object.assign({}, field, { role: 'common' }));
            },
            getMultiformFields: function() {
                return normalizeMultiformFields(multiformFields);
            },
            clearBinaryFields,
            destroy: function() {
                if (binaryFieldEditor && typeof binaryFieldEditor.destroy === 'function') {
                    binaryFieldEditor.destroy();
                }
                container.innerHTML = '';
            },
        };
    }

    KitProxy.bodySyntax = {
        BODY_TYPE_OPTIONS,
        NO_CONTENT_BODY_TYPES,
        TEXT_INPUT_COLLAPSED_BODY_TYPES,
        REQUEST_TEXTLESS_BODY_TYPES,
        HIGHLIGHT_SIZE_LIMIT,
        validate,
        validateRequest,
        format,
        highlight,
        decodeBodyData,
        isTextlessRequestBodyType,
        normalizeBodyContent,
        normalizeRequestBodyContent,
    };

    KitProxy.bodyEditor = {
        create,
    };
})(typeof window !== 'undefined' ? window : globalThis);
