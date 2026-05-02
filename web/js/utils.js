(function initKitProxyUtils(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    // 通用工具集中在这里，避免 main.js、modal、protocol_item 继续复制实现。
    function delay(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    function showLoading(message = '处理中，请稍候...') {
        const loadingOverlay = document.createElement('div');
        loadingOverlay.className = 'loading-overlay';
        loadingOverlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(0,0,0,0.7);
            display: flex;
            justify-content: center;
            align-items: center;
            z-index: 9999;
        `;
        loadingOverlay.innerHTML = `
            <div class="loading-content" style="
                background: white;
                padding: 30px;
                border-radius: 8px;
                text-align: center;
                box-shadow: 0 4px 12px rgba(0,0,0,0.15);
                min-width: 300px;
            ">
                <div class="loading-spinner" style="
                    width: 50px;
                    height: 50px;
                    border: 5px solid #f3f3f3;
                    border-top: 5px solid #3498db;
                    border-radius: 50%;
                    margin: 0 auto 20px;
                    animation: spin 1s linear infinite;
                "></div>
                <p class="loading-message" style="
                    margin: 0;
                    font-size: 16px;
                    color: #333;
                ">${message}</p>
                <style>
                    @keyframes spin {
                        0% { transform: rotate(0deg); }
                        100% { transform: rotate(360deg); }
                    }
                </style>
            </div>
        `;
        document.body.appendChild(loadingOverlay);
        return loadingOverlay;
    }

    function hideLoading(loadingOverlay) {
        if (loadingOverlay && document.body.contains(loadingOverlay)) {
            document.body.removeChild(loadingOverlay);
        }
    }

    function removeDomNode(node) {
        if (node && node.parentNode) {
            node.parentNode.removeChild(node);
        }
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

    function createTextImportModal(options) {
        // 用于 Body 导入等“textarea + 确定/取消”的轻量弹窗，业务状态由 onConfirm 回调处理。
        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        const useBodyEditor = options.useBodyEditor && KitProxy.bodyEditor;
        modal.innerHTML = `
            <div class="${options.modalClassName || 'add-protocol-item-modal import-modal'}">
                <div class="modal-header">
                    <h3>${options.title || '导入内容'}</h3>
                    <button class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    ${useBodyEditor
                        ? '<div class="body-editor-host"></div>'
                        : `<textarea placeholder="${options.placeholder || '请在此输入内容...'}"></textarea>`}
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
                placeholder: options.placeholder || '请在此输入内容...',
            });
        } else if (textarea) {
            textarea.value = options.value || '';
        }
        document.body.appendChild(modal);

        const closeModal = bindModalCloseActions(modal);
        modal.querySelector('.confirm-btn').addEventListener('click', function() {
            if (bodyEditor) {
                const validation = bodyEditor.validate();
                if (!validation.valid) {
                    alert(validation.message);
                    return;
                }
            }

            if (typeof options.onConfirm === 'function') {
                options.onConfirm(
                    bodyEditor ? bodyEditor.getValue() : textarea.value,
                    bodyEditor ? bodyEditor.getType() : options.bodyType,
                );
            }
            closeModal();
        });

        return modal;
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
        // 后端新增协议接口要求 req/resp body 使用不同字段名，不能合并成同一个 key。
        if (!bodyData || bodyData.length <= 0) return;

        const bodyKey = body === 1 ? 'protocol_req_body' : 'protocol_resp_body';
        let bodyValue = '';

        if (bodyType.includes('json')) {
            bodyValue = JSON.stringify(JSON.parse(bodyData));
        } else if (bodyType.includes('xml')) {
            bodyValue = bodyData;
        } else if (bodyType.includes('binary')) {
            bodyValue = new Blob([bodyData]);
        } else {
            bodyValue = bodyData;
        }

        formData.append(bodyKey, bodyValue);
    }

    function createProtocolBodyFormData(protocolId, projectId, reqOrResp, protocolType, bodyType, body) {
        // /protocols/details/body 接口是 multipart：header 走 JSON 字符串，body 走文件字段。
        const formData = new FormData();
        formData.append('detail_header', JSON.stringify({
            id: protocolId,
            project_id: projectId,
            req_or_resp: reqOrResp,
            type: protocolType,
            body_type: bodyType,
        }));

        if (body && body.length > 0) {
            const binaryBlob = new Blob([body], { type: 'application/octet-stream' });
            formData.append('detail_cfg_data', binaryBlob, reqOrResp === 1 ? 'req_body.bin' : 'resp_body.bin');
        }

        return formData;
    }

    function createAddProtocolFormData(protocol) {
        // /protocols/add 的三个配置字段名是后端约定，重构时不能改名。
        const formData = new FormData();
        formData.append('protocol_cfg_header', JSON.stringify(protocol.cfg_header));
        formData.append('protocol_req_cfg', JSON.stringify(protocol.req_cfg));
        formData.append('protocol_resp_cfg', JSON.stringify(protocol.resp_cfg));

        appendProtocolBodyData(formData, 1, protocol.cfg_header.req_body_type, protocol.request_body);
        appendProtocolBodyData(formData, 2, protocol.cfg_header.resp_body_type, protocol.response_body);

        return formData;
    }

    KitProxy.utils = {
        delay,
        showLoading,
        hideLoading,
        removeDomNode,
        bindModalCloseActions,
        createTextImportModal,
        ExtractId,
        checkEmptyState,
        loadModal,
        validateHttpPath,
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
    global.removeDomNode = removeDomNode;
    global.ExtractId = ExtractId;
    global.checkEmptyState = checkEmptyState;
    global.loadModal = loadModal;
    global.formatXMLHelper = formatXMLHelper;
    global.formBodyDataHeler = appendProtocolBodyData;
})(typeof window !== 'undefined' ? window : globalThis);
