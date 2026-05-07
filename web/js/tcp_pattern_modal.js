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

    function toFiniteNumber(value, fallback = null) {
        if (value === '' || value == null) return fallback;
        const numberValue = Number(value);
        return Number.isFinite(numberValue) ? numberValue : fallback;
    }

    function cloneField(field) {
        return {
            name: String(field && field.name != null ? field.name : ''),
            idx: toFiniteNumber(field && field.idx, null),
            byte_pos: toFiniteNumber(field && field.byte_pos, null),
            byte_len: toFiniteNumber(field && field.byte_len, null),
            type: String(field && field.type != null ? field.type : ''),
            value: String(field && field.value != null ? field.value : ''),
        };
    }

    function fieldFromNode(node) {
        if (!node || typeof node.querySelector !== 'function') return cloneField({});

        return cloneField({
            name: node.querySelector('.pattern-field-name')?.value,
            idx: node.querySelector('.pattern-field-idx')?.value,
            byte_pos: node.querySelector('.pattern-field-byte-pos')?.value,
            byte_len: node.querySelector('.pattern-field-byte-len')?.value,
            type: node.querySelector('.pattern-field-type')?.value,
            value: node.querySelector('.pattern-field-value')?.value,
        });
    }

    function normalizeFieldEntry(entry) {
        if (entry && typeof entry.querySelector === 'function') {
            return fieldFromNode(entry);
        }
        return cloneField(entry || {});
    }

    function normalizePatternInfo(input, options = {}) {
        const source = input || {};
        const normalized = {
            least_byte_len: toFiniteNumber(source.least_byte_len, 0) || 0,
            special_fields: {},
            common_fields: [],
        };

        const specialFields = source.special_fields || {};
        Object.keys(specialFields).forEach(key => {
            normalized.special_fields[key] = normalizeFieldEntry(specialFields[key]);
        });

        if (Array.isArray(source.common_fields)) {
            normalized.common_fields = source.common_fields.map(normalizeFieldEntry);
        }

        if (options.sortSpecial) {
            const sortedEntries = Object.keys(normalized.special_fields)
                .sort((left, right) => {
                    const leftField = normalized.special_fields[left];
                    const rightField = normalized.special_fields[right];
                    return (leftField.idx ?? Number.MAX_SAFE_INTEGER) - (rightField.idx ?? Number.MAX_SAFE_INTEGER);
                });
            const sortedSpecial = {};
            sortedEntries.forEach(key => {
                sortedSpecial[key] = normalized.special_fields[key];
            });
            normalized.special_fields = sortedSpecial;
        }

        return normalized;
    }

    function sortPatternFields(fields) {
        return (Array.isArray(fields) ? fields : [])
            .map(cloneField)
            .sort((left, right) => {
                const leftIdx = Number.isFinite(left.idx) ? left.idx : Number.MAX_SAFE_INTEGER;
                const rightIdx = Number.isFinite(right.idx) ? right.idx : Number.MAX_SAFE_INTEGER;
                return leftIdx - rightIdx;
            });
    }

    function computeCommonFieldBytePositions(fields) {
        let offset = 0;
        return (Array.isArray(fields) ? fields : []).map(field => {
            const nextField = cloneField(field);
            const length = Number.isFinite(nextField.byte_len) ? nextField.byte_len : 0;
            nextField.byte_pos = offset;
            offset += Math.max(0, length);
            return nextField;
        });
    }

    function serializePatternInfo(root, options = {}) {
        const normalized = normalizePatternInfo(root);
        const commonFields = options.sortCommon
            ? sortPatternFields(normalized.common_fields)
            : normalized.common_fields;

        return {
            least_byte_len: Number(normalized.least_byte_len || 0),
            special_fields: normalized.special_fields,
            common_fields: computeCommonFieldBytePositions(commonFields),
        };
    }

    function validateField(field, label, options) {
        const errors = [];
        const requiresBytePos = Boolean(options.requiresBytePos);

        if (!String(field.name || '').trim()) {
            errors.push(`${label}：字段名称不能为空`);
        }

        if (!Number.isInteger(field.idx) || field.idx < 0) {
            errors.push(`${label}：idx 必须是非负整数`);
        }

        if (requiresBytePos && (!Number.isInteger(field.byte_pos) || field.byte_pos < 0)) {
            errors.push(`${label}：Byte 起始位置必须是非负整数`);
        }

        if (!Number.isInteger(field.byte_len) || field.byte_len <= 0) {
            errors.push(`${label}：Byte 长度必须是大于 0 的整数`);
        }

        if (!String(field.type || '').trim()) {
            errors.push(`${label}：类型不能为空`);
        }

        return errors;
    }

    function validateOverlaps(fields, groupLabel) {
        const errors = [];
        const spans = (Array.isArray(fields) ? fields : [])
            .map((field, index) => ({
                index,
                name: field.name || `字段${index + 1}`,
                start: field.byte_pos,
                end: Number.isFinite(field.byte_pos) && Number.isFinite(field.byte_len)
                    ? field.byte_pos + field.byte_len
                    : null,
            }))
            .filter(span => Number.isFinite(span.start) && Number.isFinite(span.end) && span.end > span.start)
            .sort((left, right) => left.start - right.start);

        for (let idx = 1; idx < spans.length; idx += 1) {
            const prev = spans[idx - 1];
            const current = spans[idx];
            if (current.start < prev.end) {
                errors.push(`${groupLabel}：${current.name} 与 ${prev.name} 字节范围重叠`);
            }
        }

        return errors;
    }

    function validatePatternInfo(patternInfo, options = {}) {
        const mode = options.mode || 'common';
        const normalized = normalizePatternInfo(patternInfo);
        const serialized = serializePatternInfo(normalized);
        const errors = [];
        const specialEntries = Object.keys(normalized.special_fields).map(key => ({
            key,
            field: normalized.special_fields[key],
        }));

        if (mode === 'special' && (!Number.isInteger(normalized.least_byte_len) || normalized.least_byte_len <= 0)) {
            errors.push('解析所需最小长度必须是大于 0 的整数');
        }

        specialEntries.forEach(entry => {
            errors.push(...validateField(entry.field, `特殊字段 ${entry.field.name || entry.key}`, {
                requiresBytePos: true,
            }));
        });

        normalized.common_fields.forEach((field, index) => {
            errors.push(...validateField(field, `普通字段 ${field.name || index + 1}`, {
                requiresBytePos: false,
            }));
        });

        if (mode === 'special') {
            errors.push(...validateOverlaps(specialEntries.map(entry => entry.field), '特殊字段'));
        } else {
            errors.push(...validateOverlaps(serialized.common_fields, '普通字段'));
        }

        return {
            valid: errors.length === 0,
            errors,
        };
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

    function updatePatternField(fieldNode, fieldInfo, isSpecial = false) {
        const field = cloneField(fieldInfo || {});
        if (!fieldNode) return fieldNode;

        fieldNode.querySelector(".pattern-field-name").value = field.name;
        fieldNode.querySelector(".pattern-field-idx").value = field.idx ?? '';
        fieldNode.querySelector(".pattern-field-byte-len").value = field.byte_len ?? '';
        if (isSpecial || fieldNode.querySelector(".pattern-field-byte-pos")) {
            fieldNode.querySelector(".pattern-field-byte-pos").value = field.byte_pos ?? '';
        }
        fieldNode.querySelector(".pattern-field-type").value = field.type;
        fieldNode.querySelector(".pattern-field-value").value = field.value;

        return fieldNode;
    }

    function createPatternField(namePlaceholder = '', specialName = '', fieldInfo = null) {
        const field = cloneField(fieldInfo || {});
        const fieldDiv = document.createElement('div');
        fieldDiv.className = 'pattern-field-container';
        fieldDiv.dataset.fieldScope = specialName ? 'special' : 'common';
        if (specialName) {
            fieldDiv.id = specialName;
            fieldDiv.dataset.specialKey = specialName;
        }

        fieldDiv.innerHTML = `
            <div class="pattern-field">
                <div class="pattern-cell pattern-cell-idx">
                    <label>数组下标</label>
                    <input type="number" class="pattern-field-idx" value="${escapeHTML(field.idx ?? '')}" min="0" max="1024" placeholder="0" required>
                </div>
                <div class="pattern-cell pattern-cell-name">
                    <label>名称</label>
                    <input type="text" class="pattern-field-name" value="${escapeHTML(field.name)}" placeholder="${escapeHTML(namePlaceholder)}" required>
                </div>
                <div class="pattern-cell pattern-cell-byte-pos">
                    <label>Byte起始位置</label>
                    <input type="number" class="pattern-field-byte-pos" value="${escapeHTML(field.byte_pos ?? '')}" min="0" max="65535" placeholder="自动" required>
                </div>
                <div class="pattern-cell pattern-cell-byte-len">
                    <label>Byte长度</label>
                    <input type="number" class="pattern-field-byte-len" value="${escapeHTML(field.byte_len ?? '')}" min="1" max="65535" placeholder="1" required>
                </div>
                <div class="pattern-cell pattern-cell-type">
                    <label>类型</label>
                    <select class="pattern-field-type" required>${patternFieldTypeOptionsHTML(field.type)}</select>
                </div>
                <div class="pattern-cell pattern-cell-value">
                    <label>值</label>
                    <div class="pattern-field-value-container">
                        <input type="text" class="pattern-field-value" value="${escapeHTML(field.value)}" placeholder="示例: H010203">
                        <button type="button" class="pattern-field-value-display-btn" title="值保持原始存储语义">H</button>
                    </div>
                </div>
                <div class="pattern-cell pattern-cell-actions">
                    <label>操作</label>
                    <div class="pattern-field-actions">
                        <button type="button" class="pattern-field-move-up" title="上移">上移</button>
                        <button type="button" class="pattern-field-move-down" title="下移">下移</button>
                        <button type="button" class="del-field-btn" title="删除字段">删除</button>
                    </div>
                </div>
            </div>
        `;

        const nameInput = fieldDiv.querySelector('.pattern-field-name');
        nameInput.addEventListener('dblclick', function() {
            if (!this.value && this.placeholder) {
                this.value = this.placeholder;
                this.dispatchEvent(new Event('input', { bubbles: true }));
            }
        });

        return fieldDiv;
    }

    function patternListSortByIdx(patternList) {
        let children = [];
        if (Array.isArray(patternList)) {
            children = patternList;
        } else if (patternList && patternList.children) {
            children = Array.from(patternList.children);
        }

        return children.sort((left, right) => {
            const leftIdx = toFiniteNumber(left.querySelector('.pattern-field-idx')?.value, Number.MAX_SAFE_INTEGER);
            const rightIdx = toFiniteNumber(right.querySelector('.pattern-field-idx')?.value, Number.MAX_SAFE_INTEGER);
            return leftIdx - rightIdx;
        });
    }

    function setFieldNodeReadonly(fieldNode, readonly) {
        fieldNode.classList.toggle('is-readonly', Boolean(readonly));
        fieldNode.querySelectorAll('input, select').forEach(input => {
            input.disabled = Boolean(readonly);
        });
        fieldNode.querySelector('.del-field-btn').disabled = Boolean(readonly);
        fieldNode.querySelector('.pattern-field-move-up').disabled = Boolean(readonly);
        fieldNode.querySelector('.pattern-field-move-down').disabled = Boolean(readonly);
    }

    function readFieldNode(fieldNode) {
        const field = fieldFromNode(fieldNode);
        if (fieldNode.dataset.fieldScope === 'common') {
            field.byte_pos = null;
        }
        return field;
    }

    function readPatternInfoFromDOM(modal) {
        const root = {
            least_byte_len: toFiniteNumber(modal.querySelector('.pattern-least-length')?.value, 0) || 0,
            special_fields: {},
            common_fields: [],
        };

        modal.querySelectorAll('.pattern-field-container').forEach(fieldNode => {
            const field = readFieldNode(fieldNode);
            const key = fieldNode.dataset.specialKey || fieldNode.id || '';

            if (fieldNode.dataset.fieldScope === 'special' && key) {
                root.special_fields[key.replaceAll('-', '_')] = field;
            } else {
                root.common_fields.push(field);
            }
        });

        return root;
    }

    function totalByteLength(patternInfo, mode) {
        if (mode === 'special') {
            return Object.keys(patternInfo.special_fields || {}).reduce((max, key) => {
                const field = patternInfo.special_fields[key];
                if (!Number.isFinite(field.byte_pos) || !Number.isFinite(field.byte_len)) return max;
                return Math.max(max, field.byte_pos + field.byte_len);
            }, 0);
        }

        return (patternInfo.common_fields || []).reduce((sum, field) => {
            return sum + Math.max(0, Number.isFinite(field.byte_len) ? field.byte_len : 0);
        }, 0);
    }

    function renderSummary(modal, serialized, validation, isSpecial) {
        const summary = modal.querySelector('.pattern-summary');
        if (!summary) return;

        const specialCount = Object.keys(serialized.special_fields || {}).length;
        const commonCount = serialized.common_fields.length;
        const fieldCount = isSpecial ? specialCount : commonCount;
        const totalLength = totalByteLength(serialized, isSpecial ? 'special' : 'common');

        summary.innerHTML = `
            <div class="pattern-summary-item">
                <span>配置对象</span>
                <strong>${isSpecial ? '项目特殊字段' : '协议项普通字段'}</strong>
            </div>
            <div class="pattern-summary-item">
                <span>字段数量</span>
                <strong>${fieldCount}</strong>
            </div>
            <div class="pattern-summary-item">
                <span>最小解析长度</span>
                <strong>${serialized.least_byte_len || 0} Byte</strong>
            </div>
            <div class="pattern-summary-item">
                <span>总字节长度</span>
                <strong>${totalLength} Byte</strong>
            </div>
            <div class="pattern-summary-item ${validation.valid ? 'is-ok' : 'is-error'}">
                <span>校验状态</span>
                <strong>${validation.valid ? '无错误' : `${validation.errors.length} 个错误`}</strong>
            </div>
        `;
    }

    function collectPreviewFields(serialized, isSpecial) {
        if (isSpecial) {
            return Object.keys(serialized.special_fields || {}).map(key => {
                const field = cloneField(serialized.special_fields[key]);
                field.previewKey = key;
                return field;
            }).sort((left, right) => {
                const leftPos = Number.isFinite(left.byte_pos) ? left.byte_pos : Number.MAX_SAFE_INTEGER;
                const rightPos = Number.isFinite(right.byte_pos) ? right.byte_pos : Number.MAX_SAFE_INTEGER;
                return leftPos - rightPos;
            });
        }

        return serialized.common_fields.map(cloneField);
    }

    function renderPreview(modal, serialized, isSpecial) {
        const preview = modal.querySelector('.pattern-byte-layout');
        if (!preview) return;

        const fields = collectPreviewFields(serialized, isSpecial);

        if (!fields.length) {
            preview.innerHTML = '<div class="pattern-layout-empty">暂无字段</div>';
            return;
        }

        preview.innerHTML = fields.map((field, index) => {
            const valid = String(field.name || '').trim() &&
                Number.isFinite(field.byte_pos) &&
                Number.isFinite(field.byte_len) &&
                field.byte_len > 0 &&
                String(field.type || '').trim();

            return `
                <div class="pattern-byte-block ${valid ? '' : 'is-error'}" style="--pattern-span:${Math.max(1, Number(field.byte_len) || 1)}">
                    <span class="pattern-byte-offset">offset ${escapeHTML(field.byte_pos ?? '?')}</span>
                    <strong>${escapeHTML(field.name || `字段${index + 1}`)}</strong>
                    <span>${escapeHTML(field.type || '未选类型')} · ${escapeHTML(field.byte_len ?? '?')} Byte</span>
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
        errorBox.innerHTML = validation.errors
            .map(error => `<div>${escapeHTML(error)}</div>`)
            .join('');
    }

    function refreshPatternModal(modal, isSpecial) {
        const serialized = serializePatternInfo(readPatternInfoFromDOM(modal));
        const validation = validatePatternInfo(serialized, { mode: isSpecial ? 'special' : 'common' });

        renderSummary(modal, serialized, validation, isSpecial);
        renderPreview(modal, serialized, isSpecial);
        renderErrors(modal, validation);
        return { serialized, validation };
    }

    function bindPatternFieldNodeActions(fieldNode, patternList, modal, isSpecial) {
        fieldNode.querySelector('.del-field-btn')?.addEventListener('click', function() {
            if (fieldNode.dataset.fieldScope === 'special') return;
            fieldNode.remove();
            refreshPatternModal(modal, isSpecial);
        });

        fieldNode.querySelector('.pattern-field-move-up')?.addEventListener('click', function() {
            if (fieldNode.dataset.fieldScope === 'special') return;
            const previous = fieldNode.previousElementSibling;
            if (previous && previous.dataset.fieldScope === 'common') {
                patternList.insertBefore(fieldNode, previous);
                refreshPatternModal(modal, isSpecial);
            }
        });

        fieldNode.querySelector('.pattern-field-move-down')?.addEventListener('click', function() {
            if (fieldNode.dataset.fieldScope === 'special') return;
            const next = fieldNode.nextElementSibling;
            if (next && next.dataset.fieldScope === 'common') {
                patternList.insertBefore(next, fieldNode);
                refreshPatternModal(modal, isSpecial);
            }
        });

        fieldNode.addEventListener('input', function() {
            refreshPatternModal(modal, isSpecial);
        });

        fieldNode.addEventListener('change', function() {
            refreshPatternModal(modal, isSpecial);
        });
    }

    function appendFieldNode(patternList, fieldNode, modal, isSpecial, options = {}) {
        if (options.commonBytePosReadonly || fieldNode.dataset.fieldScope === 'common') {
            const bytePosInput = fieldNode.querySelector('.pattern-field-byte-pos');
            bytePosInput.required = false;
            bytePosInput.readOnly = true;
            bytePosInput.placeholder = '自动';
        }

        if (fieldNode.dataset.fieldScope === 'special') {
            fieldNode.querySelector('.del-field-btn').disabled = true;
            fieldNode.querySelector('.pattern-field-move-up').disabled = true;
            fieldNode.querySelector('.pattern-field-move-down').disabled = true;
            if (options.readonlySpecial) {
                setFieldNodeReadonly(fieldNode, true);
            }
        }

        bindPatternFieldNodeActions(fieldNode, patternList, modal, isSpecial);
        patternList.appendChild(fieldNode);
        return fieldNode;
    }

    function createCustomTcpPatternModal(targetElement, fieldTitle, patternInfosMap, statusElement, isSpecial = false, userHandleCb = null) {
        const resolvedTargetElement = typeof targetElement === 'string'
            ? document.getElementById(targetElement)
            : targetElement;
        const resolvedStatusElement = typeof statusElement === 'string'
            ? document.getElementById(statusElement)
            : statusElement;
        const normalized = normalizePatternInfo(patternInfosMap, { sortSpecial: true });
        const configModal = document.createElement('div');
        configModal.className = 'modal-overlay';
        configModal.innerHTML = `
            <div class="config-pattern-modal">
                <div class="modal-header">
                    <h3>配置TCP格式信息</h3>
                    <button type="button" class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <form id="config-pattern-modal-form" class="config-pattern-modal-form">
                        ${isSpecial ? `
                        <div class="form-group pattern-least-length-group">
                            <label for="pattern-least-length">解析所需最小长度(Byte)</label>
                            <input type="number" id="pattern-least-length" class="pattern-least-length" value="${escapeHTML(normalized.least_byte_len || '')}" min="1" max="65535" placeholder="1-65535" required>
                        </div>` : ''}
                        <div class="pattern-summary" aria-live="polite"></div>
                        <div class="pattern-layout-section">
                            <div class="pattern-section-title">字节布局预览</div>
                            <div class="pattern-byte-layout"></div>
                        </div>
                        <div class="pattern-field-info" id="special-pattern-fields">
                            <div class="pattern-field-info-header">
                                <label>${escapeHTML(fieldTitle || '字段配置')}</label>
                                <div class="pattern-field-toolbar">
                                    <button type="button" class="add-field-btn" ${isSpecial ? 'hidden' : ''}>新增字段</button>
                                    <button type="button" class="sort-field-btn" title="默认升序排列">按 idx 排序</button>
                                </div>
                            </div>
                            <div class="pattern-field-grid-labels" aria-hidden="true">
                                <span>数组下标</span>
                                <span>名称</span>
                                <span>Byte起始位置</span>
                                <span>Byte长度</span>
                                <span>类型</span>
                                <span>值</span>
                                <span>操作</span>
                            </div>
                            <div class="pattern-list"></div>
                        </div>
                        <div class="pattern-validation-errors" aria-live="polite"></div>
                        <div class="form-actions">
                            <button type="button" class="clear-btn" ${isSpecial ? 'hidden' : ''}>清除普通字段</button>
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="submit" class="confirm-btn">确定</button>
                        </div>
                    </form>
                </div>
            </div>
        `;

        document.body.appendChild(configModal);

        const patternList = configModal.querySelector('.pattern-list');

        Object.keys(normalized.special_fields || {}).forEach(key => {
            const fieldNode = createPatternField('', key, normalized.special_fields[key]);
            appendFieldNode(patternList, fieldNode, configModal, isSpecial, {
                readonlySpecial: !isSpecial,
            });
        });

        (normalized.common_fields || []).forEach(field => {
            const fieldNode = createPatternField('其他普通字段', '', field);
            appendFieldNode(patternList, fieldNode, configModal, isSpecial, {
                commonBytePosReadonly: true,
            });
        });

        patternList.replaceChildren(...patternListSortByIdx(patternList));
        refreshPatternModal(configModal, isSpecial);

        configModal.querySelector('.pattern-least-length')?.addEventListener('input', function() {
            refreshPatternModal(configModal, isSpecial);
        });

        configModal.querySelector('.add-field-btn')?.addEventListener('click', function() {
            const fieldNode = createPatternField('其他普通字段', '', {
                idx: patternList.querySelectorAll('[data-field-scope="common"]').length,
                byte_len: 1,
            });
            appendFieldNode(patternList, fieldNode, configModal, isSpecial, {
                commonBytePosReadonly: true,
            });
            refreshPatternModal(configModal, isSpecial);
        });

        configModal.querySelector('.sort-field-btn')?.addEventListener('click', function() {
            const specialNodes = Array.from(patternList.children).filter(node => node.dataset.fieldScope === 'special');
            const commonNodes = patternListSortByIdx(Array.from(patternList.children).filter(node => node.dataset.fieldScope === 'common'));
            patternList.replaceChildren(...specialNodes, ...commonNodes);
            refreshPatternModal(configModal, isSpecial);
        });

        configModal.querySelector('.clear-btn')?.addEventListener('click', function(event) {
            event.stopPropagation();
            if (!confirm('确定要清除当前全部普通字段吗？')) return;

            patternList.querySelectorAll('[data-field-scope="common"]').forEach(node => node.remove());
            if (resolvedTargetElement && resolvedTargetElement.dataset) {
                delete resolvedTargetElement.dataset.patternInfos;
            }
            if (resolvedStatusElement) {
                resolvedStatusElement.style.display = 'none';
            }
            refreshPatternModal(configModal, isSpecial);
        });

        function closeModal() {
            utils.removeDomNode ? utils.removeDomNode(configModal) : document.body.removeChild(configModal);
        }

        configModal.querySelector('.cancel-btn')?.addEventListener('click', closeModal);
        configModal.querySelector('.close-modal')?.addEventListener('click', closeModal);

        configModal.querySelector('#config-pattern-modal-form').addEventListener('submit', async function(event) {
            event.preventDefault();
            event.stopPropagation();

            const serialized = serializePatternInfo(readPatternInfoFromDOM(configModal));
            const validation = validatePatternInfo(serialized, { mode: isSpecial ? 'special' : 'common' });
            renderSummary(configModal, serialized, validation, isSpecial);
            renderPreview(configModal, serialized, isSpecial);
            renderErrors(configModal, validation);

            if (!validation.valid) {
                return;
            }

            if (resolvedTargetElement && resolvedTargetElement.dataset) {
                resolvedTargetElement.dataset.patternInfos = JSON.stringify(serialized);
            }
            if (resolvedStatusElement) {
                resolvedStatusElement.style.display = 'inline';
            }

            if (userHandleCb) {
                await userHandleCb(serialized);
            }

            closeModal();
        });

        return configModal;
    }

    KitProxy.tcpPatternEditor = {
        normalizePatternInfo,
        serializePatternInfo,
        validatePatternInfo,
        sortPatternFields,
        computeCommonFieldBytePositions,
    };

    global.updatePatternField = updatePatternField;
    global.createPatternField = createPatternField;
    global.patternListSortByIdx = patternListSortByIdx;
    global.createCustomTcpPatternModal = createCustomTcpPatternModal;
})(typeof window !== 'undefined' ? window : globalThis);
