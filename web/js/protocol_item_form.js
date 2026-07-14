(function initProtocolItemFormPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const utils = KitProxy.utils || {};
    const escapeHTML = utils.escapeHTML || function(value) {
        return String(value == null ? '' : value);
    };

    const REQ_BODY = 1;
    const RESP_BODY = 2;
    /**
     * 请求侧 Body 内容配置暂时隐藏，待请求校验模块支持完整 Body 配置后恢复为 false。
     * @type {boolean}
     */
    const HIDE_REQUEST_BODY_CONTENT_CONFIG = true;

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
        params.delete('mode');
        if (pageState.projectId > 0) {
            params.set('projectId', String(pageState.projectId));
        }
        const query = params.toString();
        return query ? `protocol_items.html?${query}` : 'protocol_items.html';
    }

    /**
     * @returns {{ projectId: number; protocolId: number | null; mode: string; }}
     */
    function readURLParams() {
        const params = new URLSearchParams(global.location.search);
        const projectId = Number(params.get('projectId'));
        const protocolIdValue = params.get('protocolId');
        const protocolId = Number(protocolIdValue);
        const mode = params.get('mode') === 'reconfig' ? 'reconfig' : '';

        return {
            projectId: Number.isInteger(projectId) && projectId > 0 ? projectId : -1,
            protocolId: Number.isInteger(protocolId) && protocolId > 0 ? protocolId : null,
            mode,
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

    function isProjectRunning(project) {
        return Number(project && project.runtime_state) === 1;
    }

    /**
     * 页面内部统一用 1/2/3 判断协议类型，兼容后端返回 HTTP/TCP/HTTPS 字符串。
     * @param {any} protocolType
     * @returns {number}
     */
    function normalizeProtocolTypeForPage(protocolType) {
        if (typeof protocolType === 'string') {
            const normalized = protocolType.trim().toUpperCase();
            if (normalized === 'HTTP') return ProtocolType.HTTP;
            if (normalized === 'TCP' || normalized === 'CUSTOM_TCP') return ProtocolType.CUSTOM_TCP;
            if (normalized === 'HTTPS') return ProtocolType.HTTPS;
        }

        const value = Number(protocolType);
        return [ProtocolType.HTTP, ProtocolType.CUSTOM_TCP, ProtocolType.HTTPS].includes(value) ? value : 0;
    }

    /**
     * @param {any} project
     * @param {any=} protocol
     * @returns {number}
     */
    function resolveProtocolType(project, protocol) {
        const protocolType = normalizeProtocolTypeForPage(protocol && protocol.type);
        if (protocolType) return protocolType;
        return normalizeProtocolTypeForPage(project && project.protocol_type);
    }

    /**
     * @param {any} project
     */
    function renderProjectContext(project) {
        const context = document.getElementById('protocol-form-project-context');
        if (!context) return;

        const endpointLabel = project.mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口';
        const endpointValue = isProjectRunning(project)
            ? (project.mode === ProjectMode.SERVER ? project.listen_port || '未分配' : project.target_ip || '未设置')
            : '未开启';
        const projectProtocolType = normalizeProtocolTypeForPage(project.protocol_type);
        const protocolText = ProtocolTypeStr[projectProtocolType] || '未知协议';
        const modeText = ProjectModeStr[project.mode] || '未知模式';
        const statusText = isProjectRunning(project) ? '开启' : '未开启';
        const patternHTML = projectProtocolType === ProtocolType.CUSTOM_TCP
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
                        <span class="service-context-status ${isProjectRunning(project) ? 'is-active' : 'is-inactive'}">${escapeHTML(statusText)}</span>
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
            if (pageState.mode === 'reconfig') {
                title.textContent = '重配置协议项';
            } else {
                title.textContent = pageState.mode === 'edit' ? '修改协议项' : '添加协议项';
            }
        }

        if (subtitle) {
            if (pageState.mode === 'reconfig') {
                subtitle.textContent = '重新提交当前协议项的完整配置、请求 Body 和响应 Body';
            } else {
                subtitle.textContent = pageState.mode === 'edit'
                    ? '修改当前协议项配置、请求 Body 和响应 Body'
                    : '新增当前测试服务下的请求校验和响应行为';
            }
        }

        if (backLink) {
            backLink.href = buildProtocolListUrl();
        }

        if (saveButton) {
            saveButton.textContent = '保存';
        }

        updateSaveActionState();
    }

    function updateSaveActionState() {
        const toggleButton = document.getElementById('save-protocol-menu-toggle');
        const menu = document.getElementById('save-protocol-menu');
        const saveAndOnlineButton = document.getElementById('save-and-online-protocol');
        const isCreateMode = pageState.mode === 'create';
        const canSaveAndOnline = isCreateMode && isProjectRunning(pageState.project);

        if (toggleButton) {
            toggleButton.disabled = !isCreateMode;
            toggleButton.title = isCreateMode ? '更多保存选项' : '只有新增协议项支持保存并上线';
            if (!isCreateMode) {
                toggleButton.setAttribute('aria-expanded', 'false');
            }
        }

        if (menu && !isCreateMode) {
            menu.hidden = true;
        }

        if (saveAndOnlineButton) {
            saveAndOnlineButton.disabled = !canSaveAndOnline;
            saveAndOnlineButton.title = canSaveAndOnline
                ? '保存协议项并立即上线'
                : '项目未运行，不能保存并上线';
        }
    }

    /**
     * @returns {number | string}
     */
    function getCurrentProtocolTypeForBody() {
        return pageState.protocolType || (pageState.project && pageState.project.protocol_type) || ProtocolType.HTTP;
    }

    /**
     * @param {'request' | 'response'} tab
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function fallbackBodyTypes(tab) {
        return [
            { value: 'none', label: 'None', enabled: true },
            { value: 'empty', label: 'Empty', enabled: true },
            { value: 'json', label: 'JSON', enabled: true },
            { value: 'xml', label: 'XML', enabled: true },
            { value: 'text', label: 'Text', enabled: true },
            { value: 'image', label: 'Image', enabled: true },
            { value: 'binary', label: 'Binary', enabled: true },
        ];
    }

    /**
     * @param {'request' | 'response'} tab
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function getAllowedBodyTypes(tab) {
        const protocolType = getCurrentProtocolTypeForBody();
        if (KitProxy.protocolTypes) {
            if (tab === 'request' && typeof KitProxy.protocolTypes.getRequestBodyTypeOptions === 'function') {
                return KitProxy.protocolTypes.getRequestBodyTypeOptions(protocolType);
            }
            if (tab === 'response' && typeof KitProxy.protocolTypes.getResponseBodyTypeOptions === 'function') {
                return KitProxy.protocolTypes.getResponseBodyTypeOptions(protocolType);
            }
            if (typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function') {
                return KitProxy.protocolTypes.getBodyTypeOptions(protocolType, tab);
            }
        }
        return fallbackBodyTypes(tab);
    }

    /**
     * @param {string} content
     * @param {string} bodyType
     * @returns {string}
     */
    function normalizeRequestBodyContent(content, bodyType) {
        if (KitProxy.bodySyntax && typeof KitProxy.bodySyntax.normalizeRequestBodyContent === 'function') {
            return KitProxy.bodySyntax.normalizeRequestBodyContent(content, bodyType);
        }
        return ['none', 'empty', 'binary'].includes(String(bodyType || '').toLowerCase())
            ? ''
            : String(content == null ? '' : content);
    }

    /**
     * @param {string} content
     * @param {string} bodyType
     * @returns {string}
     */
    function normalizeBodyContent(content, bodyType) {
        if (KitProxy.bodySyntax && typeof KitProxy.bodySyntax.normalizeBodyContent === 'function') {
            return KitProxy.bodySyntax.normalizeBodyContent(content, bodyType);
        }
        return normalizeRequestBodyContent(content, bodyType);
    }

    /**
     * @param {any} headers
     * @returns {Record<string, string>}
     */
    function normalizeHttpHeaders(headers) {
        return KitProxy.httpHeaders
            ? KitProxy.httpHeaders.normalize(headers)
            : {};
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
        return KitProxy.httpHeaders
            ? KitProxy.httpHeaders.summarize(headers)
            : '未设置';
    }

    /**
     * @param {HTMLElement} button
     * @param {Record<string, string>} headers
     */
    function updateHttpHeaderButtonState(button, headers) {
        if (KitProxy.httpHeaders) {
            KitProxy.httpHeaders.updateButtonState(button, headers);
        }
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
                KitProxy.httpHeaders.openModal(
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

        const normalizedProtocolType = normalizeProtocolTypeForPage(protocolType);
        if (normalizedProtocolType === ProtocolType.HTTP) {
            renderHTTPFields(container, protocol);
            return;
        }

        if (normalizedProtocolType === ProtocolType.CUSTOM_TCP) {
            renderTCPFields(container, protocol);
            return;
        }

        container.innerHTML = '<div class="form-group"><p class="form-note">当前协议类型暂不支持协议项表单。</p></div>';
    }

    function syncActiveBodyFromEditor() {
        if (!pageState.bodyEditor) return;
        const activeTab = pageState.bodyState.activeTab;
        const bodyType = pageState.bodyEditor.getType();
        const content = pageState.bodyEditor.getValue();
        pageState.bodyState[activeTab] = {
            content: normalizeBodyContent(content, bodyType),
            bodyType,
        };
    }

    /**
     * @param {'request' | 'response'} tab
     * @param {string=} bodyType
     */
    function configureBodyEditorForTab(tab, bodyType) {
        if (!pageState.bodyEditor) return;

        const isRequest = tab === 'request';
        pageState.bodyEditor.setAllowedTypes(getAllowedBodyTypes(tab), bodyType || 'json');
        pageState.bodyEditor.setTypeLabel(isRequest ? '期望Body类型' : 'Body类型');
        pageState.bodyEditor.setValidator(KitProxy.bodySyntax && typeof KitProxy.bodySyntax.validateRequest === 'function'
            ? KitProxy.bodySyntax.validateRequest
            : (KitProxy.bodySyntax ? KitProxy.bodySyntax.validate : undefined));
        pageState.bodyEditor.setContentHidden(shouldHideBodyContentForTab(tab));
    }

    /**
     * 判断当前 Body Tab 是否需要暂时隐藏内容输入/配置区。
     * @param {'request' | 'response'} tab Body Tab。
     * @returns {boolean}
     */
    function shouldHideBodyContentForTab(tab) {
        return tab === 'request' && HIDE_REQUEST_BODY_CONTENT_CONFIG;
    }

    /**
     * @param {'request' | 'response'} tab
     */
    function setActiveBodyTab(tab) {
        if (!pageState.bodyEditor || tab === pageState.bodyState.activeTab) return;

        syncActiveBodyFromEditor();
        pageState.bodyState.activeTab = tab;
        const target = pageState.bodyState[tab];
        configureBodyEditorForTab(tab, target.bodyType || 'json');
        pageState.bodyEditor.setValue(target.content || '');
        pageState.bodyEditor.setType(target.bodyType || 'json');

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
            allowedTypes: getAllowedBodyTypes('request'),
            typeLabel: '期望Body类型',
            hideContent: shouldHideBodyContentForTab('request'),
            validate: KitProxy.bodySyntax && typeof KitProxy.bodySyntax.validateRequest === 'function'
                ? KitProxy.bodySyntax.validateRequest
                : undefined,
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
        const validateFn = KitProxy.bodySyntax && typeof KitProxy.bodySyntax.validateRequest === 'function'
            ? KitProxy.bodySyntax.validateRequest
            : KitProxy.bodySyntax.validate;
        const validation = validateFn(body.content, body.bodyType);
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
        const cfg = normalizeProtocolTypeForPage(pageState.protocolType) === ProtocolType.CUSTOM_TCP
            ? collectTCPCfg()
            : collectHTTPCfg();
        const requestBodyType = pageState.bodyState.request.bodyType;
        const requestBody = normalizeBodyContent(pageState.bodyState.request.content.trim(), requestBodyType);
        const responseBodyType = pageState.bodyState.response.bodyType;
        const responseBody = normalizeBodyContent(pageState.bodyState.response.content.trim(), responseBodyType);

        return Object.assign({}, base, cfg, {
            request_body: requestBody,
            response_body: responseBody,
            req_body_type: requestBodyType,
            resp_body_type: responseBodyType,
        });
    }

    /**
     * @param {any} data
     * @param {number=} configState
     * @returns {any}
     */
    function buildAddPayload(data, configState = 0) {
        const isTcp = normalizeProtocolTypeForPage(pageState.protocolType) === ProtocolType.CUSTOM_TCP;
        return {
            cfg_header: {
                name: data.name,
                type: isTcp ? 'TCP' : 'HTTP',
                project_id: pageState.projectId,
                req_body_type: data.req_body_type,
                resp_body_type: data.resp_body_type,
                config_state: [0, 1].includes(Number(configState)) ? Number(configState) : 0,
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
     * @param {number=} configState
     */
    async function handleAdd(data, configState = 0) {
        await KitProxy.api.addProtocol(buildAddPayload(data, configState));
    }

    /**
     * @param {any} data
     */
    async function handleReconfig(data) {
        if (!pageState.protocolId) {
            throw new Error('缺少协议项 ID，无法重配置');
        }

        await KitProxy.api.reconfigProtocol(pageState.protocolId, buildAddPayload(data, 0));
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
     * @returns {string}
     */
    function mutationKeyForSubmit() {
        if (pageState.mode === 'edit') return `protocol-form-edit-${pageState.protocolId}`;
        if (pageState.mode === 'reconfig') return `protocol-form-reconfig-${pageState.protocolId}`;
        return `protocol-form-add-${pageState.projectId}`;
    }

    /**
     * @returns {string}
     */
    function submitMessage() {
        if (pageState.mode === 'edit') return '正在保存协议项...';
        if (pageState.mode === 'reconfig') return '正在重配置协议项...';
        return '正在添加协议项...';
    }

    /**
     * @returns {string}
     */
    function submitSuccessMessage() {
        if (pageState.mode === 'edit') return '协议项修改成功';
        if (pageState.mode === 'reconfig') return '协议项重配置成功';
        return '协议项添加成功';
    }

    /**
     * @param {number=} configState
     */
    async function submitProtocolForm(configState = 0) {
        if (pageState.isSubmitting) return;

        const saveButton = document.getElementById('save-protocol-form');

        pageState.isSubmitting = true;
        await KitProxy.utils.runMutationOnce(
            mutationKeyForSubmit(),
            async function() {
                clearPageError();
                const data = collectFormData();
                if (pageState.mode === 'edit') {
                    await handleEdit(data);
                } else if (pageState.mode === 'reconfig') {
                    await handleReconfig(data);
                } else {
                    await handleAdd(data, configState);
                }
                return true;
            },
            {
                message: submitMessage(),
                successMessage: submitSuccessMessage(),
                button: saveButton,
                busyText: '保存中...',
            },
        ).then(function() {
            setTimeout(navigateBack, 650);
        }).catch(function(error) {
            const prefix = pageState.mode === 'edit'
                ? '修改协议项失败：'
                : (pageState.mode === 'reconfig' ? '重配置协议项失败：' : '添加协议项失败：');
            setInlineError(prefix + error.message);
        }).finally(function() {
            pageState.isSubmitting = false;
        });
    }

    /**
     * @param {Event} event
     */
    async function handleSubmit(event) {
        event.preventDefault();
        await submitProtocolForm(0);
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

        pageState.protocol = await KitProxy.api.getProtocolEditDetail(pageState.protocolId);
        pageState.protocolType = resolveProtocolType(pageState.project, pageState.protocol);
        if (normalizeProtocolTypeForPage(pageState.protocolType) === ProtocolType.HTTP) {
            pageState.initialReqCfg = normalizeHttpReqCfgForSubmit(pageState.protocol.req_cfg || {});
            pageState.initialRespCfg = normalizeHttpRespCfgForSubmit(pageState.protocol.resp_cfg || {});
        } else {
            pageState.initialReqCfg = clone(pageState.protocol.req_cfg || {});
            pageState.initialRespCfg = clone(pageState.protocol.resp_cfg || {});
        }

        pageState.bodyState.request = {
            bodyType: pageState.protocol.req_body_type || 'json',
            content: decodeBodyData(pageState.protocol.request_body || ''),
        };
        pageState.bodyState.response = {
            bodyType: pageState.protocol.resp_body_type || 'json',
            content: decodeBodyData(pageState.protocol.response_body || ''),
        };
        pageState.initialBodyState = {
            request: clone(pageState.bodyState.request),
            response: clone(pageState.bodyState.response),
        };
    }

    function bindFormActions() {
        document.getElementById('protocol-item-form')?.addEventListener('submit', handleSubmit);
        const menuToggle = document.getElementById('save-protocol-menu-toggle');
        const menu = document.getElementById('save-protocol-menu');
        const saveAndOnlineButton = document.getElementById('save-and-online-protocol');

        if (menuToggle && menu) {
            menuToggle.addEventListener('click', function(event) {
                event.preventDefault();
                event.stopPropagation();
                if (menuToggle.disabled) return;

                const nextHidden = !menu.hidden ? true : false;
                menu.hidden = nextHidden;
                menuToggle.setAttribute('aria-expanded', String(!nextHidden));
            });
        }

        if (saveAndOnlineButton) {
            saveAndOnlineButton.addEventListener('click', async function(event) {
                event.preventDefault();
                event.stopPropagation();
                if (saveAndOnlineButton.disabled) {
                    setInlineError('项目未运行，不能保存并上线');
                    return;
                }
                if (menu) menu.hidden = true;
                if (menuToggle) menuToggle.setAttribute('aria-expanded', 'false');
                await submitProtocolForm(1);
            });
        }

        document.addEventListener('click', function(event) {
            if (!menu || menu.hidden) return;
            if (menu.contains(event.target) || menuToggle?.contains(event.target)) return;
            menu.hidden = true;
            if (menuToggle) menuToggle.setAttribute('aria-expanded', 'false');
        });

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
        pageState.mode = pageState.protocolId ? (params.mode === 'reconfig' ? 'reconfig' : 'edit') : 'create';

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
            pageState.protocolType = resolveProtocolType(pageState.project);
            if (normalizeProtocolTypeForPage(pageState.protocolType) === ProtocolType.CUSTOM_TCP) {
                pageState.projectPatternInfo = await KitProxy.api.getProjectPatternInfo(pageState.projectId);
            }

            if (pageState.mode === 'edit' || pageState.mode === 'reconfig') {
                await loadEditProtocol();
            }
            if (normalizeProtocolTypeForPage(pageState.protocolType) === ProtocolType.CUSTOM_TCP && !pageState.projectPatternInfo) {
                pageState.projectPatternInfo = await KitProxy.api.getProjectPatternInfo(pageState.projectId);
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
