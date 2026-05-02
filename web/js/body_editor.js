(function initKitProxyBodyEditor(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const utils = KitProxy.utils || {};

    const BODY_TYPE_OPTIONS = Object.freeze([
        { value: 'json', label: 'JSON', enabled: true },
        { value: 'xml', label: 'XML', enabled: true },
        { value: 'text', label: 'Text', enabled: true },
        { value: 'binary', label: 'Binary', enabled: false, reserved: true },
    ]);

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

        if (normalizedType === 'json') {
            return validateJson(text);
        }

        if (normalizedType === 'xml') {
            return validateXml(text);
        }

        if (normalizedType === 'binary') {
            return {
                valid: false,
                line: null,
                column: null,
                message: 'Binary Body 暂不支持在线文本编辑',
            };
        }

        return {
            valid: true,
            line: null,
            column: null,
            message: text ? 'Text Body 不做语法校验' : '空 Body 将按未设置处理',
        };
    }

    function format(text, bodyType) {
        const normalizedType = String(bodyType || 'text').toLowerCase();

        if (!text) return '';

        if (normalizedType === 'json') {
            return JSON.stringify(JSON.parse(text), null, 2);
        }

        if (normalizedType === 'xml') {
            if (typeof utils.formatXMLHelper === 'function') {
                return utils.formatXMLHelper(text);
            }
            return text;
        }

        if (normalizedType === 'binary') {
            throw new Error('Binary Body 暂不支持格式化');
        }

        return text;
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

        return allowedTypes.map(option => {
            if (typeof option === 'string') {
                return typeMap[option] || { value: option, label: option.toUpperCase(), enabled: true };
            }

            return Object.assign({}, typeMap[option.value] || {}, option);
        });
    }

    function lineNumbersHTML(lineCount) {
        const count = Math.max(1, Number(lineCount) || 1);
        const lines = [];

        for (let idx = 1; idx <= count; idx += 1) {
            lines.push(String(idx));
        }

        return lines.join('<br>');
    }

    function create(container, options = {}) {
        if (!container) {
            throw new Error('Body 输入组件缺少挂载容器');
        }

        const idPrefix = options.idPrefix || `body-editor-${Date.now()}`;
        const allowedTypes = resolveAllowedTypes(options);
        const initialType = String(options.bodyType || allowedTypes[0]?.value || 'json').toLowerCase();
        const initialValue = decodeBodyData(options.value || '');

        container.innerHTML = `
            <div class="body-editor" data-body-editor="${escapeHtml(idPrefix)}">
                <div class="body-editor-toolbar">
                    <label class="body-editor-type-label" for="${escapeHtml(idPrefix)}-type">Body类型</label>
                    <select id="${escapeHtml(idPrefix)}-type" class="body-editor-type"></select>
                    <button type="button" class="body-editor-format">格式化</button>
                    <button type="button" class="body-editor-clear">清空</button>
                    <span class="body-editor-status" aria-live="polite"></span>
                </div>
                <div class="body-editor-input-wrap">
                    <div class="body-editor-lines" aria-hidden="true">1</div>
                    <textarea id="${escapeHtml(idPrefix)}-content" class="body-editor-textarea" spellcheck="false"></textarea>
                </div>
                <div class="body-editor-error" aria-live="polite"></div>
            </div>
        `;

        const root = container.querySelector('.body-editor');
        const typeSelect = container.querySelector('.body-editor-type');
        const textarea = container.querySelector('.body-editor-textarea');
        const lineNumberBox = container.querySelector('.body-editor-lines');
        const formatButton = container.querySelector('.body-editor-format');
        const clearButton = container.querySelector('.body-editor-clear');
        const statusElement = container.querySelector('.body-editor-status');
        const errorElement = container.querySelector('.body-editor-error');

        allowedTypes.forEach(option => {
            const optionElement = document.createElement('option');
            optionElement.value = option.value;
            optionElement.textContent = option.reserved ? `${option.label}（预留）` : option.label;
            optionElement.disabled = option.enabled === false;
            typeSelect.appendChild(optionElement);
        });

        if (!Array.from(typeSelect.options).some(option => option.value === initialType)) {
            const fallbackOption = document.createElement('option');
            fallbackOption.value = initialType;
            fallbackOption.textContent = initialType.toUpperCase();
            typeSelect.appendChild(fallbackOption);
        }

        typeSelect.value = initialType;
        textarea.value = initialValue;
        textarea.placeholder = options.placeholder || '输入 Body 内容...';
        textarea.readOnly = Boolean(options.readonly || typeSelect.value === 'binary');

        function updateLineNumbers() {
            const lineCount = textarea.value.split('\n').length;
            lineNumberBox.innerHTML = lineNumbersHTML(lineCount);
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
            errorElement.textContent = result.valid ? '' : result.message;
            dispatchValidity(result);
            return result;
        }

        function runValidation() {
            return renderValidation(validate(textarea.value, typeSelect.value));
        }

        const debouncedValidate = debounce(runValidation, 180);

        textarea.addEventListener('input', function() {
            updateLineNumbers();
            debouncedValidate();
        });

        textarea.addEventListener('scroll', function() {
            lineNumberBox.scrollTop = textarea.scrollTop;
        });

        typeSelect.addEventListener('change', function() {
            textarea.readOnly = Boolean(options.readonly || typeSelect.value === 'binary');
            textarea.placeholder = typeSelect.value === 'binary'
                ? 'Binary Body 暂不支持在线文本编辑'
                : (options.placeholder || '输入 Body 内容...');
            runValidation();
        });

        formatButton.addEventListener('click', function() {
            try {
                textarea.value = format(textarea.value, typeSelect.value);
                updateLineNumbers();
                runValidation();
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
            updateLineNumbers();
            runValidation();
        });

        updateLineNumbers();
        runValidation();

        return {
            getValue: function() {
                return textarea.value;
            },
            setValue: function(value) {
                textarea.value = decodeBodyData(value || '');
                updateLineNumbers();
                return runValidation();
            },
            getType: function() {
                return typeSelect.value;
            },
            setType: function(type) {
                typeSelect.value = String(type || 'text').toLowerCase();
                typeSelect.dispatchEvent(new Event('change'));
            },
            validate: runValidation,
            focus: function() {
                textarea.focus();
            },
            destroy: function() {
                container.innerHTML = '';
            },
        };
    }

    KitProxy.bodySyntax = {
        BODY_TYPE_OPTIONS,
        validate,
        format,
        decodeBodyData,
    };

    KitProxy.bodyEditor = {
        create,
    };
})(typeof window !== 'undefined' ? window : globalThis);
