(function initProtocolItemFormPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const utils = KitProxy.utils || {};
    const escapeHTML = utils.escapeHTML || function(value) {
        return String(value == null ? '' : value);
    };

    const REQ_BODY = 1;
    const RESP_BODY = 2;

    const pageState = {
        projectId: -1,
        protocolId: null,
        project: null,
        protocol: null,
        mode: 'create',
        protocolType: null,
        bodyState: {
            request: { content: '', bodyType: 'json' },
            response: { content: '', bodyType: 'json' },
            activeTab: 'request',
        },
        initialBodyState: {
            request: { content: '', bodyType: 'json' },
            response: { content: '', bodyType: 'json' },
        },
        initialReqCfg: {},
        initialRespCfg: {},
        reqHttpHeaders: {},
        respHttpHeaders: {},
        projectPatternInfo: null,
        reqPatternFields: null,
        respPatternFields: null,
        bodyEditor: null,
        isSubmitting: false,
    };

    /**
     * 保留当前调试参数并返回协议项列表页。
     * @returns {string}
     */
    function buildProtocolListUrl() {
        const params = new URLSearchParams(global.location.search);
        params.delete('protocolId');
        if (pageState.projectId > 0) {
            params.set('projectId', String(pageState.projectId));
        }
        const query = params.toString();
        return query ? `protocol_items.html?${query}` : 'protocol_items.html';
    }

    /**
     * @returns {{ projectId: number; protocolId: number | null; }}
     */
    function readURLParams() {
        const params = new URLSearchParams(global.location.search);
        const projectId = Number(params.get('projectId'));
        const protocolIdValue = params.get('protocolId');
        const protocolId = Number(protocolIdValue);

        return {
            projectId: Number.isInteger(projectId) && projectId > 0 ? projectId : -1,
            protocolId: Number.isInteger(protocolId) && protocolId > 0 ? protocolId : null,
        };
    }

    /**
     * @param {string} message
     */
    function showPageError(message) {
        if (!utils.showGlobalError) return;

        utils.showGlobalError(message, {
            actionText: '返回协议项列表',
            actionHref: buildProtocolListUrl(),
            durationMs: 0,
        });
    }

    function clearPageError() {
        if (utils.clearGlobalErrorPopup) {
            utils.clearGlobalErrorPopup();
        }
    }

    /**
     * @param {string} message
     */
    function setInlineError(message) {
        if (!message) {
            clearPageError();
            return;
        }

        if (utils.showGlobalError) {
            utils.showGlobalError(message);
        }
    }

    function navigateBack() {
        const targetUrl = buildProtocolListUrl();
        const navigateEvent = new CustomEvent('protocol-item-form:navigate-back', {
            bubbles: true,
            cancelable: true,
            detail: {
                projectId: pageState.projectId,
                url: targetUrl,
            },
        });
        document.dispatchEvent(navigateEvent);
        if (navigateEvent.defaultPrevented) return;
        global.location.href = targetUrl;
    }

    /**
     * @param {Uint8Array | ArrayBuffer | string | null | undefined} bodyData
     * @returns {string}
     */
    function decodeBodyData(bodyData) {
        if (KitProxy.bodySyntax && typeof KitProxy.bodySyntax.decodeBodyData === 'function') {
            return KitProxy.bodySyntax.decodeBodyData(bodyData);
        }

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

    /**
     * @param {any} value
     * @returns {any}
     */
    function clone(value) {
        return JSON.parse(JSON.stringify(value == null ? {} : value));
    }

    function isProjectActive(project) {
        return Number(project && project.active) === 1;
    }

    /**
     * @param {any} project
     */
    function renderProjectContext(project) {
        const context = document.getElementById('protocol-form-project-context');
        if (!context) return;

        const endpointLabel = project.mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口';
        const endpointValue = isProjectActive(project)
            ? (project.mode === ProjectMode.SERVER ? project.listen_port || '未分配' : project.target_ip || '未设置')
            : '未开启';
        const protocolText = ProtocolTypeStr[project.protocol_type] || '未知协议';
        const modeText = ProjectModeStr[project.mode] || '未知模式';
        const statusText = isProjectActive(project) ? '开启' : '未开启';
        const patternHTML = Number(project.protocol_type) === ProtocolType.CUSTOM_TCP
            ? `
                <span class="service-context-item">
                    <span class="meta-label">报文格式</span>
                    <span class="meta-value">${escapeHTML(project.length_policy && globalThis.tcpLengthPolicyText ? globalThis.tcpLengthPolicyText(project.length_policy) : 'TCP格式')}</span>
                </span>
            `
            : '';

        context.innerHTML = `
            <div class="service-context-summary">
                <div class="service-context-title-block">
                    <span class="service-context-kicker">当前测试服务</span>
                    <div class="service-context-title-row">
                        <strong class="service-context-title">${escapeHTML(project.name || '未命名服务')}</strong>
                        <span class="service-context-status ${isProjectActive(project) ? 'is-active' : 'is-inactive'}">${escapeHTML(statusText)}</span>
                    </div>
                </div>
                <span class="service-context-id">ID ${escapeHTML(project.id || '')}</span>
            </div>
            <div class="service-context-grid">
                <span class="service-context-item">
                    <span class="meta-label">协议种类</span>
                    <span class="meta-value">${escapeHTML(protocolText)}</span>
                </span>
                <span class="service-context-item">
                    <span class="meta-label">测试模式</span>
                    <span class="meta-value">${escapeHTML(modeText)}</span>
                </span>
                <span class="service-context-item">
                    <span class="meta-label">${escapeHTML(endpointLabel)}</span>
                    <span class="meta-value">${escapeHTML(endpointValue)}</span>
                </span>
                ${patternHTML}
            </div>
        `;
    }

    function renderPageTitle() {
        const title = document.getElementById('protocol-form-title');
        const subtitle = document.getElementById('protocol-form-subtitle');
        const backLink = document.getElementById('back-protocol-list');
        const saveButton = document.getElementById('save-protocol-form');

        if (title) {
            title.textContent = pageState.mode === 'edit' ? '修改协议项' : '添加协议项';
        }

        if (subtitle) {
            subtitle.textContent = pageState.mode === 'edit'
                ? '修改当前协议项配置、请求 Body 和响应 Body'
                : '新增当前测试服务下的请求校验和响应行为';
        }

        if (backLink) {
            backLink.href = buildProtocolListUrl();
        }

        if (saveButton) {
            saveButton.textContent = pageState.mode === 'edit' ? '保存修改' : '确认添加';
        }
    }

    /**
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function getAllowedBodyTypes() {
        if (KitProxy.protocolTypes && typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function') {
            return KitProxy.protocolTypes.getBodyTypeOptions(pageState.project.protocol_type);
        }
        return [
            { value: 'json', label: 'JSON', enabled: true },
            { value: 'xml', label: 'XML', enabled: true },
            { value: 'text', label: 'Text', enabled: true },
            { value: 'binary', label: 'Binary', enabled: false, reserved: true },
        ];
    }

    const HTTP_HEADER_NAME_RE = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;

    /**
     * @param {any} headers
     * @returns {Record<string, string>}
     */
    function normalizeHttpHeaders(headers) {
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
     * @param {any} reqCfg
     * @returns {{ method: string; path: string; headers: Record<string, string>; }}
     */
    function normalizeHttpReqCfgForSubmit(reqCfg) {
        const cfg = reqCfg && typeof reqCfg === 'object' ? reqCfg : {};
        return {
            method: cfg.method || 'GET',
            path: cfg.path || '/api/',
            headers: normalizeHttpHeaders(cfg.headers),
        };
    }

    /**
     * @param {any} respCfg
     * @returns {{ status_code: string; headers: Record<string, string>; }}
     */
    function normalizeHttpRespCfgForSubmit(respCfg) {
        const cfg = respCfg && typeof respCfg === 'object' ? respCfg : {};
        const statusCode = utils.normalizeHttpStatusCode
            ? utils.normalizeHttpStatusCode(cfg.status_code, 200)
            : (Number(cfg.status_code) || 200);
        return {
            status_code: String(statusCode),
            headers: normalizeHttpHeaders(cfg.headers),
        };
    }

    /**
     * @param {Record<string, string>} headers
     * @returns {string}
     */
    function summarizeHttpHeaders(headers) {
        const count = Object.keys(normalizeHttpHeaders(headers)).length;
        return count > 0 ? `已设置 ${count} 条` : '未设置';
    }

    /**
     * @param {HTMLElement} button
     * @param {Record<string, string>} headers
     */
    function updateHttpHeaderButtonState(button, headers) {
        if (!button) return;
        const normalized = normalizeHttpHeaders(headers);
        const status = button.closest('.form-group')?.querySelector('.import-status');
        button.dataset.headers = JSON.stringify(normalized);
        if (status) {
            status.textContent = summarizeHttpHeaders(normalized);
            status.style.display = 'inline';
        }
    }

    /**
     * @param {{ name?: string; value?: string; }} header
     * @returns {HTMLElement}
     */
    function createHttpHeaderRow(header = {}) {
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
     * @param {Record<string, string>} headers
     * @returns {Array<{ name: string; value: string; }>}
     */
    function httpHeaderEntries(headers) {
        return Object.keys(normalizeHttpHeaders(headers)).map(name => ({
            name,
            value: normalizeHttpHeaders(headers)[name],
        }));
    }

    /**
     * @param {HTMLElement} modal
     * @returns {{ valid: boolean; headers: Record<string, string>; errors: Array<string>; }}
     */
    function collectHttpHeadersFromModal(modal) {
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
     * @param {(headers: Record<string, string>) => void} onSave
     */
    function openHttpHeadersModal(title, headers, onSave) {
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

        const closeModal = utils.bindModalCloseActions
            ? utils.bindModalCloseActions(modal)
            : function() { utils.removeDomNode ? utils.removeDomNode(modal) : modal.remove(); };
        const list = modal.querySelector('.http-headers-list');
        const errorBox = modal.querySelector('.http-headers-error');

        const entries = httpHeaderEntries(headers);
        entries.forEach(entry => {
            list.appendChild(createHttpHeaderRow(entry));
        });
        if (entries.length === 0) {
            list.appendChild(createHttpHeaderRow());
        }

        modal.querySelector('.add-http-header-btn')?.addEventListener('click', function() {
            list.appendChild(createHttpHeaderRow());
        });

        modal.querySelector('.clear-http-headers-btn')?.addEventListener('click', function() {
            list.innerHTML = '';
            list.appendChild(createHttpHeaderRow());
            if (errorBox) errorBox.textContent = '';
        });

        modal.querySelector('.http-headers-form')?.addEventListener('submit', function(event) {
            event.preventDefault();
            event.stopPropagation();

            const result = collectHttpHeadersFromModal(modal);
            if (!result.valid) {
                if (errorBox) {
                    errorBox.innerHTML = result.errors
                        .map(error => `<div>${escapeHTML(error)}</div>`)
                        .join('');
                }
                return;
            }

            onSave(result.headers);
            closeModal();
        });

        return modal;
    }

    /**
     * @param {HTMLElement} container
     */
    function bindHttpHeadersButtons(container) {
        container.querySelectorAll('.http-headers-config-btn').forEach(button => {
            button.addEventListener('click', function(event) {
                event.preventDefault();
                event.stopPropagation();

                const isReq = button.id === 'req-http-headers';
                openHttpHeadersModal(
                    isReq ? '配置请求 Headers' : '配置响应 Headers',
                    isReq ? pageState.reqHttpHeaders : pageState.respHttpHeaders,
                    function(headers) {
                        if (isReq) {
                            pageState.reqHttpHeaders = headers;
                        } else {
                            pageState.respHttpHeaders = headers;
                        }
                        updateHttpHeaderButtonState(button, headers);
                    },
                );
            });
        });
    }

    /**
     * @param {HTMLElement} container
     * @param {any=} protocol
     */
    function renderHTTPFields(container, protocol) {
        const reqCfg = protocol && protocol.req_cfg ? protocol.req_cfg : {};
        const respCfg = protocol && protocol.resp_cfg ? protocol.resp_cfg : {};
        const method = reqCfg.method || 'GET';
        const path = reqCfg.path || '/api/';
        pageState.reqHttpHeaders = normalizeHttpHeaders(reqCfg.headers);
        pageState.respHttpHeaders = normalizeHttpHeaders(respCfg.headers);
        // 老数据可能没有响应码，表单统一按后端当前默认行为显示为 200。
        const statusCode = utils.normalizeHttpStatusCode
            ? utils.normalizeHttpStatusCode(respCfg.status_code, 200)
            : (Number(respCfg.status_code) || 200);
        container.innerHTML = `
            <div class="protocol-config-card protocol-config-card-request">
                <div class="protocol-config-card-title">请求匹配</div>
                <div class="protocol-config-card-fields">
                    <div class="form-group protocol-http-method-group">
                        <label>请求方法</label>
                        <div class="method-radios protocol-method-radios">
                            ${['GET', 'POST', 'PUT', 'DELETE'].map(item => `
                                <label><input type="radio" name="request-method" value="${item}" ${method === item ? 'checked' : ''} required> ${item}</label>
                            `).join('')}
                        </div>
                    </div>
                    <div class="form-group protocol-http-path-group">
                        <label for="request-path">请求路径</label>
                        <input type="text" id="request-path" value="${escapeHTML(path || '/api/')}" placeholder="输入请求路径" required>
                        <div class="path-hint">必须以 / 开头，例如：/api/v1/test</div>
                    </div>
                    <div class="form-group protocol-http-headers-group">
                        <div class="pattern-header">
                            <label>请求 Headers</label>
                            <span class="import-status">${escapeHTML(summarizeHttpHeaders(pageState.reqHttpHeaders))}</span>
                        </div>
                        <button type="button" class="pattern-config-btn http-headers-config-btn" id="req-http-headers">配置请求 Headers</button>
                    </div>
                </div>
            </div>
            <div class="protocol-config-card protocol-config-card-response">
                <div class="protocol-config-card-title">响应行为</div>
                <div class="protocol-config-card-fields">
                    <div class="form-group protocol-http-status-group">
                        <label for="response-status-code">响应码</label>
                        <input type="text" id="response-status-code" inputmode="numeric" pattern="[0-9]{3}" value="${escapeHTML(statusCode)}" placeholder="200" required>
                        <div class="path-hint">填写 100 - 599 的 HTTP 状态码</div>
                    </div>
                    <div class="form-group protocol-http-headers-group">
                        <div class="pattern-header">
                            <label>响应 Headers</label>
                            <span class="import-status">${escapeHTML(summarizeHttpHeaders(pageState.respHttpHeaders))}</span>
                        </div>
                        <button type="button" class="pattern-config-btn http-headers-config-btn" id="resp-http-headers">配置响应 Headers</button>
                    </div>
                </div>
            </div>
        `;
        updateHttpHeaderButtonState(container.querySelector('#req-http-headers'), pageState.reqHttpHeaders);
        updateHttpHeaderButtonState(container.querySelector('#resp-http-headers'), pageState.respHttpHeaders);
        bindHttpHeadersButtons(container);
    }

    /**
     * @param {Array<any>} fields
     * @returns {string}
     */
    function summarizePatternFields(fields) {
        const cfg = buildHeaderValueCfg(fields);
        const count = (cfg.function_code ? 1 : 0) + Object.keys(cfg.fields || {}).length;
        return count > 0 ? `已设置 ${count} 个字段` : '未设置';
    }

    /**
     * @param {Array<any>} fields
     * @returns {{ function_code: string; fields: Record<string, string>; }}
     */
    function buildHeaderValueCfg(fields) {
        if (KitProxy.tcpPatternEditor && typeof KitProxy.tcpPatternEditor.buildTcpHeaderValueCfg === 'function') {
            return KitProxy.tcpPatternEditor.buildTcpHeaderValueCfg(fields || []);
        }

        const cfg = { function_code: '', fields: {} };
        (Array.isArray(fields) ? fields : []).forEach(field => {
            if (!field || !field.value) return;
            if (field.role === 'function_code') {
                cfg.function_code = field.value;
                return;
            }
            if (field.role === 'common') {
                cfg.fields[String(field.byte_pos)] = field.value;
            }
        });
        return cfg;
    }

    /**
     * @param {any} patternInfo
     * @param {any} cfg
     * @returns {Array<any>}
     */
    function toHeaderValueFields(patternInfo, cfg) {
        if (KitProxy.tcpPatternEditor && typeof KitProxy.tcpPatternEditor.patternInfoToHeaderValueFields === 'function') {
            return KitProxy.tcpPatternEditor.patternInfoToHeaderValueFields(patternInfo, cfg);
        }
        return KitProxy.tcpPatternEditor.patternInfoToItemFields(patternInfo, cfg);
    }

    /**
     * @param {HTMLElement} button
     * @param {Array<any>} fields
     */
    function updatePatternButtonState(button, fields) {
        if (!button) return;
        const status = button.closest('.form-group')?.querySelector('.import-status');
        button.dataset.patternInfos = JSON.stringify({ fields: fields || [] });
        if (status) {
            status.textContent = summarizePatternFields(fields);
            status.style.display = 'inline';
        }
    }

    /**
     * @param {HTMLElement} container
     * @param {any=} protocol
     */
    function renderTCPFields(container, protocol) {
        const reqCfg = protocol && protocol.req_cfg ? protocol.req_cfg : {};
        const respCfg = protocol && protocol.resp_cfg ? protocol.resp_cfg : {};
        const reqTcpCfg = KitProxy.tcpPatternEditor.normalizeTcpItemCfg(reqCfg);
        const respTcpCfg = KitProxy.tcpPatternEditor.normalizeTcpItemCfg(respCfg);
        pageState.reqPatternFields = pageState.projectPatternInfo
            ? toHeaderValueFields(pageState.projectPatternInfo, reqTcpCfg)
            : [];
        pageState.respPatternFields = pageState.projectPatternInfo
            ? toHeaderValueFields(pageState.projectPatternInfo, respTcpCfg)
            : [];

        container.innerHTML = `
            <div class="protocol-config-card protocol-config-card-request">
                <div class="protocol-config-card-title">请求侧配置</div>
                <div class="protocol-config-card-fields">
                    <div class="form-group">
                        <div class="pattern-header">
                            <label>校验请求头部字段值</label>
                            <span class="import-status">${escapeHTML(summarizePatternFields(pageState.reqPatternFields))}</span>
                        </div>
                        <button type="button" class="pattern-config-btn" id="req-pattern-infos">配置请求头部字段值</button>
                    </div>
                </div>
            </div>
            <div class="protocol-config-card protocol-config-card-response">
                <div class="protocol-config-card-title">响应侧配置</div>
                <div class="protocol-config-card-fields">
                    <div class="form-group">
                        <div class="pattern-header">
                            <label>目标响应头部字段值</label>
                            <span class="import-status">${escapeHTML(summarizePatternFields(pageState.respPatternFields))}</span>
                        </div>
                        <button type="button" class="pattern-config-btn" id="resp-pattern-infos">配置响应头部字段值</button>
                    </div>
                </div>
            </div>
        `;

        updatePatternButtonState(container.querySelector('#req-pattern-infos'), pageState.reqPatternFields);
        updatePatternButtonState(container.querySelector('#resp-pattern-infos'), pageState.respPatternFields);
        bindTcpPatternButtons(container);
    }

    /**
     * @param {HTMLElement} container
     */
    function bindTcpPatternButtons(container) {
        container.querySelectorAll('.pattern-config-btn').forEach(button => {
            const isReq = button.id === 'req-pattern-infos';
            button.addEventListener('click', async function(event) {
                event.preventDefault();
                event.stopPropagation();

                const loading = showLoading('正在加载 TCP 头部字段值...');
                let patternInfo = null;
                try {
                    patternInfo = pageState.projectPatternInfo || await KitProxy.api.getProjectPatternInfo(pageState.projectId);
                    pageState.projectPatternInfo = patternInfo;
                } catch (error) {
                    hideLoading(loading);
                    setInlineError('获取 TCP 头部字段失败：' + error.message);
                    return;
                }

                await delay(100);
                hideLoading(loading);

                const currentFields = isReq ? pageState.reqPatternFields : pageState.respPatternFields;
                const currentCfg = buildHeaderValueCfg(currentFields || []);
                const patternInfosMap = Object.assign({}, patternInfo, {
                    item_value_scope: 'header',
                    fields: toHeaderValueFields(patternInfo, currentCfg),
                });
                const statusElement = button.closest('.form-group')?.querySelector('.import-status');
                createCustomTcpPatternModal(
                    button,
                    isReq ? '请求头部字段值' : '响应头部字段值',
                    patternInfosMap,
                    statusElement,
                    false,
                    function(patternFieldInfos) {
                        const fields = clone(patternFieldInfos.fields || []);
                        if (isReq) {
                            pageState.reqPatternFields = fields;
                        } else {
                            pageState.respPatternFields = fields;
                        }
                        updatePatternButtonState(button, fields);
                    },
                );
            });
        });
    }

    /**
     * @param {number} protocolType
     * @param {any=} protocol
     */
    function renderTypeSpecificFields(protocolType, protocol) {
        const container = document.getElementById('protocol-type-fields');
        if (!container) return;

        if (Number(protocolType) === ProtocolType.HTTP) {
            renderHTTPFields(container, protocol);
            return;
        }

        if (Number(protocolType) === ProtocolType.CUSTOM_TCP) {
            renderTCPFields(container, protocol);
            return;
        }

        container.innerHTML = '<div class="form-group"><p class="form-note">当前协议类型暂不支持协议项表单。</p></div>';
    }

    function syncActiveBodyFromEditor() {
        if (!pageState.bodyEditor) return;
        const activeTab = pageState.bodyState.activeTab;
        pageState.bodyState[activeTab] = {
            content: pageState.bodyEditor.getValue(),
            bodyType: pageState.bodyEditor.getType(),
        };
    }

    /**
     * @param {'request' | 'response'} tab
     */
    function setActiveBodyTab(tab) {
        if (!pageState.bodyEditor || tab === pageState.bodyState.activeTab) return;

        syncActiveBodyFromEditor();
        pageState.bodyState.activeTab = tab;
        const target = pageState.bodyState[tab];
        pageState.bodyEditor.setType(target.bodyType || 'json');
        pageState.bodyEditor.setValue(target.content || '');

        document.querySelectorAll('.body-switch-btn').forEach(button => {
            button.classList.toggle('is-active', button.dataset.bodyTab === tab);
        });
    }

    function setupBodyToggleButtons() {
        document.querySelectorAll('.body-switch-btn').forEach(button => {
            button.addEventListener('click', function() {
                setActiveBodyTab(button.dataset.bodyTab === 'response' ? 'response' : 'request');
            });
        });
    }

    function setupBodyEditor() {
        const host = document.getElementById('protocol-body-editor-host');
        if (!host) return;

        pageState.bodyEditor = KitProxy.bodyEditor.create(host, {
            idPrefix: 'protocol-form-body',
            value: pageState.bodyState.request.content,
            bodyType: pageState.bodyState.request.bodyType,
            allowedTypes: getAllowedBodyTypes(),
            placeholder: '输入 Body 内容...',
        });
    }

    /**
     * @param {'request' | 'response'} tab
     * @returns {{ valid: boolean; message: string; }}
     */
    function validateBodyTab(tab) {
        const label = tab === 'request' ? '校验请求Body' : '目标响应Body';
        const body = pageState.bodyState[tab];
        const validation = KitProxy.bodySyntax.validate(body.content, body.bodyType);
        if (!validation.valid) {
            return {
                valid: false,
                message: `${label}格式错误：${validation.message}`,
            };
        }
        return { valid: true, message: '' };
    }

    /**
     * @returns {{ name: string; }}
     */
    function collectBaseFields() {
        const name = document.getElementById('protocol-item-name')?.value.trim() || '';
        if (!name) {
            throw new Error('协议项名称不能为空');
        }
        return { name };
    }

    /**
     * @returns {{ req_cfg: any; resp_cfg: any; }}
     */
    function collectHTTPCfg() {
        const method = document.querySelector('input[name="request-method"]:checked')?.value;
        const path = document.getElementById('request-path')?.value.trim() || '';
        const statusCodeValue = document.getElementById('response-status-code')?.value.trim() || '';

        if (!method) {
            throw new Error('请选择请求方法');
        }
        if (!utils.validateHttpPath(path)) {
            throw new Error('请求路径必须以 / 开头');
        }
        if (!utils.validateHttpStatusCode(statusCodeValue)) {
            throw new Error('响应码必须是 100 到 599 的整数');
        }

        return {
            req_cfg: {
                method,
                path,
                headers: clone(pageState.reqHttpHeaders || {}),
            },
            resp_cfg: {
                status_code: statusCodeValue,
                headers: clone(pageState.respHttpHeaders || {}),
            },
        };
    }

    /**
     * @returns {{ req_cfg: any; resp_cfg: any; }}
     */
    function collectTCPCfg() {
        const reqCfg = buildHeaderValueCfg(pageState.reqPatternFields || []);
        const respCfg = buildHeaderValueCfg(pageState.respPatternFields || []);

        const reqValidation = KitProxy.tcpPatternEditor.validateTcpItemCfg(pageState.projectPatternInfo, reqCfg);
        if (!reqValidation.valid) {
            throw new Error('请求侧 TCP 配置错误：' + reqValidation.errors.join('；'));
        }
        const respValidation = KitProxy.tcpPatternEditor.validateTcpItemCfg(pageState.projectPatternInfo, respCfg);
        if (!respValidation.valid) {
            throw new Error('响应侧 TCP 配置错误：' + respValidation.errors.join('；'));
        }

        return {
            req_cfg: reqCfg,
            resp_cfg: respCfg,
        };
    }

    /**
     * @returns {{ name: string; req_cfg: any; resp_cfg: any; request_body: string; response_body: string; req_body_type: string; resp_body_type: string; }}
     */
    function collectFormData() {
        syncActiveBodyFromEditor();

        const requestValidation = validateBodyTab('request');
        if (!requestValidation.valid) {
            throw new Error(requestValidation.message);
        }

        const responseValidation = validateBodyTab('response');
        if (!responseValidation.valid) {
            throw new Error(responseValidation.message);
        }

        const base = collectBaseFields();
        const cfg = Number(pageState.protocolType) === ProtocolType.CUSTOM_TCP
            ? collectTCPCfg()
            : collectHTTPCfg();

        return Object.assign({}, base, cfg, {
            request_body: pageState.bodyState.request.content.trim(),
            response_body: pageState.bodyState.response.content.trim(),
            req_body_type: pageState.bodyState.request.bodyType,
            resp_body_type: pageState.bodyState.response.bodyType,
        });
    }

    /**
     * @param {any} data
     * @returns {any}
     */
    function buildAddPayload(data) {
        const isTcp = Number(pageState.protocolType) === ProtocolType.CUSTOM_TCP;
        return {
            cfg_header: {
                name: data.name,
                type: isTcp ? 'TCP' : 'HTTP',
                project_id: pageState.projectId,
                req_body_type: data.req_body_type,
                resp_body_type: data.resp_body_type,
                ...(isTcp ? { is_endian: 1 } : {}),
            },
            req_cfg: data.req_cfg,
            resp_cfg: data.resp_cfg,
            request_body: data.request_body,
            response_body: data.response_body,
        };
    }

    /**
     * @param {any} left
     * @param {any} right
     * @returns {boolean}
     */
    function isSameJSON(left, right) {
        return JSON.stringify(left || {}) === JSON.stringify(right || {});
    }

    /**
     * @param {any} data
     */
    async function handleAdd(data) {
        await KitProxy.api.addProtocol(buildAddPayload(data));
    }

    /**
     * @param {any} data
     */
    async function handleEdit(data) {
        if (!pageState.protocol) {
            throw new Error('缺少协议项数据，无法保存修改');
        }

        if (data.name !== (pageState.protocol.name || '')) {
            await KitProxy.api.updateProtocolName(pageState.protocolId, data.name);
        }

        if (!isSameJSON(data.req_cfg, pageState.initialReqCfg)) {
            await KitProxy.api.updateProtocolCfg(pageState.protocolId, pageState.projectId, REQ_BODY, data.req_cfg);
        }

        if (!isSameJSON(data.resp_cfg, pageState.initialRespCfg)) {
            await KitProxy.api.updateProtocolCfg(pageState.protocolId, pageState.projectId, RESP_BODY, data.resp_cfg);
        }

        if (
            data.req_body_type !== pageState.initialBodyState.request.bodyType ||
            data.request_body !== pageState.initialBodyState.request.content
        ) {
            await KitProxy.api.updateProtocolBody(pageState.protocolId, pageState.projectId, REQ_BODY, pageState.protocol.type, data.req_body_type, data.request_body);
        }

        if (
            data.resp_body_type !== pageState.initialBodyState.response.bodyType ||
            data.response_body !== pageState.initialBodyState.response.content
        ) {
            await KitProxy.api.updateProtocolBody(pageState.protocolId, pageState.projectId, RESP_BODY, pageState.protocol.type, data.resp_body_type, data.response_body);
        }
    }

    /**
     * @param {Event} event
     */
    async function handleSubmit(event) {
        event.preventDefault();
        if (pageState.isSubmitting) return;

        const saveButton = document.getElementById('save-protocol-form');
        const mutationKey = pageState.mode === 'edit'
            ? `protocol-form-edit-${pageState.protocolId}`
            : `protocol-form-add-${pageState.projectId}`;

        pageState.isSubmitting = true;
        await KitProxy.utils.runMutationOnce(
            mutationKey,
            async function() {
            clearPageError();
            const data = collectFormData();
            if (pageState.mode === 'edit') {
                await handleEdit(data);
            } else {
                await handleAdd(data);
            }
            return true;
            },
            {
                message: pageState.mode === 'edit' ? '正在保存协议项...' : '正在添加协议项...',
                successMessage: pageState.mode === 'edit' ? '协议项修改成功' : '协议项添加成功',
                button: saveButton,
                busyText: pageState.mode === 'edit' ? '保存中...' : '添加中...',
            },
        ).then(function() {
            setTimeout(navigateBack, 650);
        }).catch(function(error) {
            setInlineError((pageState.mode === 'edit' ? '修改协议项失败：' : '添加协议项失败：') + error.message);
        }).finally(function() {
            pageState.isSubmitting = false;
        });
    }

    /**
     * @param {any} protocol
     */
    function applyProtocolToForm(protocol) {
        const nameInput = document.getElementById('protocol-item-name');
        if (nameInput) {
            nameInput.value = protocol.name || '';
        }
        renderTypeSpecificFields(pageState.protocolType, protocol);
    }

    async function loadEditProtocol() {
        if (!pageState.protocolId) return;

        const protocols = await KitProxy.api.getProtocol(pageState.protocolId);
        if (!Array.isArray(protocols) || protocols.length <= 0) {
            throw new Error('获取协议项详情失败');
        }

        pageState.protocol = protocols[0];
        if (Number(pageState.protocolType) === ProtocolType.HTTP) {
            pageState.initialReqCfg = normalizeHttpReqCfgForSubmit(pageState.protocol.req_cfg || {});
            pageState.initialRespCfg = normalizeHttpRespCfgForSubmit(pageState.protocol.resp_cfg || {});
        } else {
            pageState.initialReqCfg = clone(pageState.protocol.req_cfg || {});
            pageState.initialRespCfg = clone(pageState.protocol.resp_cfg || {});
        }

        const [requestBodyInfo, responseBodyInfo] = await Promise.all([
            KitProxy.api.getProtocolBody(pageState.protocolId, REQ_BODY),
            KitProxy.api.getProtocolBody(pageState.protocolId, RESP_BODY),
        ]);

        pageState.bodyState.request = {
            bodyType: requestBodyInfo[0] || pageState.protocol.req_body_type || 'json',
            content: decodeBodyData(requestBodyInfo[1]),
        };
        pageState.bodyState.response = {
            bodyType: responseBodyInfo[0] || pageState.protocol.resp_body_type || 'json',
            content: decodeBodyData(responseBodyInfo[1]),
        };
        pageState.initialBodyState = {
            request: clone(pageState.bodyState.request),
            response: clone(pageState.bodyState.response),
        };
    }

    function bindFormActions() {
        document.getElementById('protocol-item-form')?.addEventListener('submit', handleSubmit);
        document.getElementById('cancel-protocol-form')?.addEventListener('click', function() {
            navigateBack();
        });
    }

    async function initPage() {
        try {
            await KitProxy.auth.requireCurrentUser();
        } catch (error) {
            if (Number(error && error.status) !== 401) {
                showPageError(error && error.message ? error.message : '登录态校验失败');
            }
            return;
        }

        const params = readURLParams();
        pageState.projectId = params.projectId;
        pageState.protocolId = params.protocolId;
        pageState.mode = pageState.protocolId ? 'edit' : 'create';

        renderPageTitle();

        if (pageState.projectId < 0) {
            showPageError('缺少或非法的测试服务 ID，无法加载协议项表单。');
            return;
        }

        const loading = showLoading('正在加载协议项表单...');
        try {
            const projects = await KitProxy.api.getProject(pageState.projectId);
            if (!Array.isArray(projects) || projects.length <= 0) {
                throw new Error('获取测试服务信息失败');
            }

            pageState.project = projects[0];
            if (Number(pageState.project && pageState.project.status) === 0) {
                throw new Error('已删除的测试服务不能编辑协议项');
            }
            pageState.protocolType = pageState.project.protocol_type;
            if (Number(pageState.protocolType) === ProtocolType.CUSTOM_TCP) {
                pageState.projectPatternInfo = await KitProxy.api.getProjectPatternInfo(pageState.projectId);
            }

            if (pageState.mode === 'edit') {
                await loadEditProtocol();
            }

            renderPageTitle();
            renderProjectContext(pageState.project);
            const nameInput = document.getElementById('protocol-item-name');
            if (nameInput && pageState.mode === 'create') {
                nameInput.value = '';
            }
            if (pageState.protocol) {
                applyProtocolToForm(pageState.protocol);
            } else {
                renderTypeSpecificFields(pageState.protocolType, null);
            }
            setupBodyEditor();
            setupBodyToggleButtons();
            bindFormActions();
        } catch (error) {
            showPageError(error.message);
        } finally {
            hideLoading(loading);
        }
    }

    KitProxy.protocolItemForm = {
        pageState,
        buildProtocolListUrl,
        readURLParams,
        collectFormData,
        buildAddPayload,
        setActiveBodyTab,
        navigateBack,
        initPage,
    };

    if (!KitProxy.__disableAutoInitProtocolItemForm) {
        document.addEventListener('DOMContentLoaded', initPage);
    }
})(typeof window !== 'undefined' ? window : globalThis);
