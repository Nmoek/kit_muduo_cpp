/**
 * 协议项卡片请求侧 Body 内容配置暂时隐藏，待请求校验模块支持完整 Body 配置后恢复为 false。
 * @type {boolean}
 */
const PROTOCOL_ITEM_HIDE_REQUEST_BODY_CONTENT_CONFIG = true;

/**
 * 生成不同协议项配置子类网格
 * @param {派生子类} item 
 * @param {协议项信息} protocol 
 * @returns 
 */
function createProtocolItemGrids(item, protocol) {
    return item.create(protocol);
}

/**
 * HTTP Headers 配置弹窗工具，供协议项表单页和卡片详情共用。
 */
(function initHttpHeadersTools(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    if (KitProxy.httpHeaders) return;

    const HTTP_HEADER_NAME_RE = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;

    /**
     * @param {any} value
     * @returns {string}
     */
    function escapeHTML(value) {
        return KitProxy.utils && KitProxy.utils.escapeHTML
            ? KitProxy.utils.escapeHTML(value)
            : String(value == null ? '' : value);
    }

    /**
     * @param {any} headers
     * @returns {Record<string, string>}
     */
    function normalize(headers) {
        const normalized = {};
        if (!headers || typeof headers !== 'object' || Array.isArray(headers)) {
            return normalized;
        }

        Object.keys(headers).forEach(name => {
            const headerName = String(name || '').trim();
            if (!headerName) return;
            normalized[headerName] = String(headers[name] == null ? '' : headers[name]);
        });
        return normalized;
    }

    /**
     * @param {Record<string, string>} headers
     * @returns {Array<{ name: string; value: string; }>}
     */
    function entries(headers) {
        const normalized = normalize(headers);
        return Object.keys(normalized).map(name => ({
            name,
            value: normalized[name],
        }));
    }

    /**
     * @param {Record<string, string>} headers
     * @returns {string}
     */
    function summarize(headers) {
        const count = Object.keys(normalize(headers)).length;
        return count > 0 ? `已设置 ${count} 条` : '未设置';
    }

    /**
     * @param {HTMLElement} field
     * @param {Record<string, string>} headers
     */
    function updateFieldState(field, headers) {
        if (!field) return;
        const normalized = normalize(headers);
        const hasHeaders = Object.keys(normalized).length > 0;
        const indicator = field.querySelector('.header-fields-indicator');
        const valueElement = field.querySelector('.value');

        field.dataset.headers = JSON.stringify(normalized);
        if (indicator) {
            indicator.classList.toggle('has', hasHeaders);
            indicator.classList.toggle('no', !hasHeaders);
        }
        if (valueElement) {
            valueElement.textContent = summarize(normalized);
        }
    }

    /**
     * @param {HTMLElement} button
     * @param {Record<string, string>} headers
     */
    function updateButtonState(button, headers) {
        if (!button) return;
        const normalized = normalize(headers);
        const status = button.closest('.form-group')?.querySelector('.import-status');

        button.dataset.headers = JSON.stringify(normalized);
        if (status) {
            status.textContent = summarize(normalized);
            status.style.display = 'inline';
        }
    }

    /**
     * @param {{ name?: string; value?: string; }} header
     * @returns {HTMLElement}
     */
    function createRow(header = {}) {
        const row = document.createElement('div');
        row.className = 'http-header-row';
        row.innerHTML = `
            <div class="http-header-cell">
                <label>Header 名称</label>
                <input type="text" class="http-header-name" value="${escapeHTML(header.name || '')}" placeholder="Key" aria-label="Header 名称">
            </div>
            <div class="http-header-cell">
                <label>Header 值</label>
                <input type="text" class="http-header-value" value="${escapeHTML(header.value || '')}" placeholder="Value" aria-label="Header 值">
            </div>
            <div class="http-header-actions">
                <button type="button" class="delete-http-header-btn" aria-label="删除 Header" title="删除 Header">&times;</button>
            </div>
        `;
        row.querySelector('.delete-http-header-btn')?.addEventListener('click', function() {
            row.remove();
        });
        return row;
    }

    /**
     * @param {HTMLElement} modal
     * @returns {{ valid: boolean; headers: Record<string, string>; errors: Array<string>; }}
     */
    function collectFromModal(modal) {
        const headers = {};
        const errors = [];
        const seenNames = new Set();

        modal.querySelectorAll('.http-header-row').forEach((row, index) => {
            const name = row.querySelector('.http-header-name')?.value.trim() || '';
            const value = row.querySelector('.http-header-value')?.value.trim() || '';

            if (!name && !value) return;

            if (!name) {
                errors.push(`第 ${index + 1} 行：Header 名称不能为空`);
                return;
            }

            if (!HTTP_HEADER_NAME_RE.test(name)) {
                errors.push(`第 ${index + 1} 行：Header 名称只能使用 HTTP token 字符`);
                return;
            }

            const normalizedName = name.toLowerCase();
            if (seenNames.has(normalizedName)) {
                errors.push(`第 ${index + 1} 行：Header 名称重复`);
                return;
            }

            seenNames.add(normalizedName);
            headers[name] = value;
        });

        return {
            valid: errors.length === 0,
            headers,
            errors,
        };
    }

    /**
     * @param {string} title
     * @param {Record<string, string>} headers
     * @param {(headers: Record<string, string>) => (void | boolean | Promise<void | boolean>)} onSave
     * @returns {HTMLElement}
     */
    function openModal(title, headers, onSave) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.innerHTML = `
            <div class="http-headers-modal">
                <div class="modal-header">
                    <h3>${escapeHTML(title)}</h3>
                    <button type="button" class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <form class="http-headers-form">
                        <div class="http-headers-toolbar">
                            <button type="button" class="add-http-header-btn">新增 Header</button>
                            <button type="button" class="clear-http-headers-btn">清空</button>
                        </div>
                        <div class="http-headers-table">
                            <div class="http-headers-head" aria-hidden="true">
                                <span>Key</span>
                                <span>Value</span>
                                <span></span>
                            </div>
                            <div class="http-headers-list"></div>
                        </div>
                        <div class="http-headers-error" aria-live="polite"></div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="submit" class="confirm-btn">确定</button>
                        </div>
                    </form>
                </div>
            </div>
        `;

        document.body.appendChild(modal);

        const closeModal = KitProxy.utils && KitProxy.utils.bindModalCloseActions
            ? KitProxy.utils.bindModalCloseActions(modal)
            : function() { modal.remove(); };
        const list = modal.querySelector('.http-headers-list');
        const errorBox = modal.querySelector('.http-headers-error');

        const currentEntries = entries(headers);
        currentEntries.forEach(entry => {
            list.appendChild(createRow(entry));
        });
        if (currentEntries.length === 0) {
            list.appendChild(createRow());
        }

        modal.querySelector('.add-http-header-btn')?.addEventListener('click', function() {
            list.appendChild(createRow());
        });

        modal.querySelector('.clear-http-headers-btn')?.addEventListener('click', function() {
            list.innerHTML = '';
            list.appendChild(createRow());
            if (errorBox) errorBox.textContent = '';
        });

        modal.querySelector('.http-headers-form')?.addEventListener('submit', function(event) {
            event.preventDefault();
            event.stopPropagation();

            const result = collectFromModal(modal);
            if (!result.valid) {
                if (errorBox) {
                    errorBox.innerHTML = result.errors
                        .map(error => `<div>${escapeHTML(error)}</div>`)
                        .join('');
                }
                return;
            }

            const confirmButton = modal.querySelector('.confirm-btn');
            if (confirmButton.disabled) return;

            confirmButton.disabled = true;
            if (errorBox) errorBox.textContent = '';

            try {
                const saveResult = onSave(result.headers);
                if (saveResult && typeof saveResult.then === 'function') {
                    saveResult.then(function(asyncSaveResult) {
                        if (asyncSaveResult !== false) {
                            closeModal();
                        }
                    }).catch(function(error) {
                        if (errorBox) {
                            errorBox.textContent = error && error.message ? error.message : '保存 Headers 失败';
                        }
                    }).finally(function() {
                        confirmButton.disabled = false;
                    });
                    return;
                }

                if (saveResult !== false) {
                    closeModal();
                }
            } catch (error) {
                if (errorBox) {
                    errorBox.textContent = error && error.message ? error.message : '保存 Headers 失败';
                }
            }

            confirmButton.disabled = false;
        });

        return modal;
    }

    KitProxy.httpHeaders = {
        normalize,
        entries,
        summarize,
        updateFieldState,
        updateButtonState,
        openModal,
        collectFromModal,
    };
})(typeof window !== 'undefined' ? window : globalThis);

if (typeof globalThis.readDatasetProjectId !== 'function') {
    globalThis.readDatasetProjectId = function(value) {
        const directId = Number(value);
        if (Number.isInteger(directId) && directId > 0) return directId;
        return ExtractId(value);
    };
}


var httpProtocolItemGrids = {
    create: function(protocol) {
        const grids = document.createElement('div');
        grids.className = 'details-grid http';
        const escape = KitProxy.utils && KitProxy.utils.escapeHTML
            ? KitProxy.utils.escapeHTML
            : function(value) { return String(value == null ? '' : value); };
        const reqCfg = protocol.req_cfg || {};
        const respCfg = protocol.resp_cfg || {};
        // 老数据可能没有响应码，卡片统一按后端当前默认行为显示为 200。
        const statusCode = KitProxy.utils && typeof KitProxy.utils.normalizeHttpStatusCode === 'function'
            ? KitProxy.utils.normalizeHttpStatusCode(respCfg.status_code, 200)
            : (Number(respCfg.status_code) || 200);
        const requestPath = reqCfg.path || '';
        grids.innerHTML = `
            <div class="http-details-row http-request-config" aria-label="请求类配置">
                <div class="protocol-field req-cfg editable-field method" data-field-name="method" title="点击编辑请求方法">
                    <label><span class="field-label-text">期望请求方法</span><span class="field-edit-hint">编辑</span></label>
                    <div class="value">${escape(reqCfg.method || '')}</div>
                </div>
                <div class="protocol-field req-cfg editable-field path" data-field-name="path" title="点击编辑请求路径">
                    <label><span class="field-label-text">请求路径</span><span class="field-edit-hint">编辑</span></label>
                    <div class="value" title="${escape(requestPath)}">${escape(requestPath)}</div>
                </div>
                <div class="protocol-field request-body editable-field" data-field-name="request-body" title="点击编辑校验请求 Body">
                    <label><span class="field-label-main"><span class="body-indicator ${protocol.req_body_status === 1 ? 'has' : 'no'}"></span><span class="field-label-text">校验请求Body</span></span><span class="field-edit-hint">编辑</span></label>
                    <div class="value">${protocol.req_body_status === 1 ? '已设置' : '未设置'}</div>
                </div>
                <div class="protocol-field http-empty-slot" aria-hidden="true"></div>
            </div>
            <div class="http-details-row http-response-config" aria-label="响应类配置">
                <div class="protocol-field resp-cfg editable-field status" data-field-name="status_code" title="点击编辑响应码">
                    <label><span class="field-label-text">目标响应码</span><span class="field-edit-hint">编辑</span></label>
                    <div class="value">${escape(statusCode)}</div>
                </div>
                <div class="protocol-field resp-cfg http-headers editable-field" data-field-name="headers" data-http-headers-side="response" title="点击编辑响应 Headers">
                    <label><span class="field-label-main"><span class="header-fields-indicator no"></span><span class="field-label-text">响应 Headers</span></span><span class="field-edit-hint">编辑</span></label>
                    <div class="value">未设置</div>
                </div>
                <div class="protocol-field response-body editable-field" data-field-name="response-body" title="点击编辑目标响应 Body">
                    <label><span class="field-label-main"><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has' : 'no'}"></span><span class="field-label-text">目标响应Body</span></span><span class="field-edit-hint">编辑</span></label>
                    <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
                </div>
                <div class="protocol-field http-empty-slot" aria-hidden="true"></div>
            </div>
        `;
        if (KitProxy.httpHeaders) {
            KitProxy.httpHeaders.updateFieldState(grids.querySelector('[data-http-headers-side="response"]'), respCfg.headers);
        }
        return grids;
    }
};

async function getAllPatternFieldsReq(protocolId, req_or_resp) {
    
    const protocolItem = document.getElementById(`protocol-item-${protocolId}`);

    const projectId = readDatasetProjectId(protocolItem.dataset.projectId);


    try{
        return await KitProxy.api.getAllPatternFields(projectId, protocolId, req_or_resp);

    } catch(error) {
        console.error(error.message);
        throw error;
    }


}

async function getPatternInfoReq(projectId) {

    try{
        return await KitProxy.api.getProjectPatternInfo(projectId);

    } catch(error) {
        console.error("获取字段信息失败!");
        throw error;
    }


}

async function getSpecialPatternFields(projectId) {
    try {

        const pattern_info = await getPatternInfoReq(projectId);
        
        console.log('pattern_info: ', pattern_info);
        
        return pattern_info.fields || [];
    } catch (error) {
        console.error('获取特殊字段信息失败!');
        throw error;
    }
}

async function getPatternFields(protocolId, req_or_resp) {

    try {
        const protocolItem = document.getElementById(`protocol-item-${protocolId}`);
        const projectId = readDatasetProjectId(protocolItem.dataset.projectId);
        const [patternInfo, cfgInfo] = await Promise.all([
            KitProxy.api.getProjectPatternInfo(projectId),
            KitProxy.api.getProtocolDetailsCfg(protocolId),
        ]);
        const sideCfg = Number(req_or_resp) === 1
            ? (cfgInfo && cfgInfo.req_cfg) || {}
            : (cfgInfo && cfgInfo.resp_cfg) || {};

        return Object.assign({}, patternInfo, {
            item_value_scope: 'header',
            fields: KitProxy.tcpPatternEditor.patternInfoToHeaderValueFields
                ? KitProxy.tcpPatternEditor.patternInfoToHeaderValueFields(patternInfo, sideCfg)
                : KitProxy.tcpPatternEditor.patternInfoToItemFields(patternInfo, sideCfg),
        });

    } catch (error) {
        console.error('获取所有字段信息失败!');
        throw error;
    }

}

var customTcpProtocolItemGrids = {
    create: function(protocol) {

        const grids = document.createElement('div');
        grids.className = 'details-grid tcp';
        const escape = KitProxy.utils && KitProxy.utils.escapeHTML
            ? KitProxy.utils.escapeHTML
            : function(value) { return String(value == null ? '' : value); };
        const reqCfg = protocol.req_cfg || {};
        const respCfg = protocol.resp_cfg || {};
        const reqTcpCfg = KitProxy.tcpPatternEditor.normalizeTcpItemCfg(reqCfg);
        const respTcpCfg = KitProxy.tcpPatternEditor.normalizeTcpItemCfg(respCfg);
        const reqHeaderValueCount = (reqTcpCfg.function_code ? 1 : 0) + Object.keys(reqTcpCfg.fields || {}).length;
        const respHeaderValueCount = (respTcpCfg.function_code ? 1 : 0) + Object.keys(respTcpCfg.fields || {}).length;
        grids.innerHTML = `
            <div class="protocol-field req-cfg editable-field tcp-header-values" data-field-name="fields" title="点击编辑请求头部字段值">
                <label><span class="field-label-main"><span class="header-fields-indicator ${reqHeaderValueCount ? 'has' : 'no'}"></span><span class="field-label-text">请求头部字段值</span></span><span class="field-edit-hint">编辑</span></label>
                <div class="value" id="${escape(protocol.id)}-header-fields">${reqHeaderValueCount ? `已设置 ${reqHeaderValueCount} 个` : '未设置'}</div>
            </div>

            <div class="protocol-field request-body editable-field" data-field-name="request-body" title="点击编辑校验请求 Body">
                <label><span class="field-label-main"><span class="body-indicator ${protocol.req_body_status === 1 ? 'has' : 'no'}"></span><span class="field-label-text">校验请求Body</span></span><span class="field-edit-hint">编辑</span></label>
                <div class="value">${protocol.req_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>

            <div class="protocol-field resp-cfg editable-field tcp-header-values" data-field-name="fields" title="点击编辑响应头部字段值">
                <label><span class="field-label-main"><span class="header-fields-indicator ${respHeaderValueCount ? 'has' : 'no'}"></span><span class="field-label-text">响应头部字段值</span></span><span class="field-edit-hint">编辑</span></label>
                <div class="value">${respHeaderValueCount ? `已设置 ${respHeaderValueCount} 个` : '未设置'}</div>
            </div>
            <div class="protocol-field response-body editable-field" data-field-name="response-body" title="点击编辑目标响应 Body">
                <label><span class="field-label-main"><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has' : 'no'}"></span><span class="field-label-text">目标响应Body</span></span><span class="field-edit-hint">编辑</span></label>
                <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
        `;
        return grids;
    }
};

/**
 * 生成body编辑框
 * @param {*} body_type 
 * @param {*} body_data 
 */
function createProtocolItemBodyModal(body_type, body_data, handleCb, options = {}) {
    const isRequest = Number(options.side) === 1 || options.side === 'request';
    const shouldHideRequestBodyContent = isRequest && PROTOCOL_ITEM_HIDE_REQUEST_BODY_CONTENT_CONFIG;
    const protocolType = options.protocolType || 'HTTP';
    const allowedTypes = KitProxy.protocolTypes
        ? (
            isRequest && typeof KitProxy.protocolTypes.getRequestBodyTypeOptions === 'function'
                ? KitProxy.protocolTypes.getRequestBodyTypeOptions(protocolType)
                : (
                    !isRequest && typeof KitProxy.protocolTypes.getResponseBodyTypeOptions === 'function'
                        ? KitProxy.protocolTypes.getResponseBodyTypeOptions(protocolType)
                        : (
                            typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function'
                                ? KitProxy.protocolTypes.getBodyTypeOptions(protocolType, isRequest ? 'request' : 'response')
                                : undefined
                        )
                )
        )
        : undefined;

    let currentBody = KitProxy.bodySyntax
        ? KitProxy.bodySyntax.decodeBodyData(body_data)
        : new TextDecoder().decode(body_data || new Uint8Array());

    try {
        if(currentBody
            && KitProxy.bodySyntax
            && !isRequest
            && String(body_type || '').toLowerCase() !== 'binary') {
            currentBody = KitProxy.bodySyntax.format(currentBody, body_type);
        }
    } catch(error) {
        console.error('请求体数据解析出错! ', error.message);
    }
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay edit-body-modal-overlay';
    modal.innerHTML = `
        <div class="edit-body-modal">
            <div class="modal-header">
                <h3>${isRequest ? '编辑校验请求Body' : '编辑目标响应Body'}</h3>
                <button class="close-modal">&times;</button>
            </div>
            <div class="modal-body">
                <div class="form-group">
                    <label>Body内容</label>
                    <div id="body-editor-host"></div>
                </div>
                <div class="form-actions">
                    <button type="button" class="clear-btn body-binary-clear-fields" hidden>清除字段</button>
                    <button type="button" class="cancel-btn">取消</button>
                    <button type="button" class="confirm-btn">确定修改</button>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);

    const bodyDialog = modal.querySelector('.edit-body-modal');
    const clearFieldsButton = modal.querySelector('.body-binary-clear-fields');
    const bodyEditor = KitProxy.bodyEditor.create(modal.querySelector('#body-editor-host'), {
        idPrefix: 'protocol-body',
        value: isRequest && KitProxy.bodySyntax && typeof KitProxy.bodySyntax.normalizeRequestBodyContent === 'function'
            ? KitProxy.bodySyntax.normalizeRequestBodyContent(currentBody, body_type)
            : currentBody,
        bodyType: body_type,
        allowedTypes,
        typeLabel: isRequest ? '期望Body类型' : 'Body类型',
        hideContent: shouldHideRequestBodyContent,
        validate: KitProxy.bodySyntax && typeof KitProxy.bodySyntax.validateRequest === 'function'
            ? KitProxy.bodySyntax.validateRequest
            : undefined,
        placeholder: options.placeholder || '输入 Body 内容...',
        onTypeChange: function(nextType) {
            const isBinaryBody = !shouldHideRequestBodyContent && String(nextType || '').toLowerCase() === 'binary';
            bodyDialog.classList.toggle('is-binary-body-mode', isBinaryBody);
            bodyDialog.classList.toggle('config-pattern-modal', isBinaryBody);
            bodyDialog.classList.toggle('is-item-pattern', isBinaryBody);
            if (clearFieldsButton) clearFieldsButton.hidden = !isBinaryBody;
        },
    });
    const initialBinaryBody = !shouldHideRequestBodyContent && bodyEditor.getType() === 'binary';
    bodyDialog.classList.toggle('is-binary-body-mode', initialBinaryBody);
    bodyDialog.classList.toggle('config-pattern-modal', initialBinaryBody);
    bodyDialog.classList.toggle('is-item-pattern', initialBinaryBody);
    if (clearFieldsButton) clearFieldsButton.hidden = !initialBinaryBody;

    clearFieldsButton?.addEventListener('click', function(event) {
        event.stopPropagation();
        if (typeof bodyEditor.clearBinaryFields === 'function') {
            bodyEditor.clearBinaryFields();
        }
    });
    
    // 处理确定按钮
    modal.querySelector('.confirm-btn').addEventListener('click', async function(e) {
        e.stopPropagation();
        e.preventDefault();

        const confirmButton = this;
        if (confirmButton.disabled) return;

        const validation = bodyEditor.validate();
        if(!validation.valid) {
            KitProxy.utils.showGlobalError(validation.message);
            return;
        }

        const newBodyType = bodyEditor.getType();
        const rawBodyValue = typeof bodyEditor.getValueAsync === 'function'
            ? await bodyEditor.getValueAsync()
            : bodyEditor.getValue();
        const rawBody = newBodyType === 'multiform'
            ? rawBodyValue
            : String(rawBodyValue || '').trim();
        const newBody = KitProxy.bodySyntax && typeof KitProxy.bodySyntax.normalizeBodyContent === 'function'
            ? KitProxy.bodySyntax.normalizeBodyContent(rawBody, newBodyType)
            : (isRequest && KitProxy.bodySyntax && typeof KitProxy.bodySyntax.normalizeRequestBodyContent === 'function'
            ? KitProxy.bodySyntax.normalizeRequestBodyContent(rawBody, newBodyType)
            : rawBody);

        confirmButton.disabled = true;

        try {
            const result = await handleCb(newBodyType, newBody);
            if (result === false) return;

            KitProxy.utils.removeDomNode(modal);
        } finally {
            confirmButton.disabled = false;
        }
    });

    KitProxy.utils.bindModalCloseActions(modal);
}
