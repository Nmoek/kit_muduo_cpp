(function initKitProxyUtils(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    // 通用工具集中在这里，避免 main.js、modal、protocol_item 继续复制实现。
    function delay(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    /**
     * 显示全局加载遮罩。
     * @param {string} message 加载提示文案。
     * @returns {HTMLDivElement} 可传给 hideLoading 的遮罩节点。
     */
    function showLoading(message = '处理中，请稍候...') {
        const loadingOverlay = document.createElement('div');
        loadingOverlay.className = 'loading-overlay';
        loadingOverlay.innerHTML = `
            <div class="loading-content" role="status" aria-live="polite" aria-label="${escapeHTML(message)}">
                <div class="loading-spinner" aria-hidden="true"></div>
                <p class="loading-message">${escapeHTML(message)}</p>
            </div>
        `;
        document.body.appendChild(loadingOverlay);
        return loadingOverlay;
    }

    /**
     * 隐藏 showLoading 创建的全局加载遮罩。
     * @param {HTMLElement | null | undefined} loadingOverlay 待移除的遮罩节点。
     */
    function hideLoading(loadingOverlay) {
        if (loadingOverlay && document.body.contains(loadingOverlay)) {
            document.body.removeChild(loadingOverlay);
        }
    }

    let activeGlobalNotificationPopup = null;
    let activeGlobalNotificationTimer = null;

    function clearGlobalNotificationPopup() {
        if (activeGlobalNotificationTimer) {
            clearTimeout(activeGlobalNotificationTimer);
            activeGlobalNotificationTimer = null;
        }

        const popup = activeGlobalNotificationPopup;
        activeGlobalNotificationPopup = null;
        if (!popup || !popup.parentNode) return;

        popup.classList.remove('is-visible');
        setTimeout(function() {
            removeDomNode(popup);
        }, 180);
    }

    /**
     * @param {'success' | 'error'} type
     * @returns {string}
     */
    function notificationIconHTML(type) {
        const iconPath = type === 'success'
            ? '<path d="M9 12l2 2 4-4"></path>'
            : '<path d="m15 9-6 6"></path><path d="m9 9 6 6"></path>';

        return `
            <span class="global-notification-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round" focusable="false">
                    <circle cx="12" cy="12" r="9"></circle>
                    ${iconPath}
                </svg>
            </span>
        `;
    }

    /**
     * 显示全局顶部通知弹框，成功和错误共用同一套位置、动效和布局。
     * @param {string} message 提示文案。
     * @param {{type?: 'success' | 'error', durationMs?: number, actionText?: string, actionHref?: string}=} options 展示选项。
     * @returns {HTMLElement | null}
     */
    function showGlobalNotification(message, options = {}) {
        const type = options.type === 'success' ? 'success' : 'error';
        const fallbackMessage = type === 'success' ? '操作成功' : '操作失败，请稍后重试';
        const notificationMessage = String(message || fallbackMessage).trim() || fallbackMessage;
        if (typeof document === 'undefined' || !document.body) {
            return null;
        }

        clearGlobalNotificationPopup();

        const popup = document.createElement('div');
        popup.className = `global-error-popup global-notification-popup is-${type}`;
        popup.setAttribute('role', type === 'error' ? 'alert' : 'status');
        popup.setAttribute('aria-live', type === 'error' ? 'assertive' : 'polite');

        const actionHTML = options.actionHref
            ? `<a class="global-error-action" href="${escapeHTML(options.actionHref)}">${escapeHTML(options.actionText || '查看')}</a>`
            : '';
        popup.innerHTML = `
            ${notificationIconHTML(type)}
            <div class="global-error-content">
                <p>${escapeHTML(notificationMessage)}</p>
                ${actionHTML}
            </div>
            <button type="button" class="global-error-close" aria-label="关闭错误提示">&times;</button>
        `;

        popup.querySelector('.global-error-close')?.addEventListener('click', clearGlobalNotificationPopup);
        document.body.appendChild(popup);
        activeGlobalNotificationPopup = popup;

        const show = function() {
            popup.classList.add('is-visible');
        };
        if (typeof global.requestAnimationFrame === 'function') {
            global.requestAnimationFrame(show);
        } else {
            setTimeout(show, 0);
        }

        const durationMs = options.durationMs == null ? 4800 : Number(options.durationMs);
        if (durationMs > 0) {
            activeGlobalNotificationTimer = setTimeout(clearGlobalNotificationPopup, durationMs);
        }

        return popup;
    }

    /**
     * 显示全局错误弹框，统一替代浏览器 alert 和页面内嵌错误区。
     * @param {string} message 错误提示文案。
     * @param {{durationMs?: number, actionText?: string, actionHref?: string}=} options 展示选项。
     * @returns {HTMLElement | null}
     */
    function showGlobalError(message, options = {}) {
        return showGlobalNotification(message, Object.assign({}, options, { type: 'error' }));
    }

    /**
     * 显示全局成功弹框。
     * @param {string} message 成功提示文案。
     * @param {{durationMs?: number, actionText?: string, actionHref?: string}=} options 展示选项。
     * @returns {HTMLElement | null}
     */
    function showGlobalSuccess(message, options = {}) {
        return showGlobalNotification(message, Object.assign({}, options, { type: 'success' }));
    }

    const pendingMutations = new Map();
    let activeMutationDepth = 0;

    function getMutationButtons(options) {
        const buttons = [];
        if (options.button) buttons.push(options.button);
        if (Array.isArray(options.buttons)) {
            options.buttons.forEach(button => {
                if (button) buttons.push(button);
            });
        }
        return buttons;
    }

    function setMutationButtonsBusy(buttons, busy, busyText) {
        buttons.forEach(button => {
            if (!button) return;

            if (busy) {
                button.dataset.mutationWasDisabled = button.disabled ? '1' : '0';
                if (busyText) {
                    button.dataset.mutationOriginalText = button.textContent;
                    button.textContent = busyText;
                }
                button.disabled = true;
                button.classList.add('is-mutation-busy');
                button.setAttribute('aria-busy', 'true');
            } else {
                button.disabled = button.dataset.mutationWasDisabled === '1';
                if (Object.prototype.hasOwnProperty.call(button.dataset, 'mutationOriginalText')) {
                    button.textContent = button.dataset.mutationOriginalText;
                    delete button.dataset.mutationOriginalText;
                }
                delete button.dataset.mutationWasDisabled;
                button.classList.remove('is-mutation-busy');
                button.removeAttribute('aria-busy');
            }
        });
    }

    function isJsdomRuntime() {
        return typeof navigator !== 'undefined'
            && /jsdom/i.test(String(navigator.userAgent || ''));
    }

    /**
     * 执行修改类操作，同一个 key 在完成前只会真正执行一次。
     * @param {string} key 修改操作唯一标识，例如 delete-project-12。
     * @param {Function} action 返回 Promise 的实际修改操作。
     * @param {{message?: string, button?: HTMLElement, buttons?: HTMLElement[], busyText?: string, loading?: boolean, minDurationMs?: number}=} options
     * @returns {Promise<any>} 当前修改操作的 Promise；重复触发时复用正在执行的 Promise。
     */
    async function runMutationOnce(key, action, options = {}) {
        const mutationKey = String(key || 'default');
        if (pendingMutations.has(mutationKey)) {
            return pendingMutations.get(mutationKey);
        }

        if (typeof action !== 'function') {
            throw new Error('runMutationOnce 需要传入 action 函数');
        }

        const mutationPromise = (async function executeMutation() {
            const buttons = getMutationButtons(options);
            const shouldShowLoading = options.loading !== false && activeMutationDepth === 0;
            const isMockMode = typeof KitProxy.config === 'object' && KitProxy.config && KitProxy.config.apiMode === 'mock';
            const mockVisibleDelayMs = Number(options.mockVisibleDelayMs || 0);
            const shouldApplyMockVisibleDelay = isMockMode && mockVisibleDelayMs > 0 && !isJsdomRuntime();
            const loading = !shouldShowLoading
                ? null
                : showLoading(options.message || '正在处理，请稍候...');
            const startedAt = Date.now();

            setMutationButtonsBusy(buttons, true, options.busyText);
            activeMutationDepth += 1;

            try {
                // Mock 数据读写是同步完成的；调试延迟放在 action 前，才能让界面和 mock 状态都真实停留在“处理中”。
                if (shouldApplyMockVisibleDelay) {
                    await delay(mockVisibleDelayMs);
                }

                const result = await action();
                if (options.successMessage) {
                    showGlobalSuccess(options.successMessage);
                }
                return result;
            } finally {
                const minDurationMs = Math.max(0, Number(options.minDurationMs || 0));
                const remainMs = minDurationMs - (Date.now() - startedAt);
                if (remainMs > 0) {
                    await delay(remainMs);
                }

                hideLoading(loading);
                setMutationButtonsBusy(buttons, false);
                activeMutationDepth = Math.max(0, activeMutationDepth - 1);
                pendingMutations.delete(mutationKey);
            }
        })();

        pendingMutations.set(mutationKey, mutationPromise);
        return mutationPromise;
    }

    function isMutationPending(key) {
        return pendingMutations.has(String(key || 'default'));
    }

    function removeDomNode(node) {
        if (node && node.parentNode) {
            node.parentNode.removeChild(node);
        }
    }

    function escapeHTML(value) {
        return String(value == null ? '' : value)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    function bindModalCloseActions(modal, options = {}) {
        // 动态 innerHTML 模态框都遵守 close-modal/cancel-btn 约定，统一关闭逻辑能减少重复事件代码。
        if (!modal) return function noop() {};

        const closeSelector = options.closeSelector || '.close-modal';
        const cancelSelector = options.cancelSelector || '.cancel-btn';
        const onBeforeClose = typeof options.onBeforeClose === 'function' ? options.onBeforeClose : null;

        function closeModal() {
            if (onBeforeClose) {
                onBeforeClose();
            }
            removeDomNode(modal);
        }

        const closeBtn = modal.querySelector(closeSelector);
        const cancelBtn = modal.querySelector(cancelSelector);

        if (closeBtn) {
            closeBtn.addEventListener('click', closeModal);
        }
        if (cancelBtn) {
            cancelBtn.addEventListener('click', closeModal);
        }

        return closeModal;
    }

    /**
     * 创建文本或 Body 编辑器导入弹窗。
     * @param {{
     *   title?: string;
     *   placeholder?: string;
     *   value?: string;
     *   bodyType?: string;
     *   allowedTypes?: Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>;
     *   typeLabel?: string;
     *   hideContent?: boolean;
     *   validate?: Function;
     *   useBodyEditor?: boolean;
     *   modalClassName?: string;
     *   idPrefix?: string;
     *   onConfirm?: Function;
     * }} options
     */
    function createTextImportModal(options) {
        // 用于 Body 导入等“textarea + 确定/取消”的轻量弹窗，业务状态由 onConfirm 回调处理。
        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        const useBodyEditor = options.useBodyEditor && KitProxy.bodyEditor;
        modal.innerHTML = `
            <div class="${options.modalClassName || 'add-protocol-item-modal import-modal'}">
                <div class="modal-header">
                    <h3>${escapeHTML(options.title || '导入内容')}</h3>
                    <button class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    ${useBodyEditor
                        ? '<div class="body-editor-host"></div>'
                        : `<textarea placeholder="${escapeHTML(options.placeholder || '请在此输入内容...')}"></textarea>`}
                    <div class="form-actions">
                        <button type="button" class="cancel-btn">取消</button>
                        <button type="button" class="confirm-btn">确定</button>
                    </div>
                </div>
            </div>
        `;

        let bodyEditor = null;
        const textarea = modal.querySelector('textarea');
        if (useBodyEditor) {
            bodyEditor = KitProxy.bodyEditor.create(modal.querySelector('.body-editor-host'), {
                idPrefix: options.idPrefix || 'body-import',
                value: options.value || '',
                bodyType: options.bodyType || 'json',
                allowedTypes: options.allowedTypes,
                typeLabel: options.typeLabel,
                hideContent: options.hideContent,
                validate: options.validate,
                placeholder: options.placeholder || '请在此输入内容...',
            });
        } else if (textarea) {
            textarea.value = options.value || '';
        }
        document.body.appendChild(modal);

        const closeModal = bindModalCloseActions(modal);
        modal.querySelector('.confirm-btn').addEventListener('click', async function() {
            if (bodyEditor) {
                const validation = bodyEditor.validate();
                if (!validation.valid) {
                    showGlobalError(validation.message);
                    return;
                }
            }

            if (typeof options.onConfirm === 'function') {
                options.onConfirm(
                    bodyEditor
                        ? (typeof bodyEditor.getValueAsync === 'function' ? await bodyEditor.getValueAsync() : bodyEditor.getValue())
                        : textarea.value,
                    bodyEditor ? bodyEditor.getType() : options.bodyType,
                );
            }
            closeModal();
        });

        return modal;
    }

    let activeInlineTitleEditor = null;
    let inlineTitleDocumentBound = false;

    function ensureInlineTitleDocumentListener() {
        if (inlineTitleDocumentBound || typeof document === 'undefined') return;

        document.addEventListener('mousedown', function(event) {
            if (!activeInlineTitleEditor || !activeInlineTitleEditor.isEditing()) return;
            if (activeInlineTitleEditor.contains(event.target)) return;
            activeInlineTitleEditor.cancel();
        });

        inlineTitleDocumentBound = true;
    }

    function bindInlineTitleEditor(options) {
        if (!options || !options.titleElement) {
            throw new Error('行内标题编辑器缺少 titleElement');
        }

        const titleElement = options.titleElement;
        const maxLength = Number(options.maxLength || 64);
        const emptyMessage = options.emptyMessage || '名称不能为空';
        const onSave = typeof options.onSave === 'function'
            ? options.onSave
            : function noopSave() { return true; };

        let controlsContainer = options.controlsContainer || null;
        if (!controlsContainer) {
            controlsContainer = document.createElement('span');
            controlsContainer.className = 'inline-title-controls';
            titleElement.insertAdjacentElement('afterend', controlsContainer);
        }

        controlsContainer.classList.add('inline-title-controls');
        controlsContainer.style.display = 'none';
        controlsContainer.innerHTML = `
            <button type="button" class="inline-title-save">保存</button>
            <button type="button" class="inline-title-cancel">取消</button>
            <span class="inline-title-error" aria-live="polite"></span>
        `;

        const saveButton = controlsContainer.querySelector('.inline-title-save');
        const cancelButton = controlsContainer.querySelector('.inline-title-cancel');
        const errorElement = controlsContainer.querySelector('.inline-title-error');

        let editing = false;
        let saving = false;
        let originalTitle = '';
        let inputElement = null;

        function getDisplayTitle() {
            return String(titleElement.dataset.titleValue || titleElement.textContent || '').trim();
        }

        function setTitleText(value) {
            const normalizedValue = String(value == null ? '' : value);
            titleElement.textContent = normalizedValue;
            titleElement.dataset.titleValue = normalizedValue;
        }

        function setBusy(isBusy) {
            saving = Boolean(isBusy);
            if (saveButton) saveButton.disabled = saving;
            if (cancelButton) cancelButton.disabled = saving;
            if (inputElement) inputElement.disabled = saving;
        }

        function showError(message) {
            if (!errorElement) return;
            errorElement.textContent = message || '';
            controlsContainer.classList.toggle('has-error', Boolean(message));
            titleElement.classList.toggle('has-title-error', Boolean(message));
        }

        function cleanupInput(text) {
            titleElement.classList.remove('is-editing');
            titleElement.removeAttribute('aria-busy');
            setTitleText(text);
            controlsContainer.style.display = 'none';
            controlsContainer.classList.remove('is-message-only');
            showError('');
            inputElement = null;
            editing = false;
            saving = false;
            if (activeInlineTitleEditor === editorApi) {
                activeInlineTitleEditor = null;
            }
        }

        function showFailureMessage(message) {
            controlsContainer.style.display = 'inline-flex';
            controlsContainer.classList.add('is-message-only');
            showError(message || '保存失败');
        }

        function enterEditMode(event) {
            if (event) {
                event.preventDefault();
                event.stopPropagation();
            }

            if (saving) return;

            if (activeInlineTitleEditor && activeInlineTitleEditor !== editorApi && activeInlineTitleEditor.isEditing()) {
                activeInlineTitleEditor.cancel();
            }

            if (editing) {
                if (inputElement) inputElement.focus();
                return;
            }

            ensureInlineTitleDocumentListener();
            originalTitle = getDisplayTitle();
            editing = true;
            activeInlineTitleEditor = editorApi;

            titleElement.textContent = '';
            titleElement.classList.add('is-editing');
            titleElement.setAttribute('aria-busy', 'false');
            controlsContainer.classList.remove('is-message-only');

            inputElement = document.createElement('input');
            inputElement.type = 'text';
            inputElement.className = 'inline-title-input';
            inputElement.value = originalTitle;
            inputElement.maxLength = maxLength;
            inputElement.setAttribute('aria-label', options.ariaLabel || '编辑名称');
            titleElement.appendChild(inputElement);
            controlsContainer.style.display = 'inline-flex';
            showError('');

            inputElement.addEventListener('click', function(inputEvent) {
                inputEvent.stopPropagation();
            });

            inputElement.addEventListener('keydown', function(keyEvent) {
                if (keyEvent.key === 'Enter') {
                    keyEvent.preventDefault();
                    editorApi.save();
                } else if (keyEvent.key === 'Escape') {
                    keyEvent.preventDefault();
                    editorApi.cancel();
                }
            });

            inputElement.addEventListener('input', function() {
                showError('');
            });

            inputElement.focus();
            inputElement.select();
        }

        const editorApi = {
            isEditing: function() {
                return editing;
            },
            contains: function(target) {
                return titleElement.contains(target) || controlsContainer.contains(target);
            },
            cancel: function() {
                if (!editing || saving) return;
                cleanupInput(originalTitle);
            },
            save: async function() {
                if (!editing || saving || !inputElement) return false;

                const nextTitle = inputElement.value.trim();
                if (!nextTitle) {
                    showError(emptyMessage);
                    inputElement.focus();
                    return false;
                }

                if (nextTitle === originalTitle) {
                    cleanupInput(originalTitle);
                    return true;
                }

                try {
                    setBusy(true);
                    titleElement.setAttribute('aria-busy', 'true');
                    const result = await onSave(nextTitle);
                    if (result === false) {
                        cleanupInput(originalTitle);
                        showFailureMessage('保存失败');
                        return false;
                    }
                    cleanupInput(nextTitle);
                    return true;
                } catch (error) {
                    cleanupInput(originalTitle);
                    showFailureMessage(error && error.message ? error.message : '保存失败');
                    return false;
                } finally {
                    setBusy(false);
                }
            },
        };

        titleElement.classList.add('editable');
        titleElement.dataset.titleValue = getDisplayTitle();
        titleElement.addEventListener('click', enterEditMode);

        if (saveButton) {
            saveButton.addEventListener('click', function(event) {
                event.preventDefault();
                event.stopPropagation();
                editorApi.save();
            });
        }

        if (cancelButton) {
            cancelButton.addEventListener('click', function(event) {
                event.preventDefault();
                event.stopPropagation();
                editorApi.cancel();
            });
        }

        return editorApi;
    }

    function ExtractId(idStr) {
        // 依赖 service-card-<id>、protocol-item-<id> 这类 DOM id 约定。
        if (typeof idStr !== 'string') return -1;
        const pos = idStr.lastIndexOf('-');
        if (pos < 0 || pos === idStr.length - 1) return -1;

        const id = Number(idStr.slice(pos + 1));
        return Number.isInteger(id) ? id : -1;
    }

    function checkEmptyState() {
        // 空状态节点本身不能计入服务卡片数量，否则删除最后一项后不会显示提示。
        const serviceCards = document.querySelector('.service-cards');
        if (!serviceCards) return;

        const existingEmptyState = serviceCards.querySelector('.empty-state');
        const realCardCount = Array.from(serviceCards.children)
            .filter(child => !child.classList.contains('empty-state'))
            .length;

        if (realCardCount === 0) {
            if (!existingEmptyState) {
                const emptyState = document.createElement('div');
                emptyState.className = 'empty-state';
                emptyState.innerHTML = `
                    <div class="empty-message">
                        <p>暂无测试服务，点击按钮添加</p>
                    </div>
                `;
                serviceCards.appendChild(emptyState);
            }
        } else if (existingEmptyState) {
            existingEmptyState.remove();
        }
    }

    async function loadModal(path) {
        try {
            const response = await fetch(path);
            if (!response.ok) {
                throw new Error('模板加载失败');
            }
            return await response.text();
        } catch (error) {
            console.error('加载模板失败:', error);
            return null;
        }
    }

    function validateHttpPath(path) {
        return typeof path === 'string' && path.trim().startsWith('/');
    }

    /**
     * @param {number | string} statusCode
     * @returns {boolean}
     */
    function validateHttpStatusCode(statusCode) {
        const code = Number(statusCode);
        return Number.isInteger(code) && code >= 100 && code <= 599;
    }

    /**
     * @param {number | string | null | undefined} statusCode
     * @param {number=} fallback
     * @returns {number}
     */
    function normalizeHttpStatusCode(statusCode, fallback = 200) {
        return validateHttpStatusCode(statusCode) ? Number(statusCode) : fallback;
    }

    function validatePort(port) {
        const portNumber = Number(port);
        return Number.isInteger(portNumber) && portNumber >= 1 && portNumber <= 65535;
    }

    function validateIpPort(value) {
        const ipPortRegex = /^((25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(25[0-5]|2[0-4]\d|[01]?\d\d?):([1-9]\d{0,3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5])$/;
        return typeof value === 'string' && ipPortRegex.test(value);
    }

    function validateJsonText(text) {
        // 空 Body 表示“不设置内容”，不是 JSON 错误。
        if (!text) return true;

        try {
            JSON.parse(text);
            return true;
        } catch (error) {
            return false;
        }
    }

    function formatJsonText(text) {
        if (!text) return '';
        return JSON.stringify(JSON.parse(text), null, 2);
    }

    function formatXMLHelper(xmlString) {
        const parser = new DOMParser();
        const xmlDoc = parser.parseFromString(xmlString, 'application/xml');
        const parserError = xmlDoc.querySelector('parsererror');

        if (parserError) {
            throw new Error('XML解析错误: ' + parserError.textContent);
        }

        function formatNode(node, indentLevel) {
            const indent = ' '.repeat(indentLevel * 4);
            let output = '';

            if (node.nodeType === Node.ELEMENT_NODE) {
                output += `${indent}<${node.tagName}`;

                for (let i = 0; i < node.attributes.length; i++) {
                    const attr = node.attributes[i];
                    output += ` ${attr.name}="${attr.value}"`;
                }

                const childNodes = node.childNodes;
                let hasElementChildren = false;
                let textContent = '';

                for (let i = 0; i < childNodes.length; i++) {
                    const child = childNodes[i];
                    if (child.nodeType === Node.ELEMENT_NODE) {
                        hasElementChildren = true;
                    } else if (child.nodeType === Node.TEXT_NODE && child.textContent.trim()) {
                        textContent += child.textContent.trim();
                    }
                }

                if (hasElementChildren || textContent) {
                    output += '>\n';

                    for (let i = 0; i < childNodes.length; i++) {
                        const child = childNodes[i];
                        if (child.nodeType === Node.ELEMENT_NODE ||
                            (child.nodeType === Node.TEXT_NODE && child.textContent.trim())) {
                            output += formatNode(child, indentLevel + 1);
                        }
                    }

                    output += `${indent}</${node.tagName}>\n`;
                } else {
                    output += ' />\n';
                }
            } else if (node.nodeType === Node.TEXT_NODE) {
                const text = node.textContent.trim();
                if (text) {
                    output += `${indent}${text}\n`;
                }
            } else if (node.nodeType === Node.COMMENT_NODE) {
                output += `${indent}<!--${node.data}-->\n`;
            } else if (node.nodeType === Node.DOCUMENT_TYPE_NODE) {
                output += `${indent}<!DOCTYPE ${node.name}>\n`;
            } else if (node.nodeType === Node.PROCESSING_INSTRUCTION_NODE) {
                output += `${indent}<?${node.target} ${node.data}?>\n`;
            }

            return output;
        }

        return formatNode(xmlDoc.documentElement, 0);
    }

    function appendProtocolBodyData(formData, body, bodyType, bodyData) {
        // 新增协议接口要求 req/resp body 的 multipart name 即使内容为空也必须出现。
        const bodyKey = body === 1 ? 'protocol_req_body' : 'protocol_resp_body';
        if (bodyData == null ||
            String(bodyData).length <= 0) {
            formData.append(bodyKey, '');
            return;
        }

        // 后端新增协议接口要求 req/resp body 使用不同字段名，不能合并成同一个 key。
        let bodyValue;

        if (bodyType.includes('json')) {
            bodyValue = new Blob(
                [bodyData],
                { type: 'application/json' }
            );
        } else if (bodyType.includes('xml')) {
            // 创建 XML 文档
            const parser = new DOMParser();
            const xmlDoc = parser.parseFromString(
                bodyData,
                'text/xml'
            );

            // 序列化为字符串
            const serializer = new XMLSerializer();
            const xmlString = serializer.serializeToString(xmlDoc);
            bodyValue = new Blob(
                [xmlString],
                { type: 'application/xml' }
            );
        } else if (bodyType.includes('multiform')) {
            // Multiform body 在存储层是字段描述 JSON，由运行时编码为真正的 multipart 报文。
            bodyValue = new Blob(
                [bodyData],
                { type: 'application/json' }
            );
        } else if (bodyType.includes('binary')) {
            bodyValue = new Blob(
                [bodyData],
                { type: 'application/octet-stream'}
            );
        } else {
            bodyValue = new Blob(
                [bodyData],
                { type: 'text/plain'}
            );
        }

        formData.append(bodyKey, bodyValue, 'body.dat');
    }

    function createProtocolBodyFormData(protocolId, projectId, reqOrResp, protocolType, bodyType, body) {
        // /protocols/:protocol_id/details/body 接口是 multipart：header 走 JSON 字符串，body 走文件字段。
        const formData = new FormData();
        
        formData.append('detail_header', new Blob(
            [JSON.stringify({
                side: reqOrResp,
                body_type: bodyType,
            })],
            { type: "application/json"},
        ), 'detail_header.json');

        if (body && body.length > 0) {
            const binaryBlob = new Blob([body], { type: 'application/octet-stream' });

            formData.append('detail_cfg_data', binaryBlob, reqOrResp === 1 ? 'req_body.dat' : 'resp_body.dat');
        }
        else {
            formData.append('detail_cfg_data', '');
        }

        return formData;
    }

    function createAddProtocolFormData(protocol) {
        // /protocols/add 的三个配置字段名是后端约定，重构时不能改名。
        const formData = new FormData();

        // JSON 数据（指定为 application/json）
        let configBlob = new Blob(
            [JSON.stringify(protocol.cfg_header)],
            { type: 'application/json' }
        );
        formData.append('protocol_cfg_header', configBlob);

        configBlob = new Blob(
            [JSON.stringify(protocol.req_cfg)],
            { type: 'application/json' }
        );
        formData.append('protocol_req_cfg',configBlob);

        configBlob = new Blob(
            [JSON.stringify(protocol.resp_cfg)],
            { type: 'application/json' }
        );
        formData.append('protocol_resp_cfg', configBlob);

        appendProtocolBodyData(formData, 1, protocol.cfg_header.req_body_type, protocol.request_body);
        appendProtocolBodyData(formData, 2, protocol.cfg_header.resp_body_type, protocol.response_body);

        return formData;
    }

    KitProxy.utils = {
        delay,
        showLoading,
        hideLoading,
        showGlobalNotification,
        showGlobalError,
        showGlobalSuccess,
        clearGlobalNotificationPopup,
        clearGlobalErrorPopup: clearGlobalNotificationPopup,
        runMutationOnce,
        isMutationPending,
        removeDomNode,
        escapeHTML,
        bindModalCloseActions,
        createTextImportModal,
        bindInlineTitleEditor,
        ExtractId,
        checkEmptyState,
        loadModal,
        validateHttpPath,
        validateHttpStatusCode,
        normalizeHttpStatusCode,
        validatePort,
        validateIpPort,
        validateJsonText,
        formatJsonText,
        formatXMLHelper,
        appendProtocolBodyData,
        createProtocolBodyFormData,
        createAddProtocolFormData,
    };

    // 保留旧全局函数名，兼容已经存在的动态模态框事件处理。
    global.delay = delay;
    global.showLoading = showLoading;
    global.hideLoading = hideLoading;
    global.showGlobalNotification = showGlobalNotification;
    global.showGlobalError = showGlobalError;
    global.showGlobalSuccess = showGlobalSuccess;
    global.runMutationOnce = runMutationOnce;
    global.removeDomNode = removeDomNode;
    global.escapeHTML = escapeHTML;
    global.ExtractId = ExtractId;
    global.checkEmptyState = checkEmptyState;
    global.loadModal = loadModal;
    global.formatXMLHelper = formatXMLHelper;
    global.formBodyDataHeler = appendProtocolBodyData;
})(typeof window !== 'undefined' ? window : globalThis);
