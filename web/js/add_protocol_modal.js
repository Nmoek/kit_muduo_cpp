/**
 * @param {{ create: (arg0: any) => any; }} modal
 * @param {HTMLDivElement} serviceCard
 */
function createAddProtocolModal(modal, serviceCard) {
    return modal.create(serviceCard);
}

/**
 * @param {HTMLElement} targetField
 * @param {any} targetId
 * @param {any} content
 */
function updateBodyImportState(targetField, targetId, content) {
    // Body 内容只暂存在当前动态表单的 dataset 中，提交时再组装到协议 payload。
    const hasContent = Boolean(content);

    if (hasContent) {
        targetField.dataset.importedContent = content;
    } else {
        delete targetField.dataset.importedContent;
    }

    const formGroup = targetField.parentNode && targetField.parentNode.parentNode;
    const statusSpan = formGroup ? formGroup.querySelector('.body-type-header .import-status') : null;
    if (statusSpan) {
        statusSpan.style.display = hasContent ? 'inline' : 'none';
    }

    const indicator = document.querySelector(`[for="${targetId}"] .body-indicator`);
    if (indicator) {
        indicator.classList.toggle('has-body', hasContent);
        indicator.classList.toggle('no-body', !hasContent);
    }

    const protocolItem = document.querySelector(`.protocol-item [for="${targetId}"]`);
    if (protocolItem) {
        const bodyIndicator = protocolItem.querySelector('.body-indicator');
        const valueDisplay = protocolItem.nextElementSibling;
        if (bodyIndicator) {
            bodyIndicator.classList.toggle('has-body', hasContent);
            bodyIndicator.classList.toggle('no-body', !hasContent);
        }
        if (valueDisplay) {
            valueDisplay.textContent = hasContent ? '已设置' : '未设置';
        }
    }
}

function bindBodyImportButtons(modal) {
    modal.querySelectorAll('.import-btn').forEach(btn => {
        btn.addEventListener('click', function() {
            const targetId = this.getAttribute('data-target');
            const targetField = document.getElementById(targetId);
            if (!targetField) return;

            const bodyName = targetId.includes('request') ? '请求' : '响应';
            KitProxy.utils.createTextImportModal({
                title: `导入${bodyName}Body内容`,
                placeholder: `请在此输入${bodyName}Body内容...`,
                value: targetField.dataset.importedContent || '',
                bodyType: targetField.value || 'json',
                allowedTypes: KitProxy.protocolTypes && typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function'
                    ? KitProxy.protocolTypes.getBodyTypeOptions(modal.dataset.projectProtocolType)
                    : undefined,
                useBodyEditor: true,
                modalClassName: 'add-protocol-item-modal import-modal',
                onConfirm: function(content, bodyType) {
                    if (bodyType && targetField.querySelector(`option[value="${bodyType}"]`)) {
                        targetField.value = bodyType;
                    }
                    updateBodyImportState(targetField, targetId, content);
                },
            });
        });
    });
}


/**
 * http协议添加模态框
 */
var httpProtocolModal = {
    create: function(/** @type {{ id: any; }} */ serviceCard) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.dataset.projectProtocolType = String(ProtocolType.HTTP);
        modal.innerHTML = `
            <div class="add-protocol-item-modal">
                <div class="modal-header">
                    <h3>添加协议项</h3>
                    <button class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <form id="add-protocol-item-form">
                        <div class="form-columns-container">
                            <div class="form-group">
                                <label for="protocol-item-name">协议项名称</label>
                                <input type="text" id="protocol-item-name" placeholder="输入协议项名称" required>
                            </div>
                            <div class="form-group">
                                <label>请求方法</label>
                                <div class="method-radios">
                                    <label><input type="radio" name="request-method" value="GET" required> GET</label>
                                    <label><input type="radio" name="request-method" value="POST" required> POST</label>
                                    <label><input type="radio" name="request-method" value="PUT" required> PUT</label>
                                    <label><input type="radio" name="request-method" value="DELETE" required> DELETE</label>
                                </div>
                            </div>
                            <div class="form-group">
                                <label for="request-path">请求路径</label>
                                <input type="text" id="request-path" value="/api/" placeholder="输入请求路径" required>
                            </div>
                            
                            <div class="form-group">
                                <div class="body-type-header">
                                    <label for="request-body-type">校验请求Body</label>
                                    <scan class="import-status" style="display: none">内容已导入</scan>
                                </div>
                                <div class="body-type-container">
                                    <select id="request-body-type">
                                        <option value="json">JSON</option>
                                        <option value="xml">XML</option>
                                        <option value="text">Text</option>
                                    </select>
                                    <button type="button" class="import-btn" data-target="request-body-type">导入</button>
                                </div>
                            </div>

                            <div class="form-group">
                                <div class="body-type-header">
                                    <label for="response-body-type">目标响应Body</label>
                                    <scan class="import-status" style="display: none">内容已导入</scan>
                                </div>
                                <div class="body-type-container">
                                    <select id="response-body-type">
                                        <option value="json">JSON</option>
                                        <option value="xml">XML</option>
                                        <option value="text">Text</option>
                                    </select>
                                    <button type="button" class="import-btn" data-target="response-body-type">导入</button>
                                </div>
                            </div>
                        </div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="submit" class="confirm-btn">确认添加</button>
                        </div>
                    </form>
                </div>
            </div>
        `;

        bindBodyImportButtons(modal);

        // 处理表单提交
        modal.querySelector('#add-protocol-item-form').addEventListener('submit', async function(e) {
            e.preventDefault();
            const loading = showLoading('正在添加协议项...');
            
            try {
                const itemName = document.getElementById('protocol-item-name').value;

                const methodStr = modal.querySelector('input[name="request-method"]:checked')?.value;
                const pathStr = document.getElementById('request-path').value;
                
                // 获取导入的Body内容
                const requestBodyType = document.getElementById('request-body-type').value;
                const responseBodyType = document.getElementById('response-body-type').value;
                const requestBody = document.getElementById('request-body-type').dataset.importedContent || '';
                const responseBody = document.getElementById('response-body-type').dataset.importedContent || '';
                
                // 等价级前端校验：提前拦截明显错误，不改变后端 payload 结构。
                if(!itemName.trim()) {
                    throw new Error('协议项名称不能为空');
                }

                if(!methodStr) {
                    throw new Error('请选择请求方法');
                }

                if(!KitProxy.utils.validateHttpPath(pathStr)) {
                    throw new Error('请求路径必须以 / 开头');
                }

                const requestValidation = KitProxy.bodySyntax
                    ? KitProxy.bodySyntax.validate(requestBody, requestBodyType)
                    : { valid: KitProxy.utils.validateJsonText(requestBody), message: '校验请求Body不是合法 JSON' };
                if(!requestValidation.valid) {
                    throw new Error(`校验请求Body格式错误：${requestValidation.message}`);
                }

                const responseValidation = KitProxy.bodySyntax
                    ? KitProxy.bodySyntax.validate(responseBody, responseBodyType)
                    : { valid: KitProxy.utils.validateJsonText(responseBody), message: '目标响应Body不是合法 JSON' };
                if(!responseValidation.valid) {
                    throw new Error(`目标响应Body格式错误：${responseValidation.message}`);
                }

                const projectId = ExtractId(serviceCard.id);


                const submit_protocol = {};
                submit_protocol.cfg_header = {
                    "name": itemName,
                    "type": 'HTTP',
                    "project_id": projectId,
                    "req_body_type": requestBodyType,
                    "resp_body_type": responseBodyType,
                };
                submit_protocol.req_cfg = {
                    "path": pathStr,
                    "method": methodStr,
                };
                // 响应暂时为空
                submit_protocol.resp_cfg = {};

                submit_protocol.request_body = requestBody;
                submit_protocol.response_body = responseBody;

                console.info('当前协议信息:', submit_protocol);

                addHTTPProtocol(serviceCard, submit_protocol);

                KitProxy.utils.removeDomNode(modal);

            } catch (error) {
                console.error('添加协议项出错:', error);
                alert('添加协议项失败: ' + error.message);
                hideLoading(loading);
            } 

            await delay(500);
            hideLoading(loading);
            
        });

        KitProxy.utils.bindModalCloseActions(modal);

        return modal;
    }
};




/**
 * 自定义协议添加模态框
 */
var customTcpProtocolModal = {
    create: function(serviceCard) {
        
        const projectId = ExtractId(serviceCard.id);


        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.dataset.projectProtocolType = String(ProtocolType.CUSTOM_TCP);
        modal.innerHTML = `
            <div class="add-protocol-item-modal">
                <div class="modal-header">
                    <h3>添加协议项</h3>
                    <button class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <form id="add-protocol-item-form">
                        <div class="form-columns-container">
                            <div class="form-group">
                                <label for="protocol-item-name">协议项名称</label>
                                <input type="text" id="protocol-item-name" placeholder="输入协议项名称" required>
                            </div>
                            <div class="form-group"></div>
                            <div class="form-group">
                                <label>请求功能码</label>
                                <input class="req-function-code-value" placeholder="示例:H010203..."></input>
                            </div>
                            <div class="form-group">
                                <label>响应功能码</label>
                                <input class="resp-function-code-value" placeholder="示例:H010203..."></input>
                            </div>
                            <div class="form-group">
                                <div class="pattern-header">
                                    <label>校验请求头部</label>
                                    <scan class="import-status" id="req-pattern-import-status" style="display: none">格式已设置</scan>
                                </div>
                                <button type="button" class="pattern-config-btn" id="req-pattern-infos" data-target="req-pattern-infos">配置</button>
                            </div>
                            <div class="form-group">
                                <div class="pattern-header">
                                    <label>目标响应头部</label>
                                    <scan class="import-status" id="resp-pattern-import-status" style="display: none">格式已设置</scan>
                                </div>
                                <button type="button" class="pattern-config-btn" id="resp-pattern-infos" data-target="resp-pattern-infos">配置</button>
                            </div>
                            
                            <div class="form-group">
                                <div class="body-type-header">
                                    <label for="request-body-type">校验请求Body</label>
                                    <scan class="import-status" style="display: none">内容已导入</scan>
                                </div>
                                <div class="body-type-container">
                                    <select id="request-body-type">
                                        <option value="json">JSON</option>
                                        <option value="xml">XML</option>
                                        <option value="text">Text</option>
                                    </select>
                                    <button type="button" class="import-btn" data-target="request-body-type">导入</button>
                                </div>
                            </div>

                            <div class="form-group">
                                <div class="body-type-header">
                                    <label for="response-body-type">目标响应Body</label>
                                    <scan class="import-status" style="display: none">内容已导入</scan>
                                </div>
                                <div class="body-type-container">
                                    <select id="response-body-type">
                                        <option value="json">JSON</option>
                                        <option value="xml">XML</option>
                                        <option value="text">Text</option>
                                    </select>
                                    <button type="button" class="import-btn" data-target="response-body-type">导入</button>
                                </div>
                            </div>
                        </div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="submit" class="confirm-btn">确认添加</button>
                        </div>
                    </form>
                </div>
            </div>
        `;

        // 必须预加载格式字段 否则不弹框
        let specialFields;

        const loading = showLoading('正在加载格式信息...');
        getSpecialPatternFields(projectId)
        .then(async data => {

            console.log('specialFields: ', data);

            specialFields = data;

            await delay(200);
            hideLoading(loading);

        })
        .catch (error => {

            alert('格式信息请求失败!');
            hideLoading(loading);
            KitProxy.utils.removeDomNode(modal);
        });

        // 请求头部配置和响应是不一样的
        /* 请求/响应头部字段 */
        modal.querySelectorAll('.pattern-config-btn').forEach(btn => {
            
            const req_or_resp = btn.id.includes('req-pattern-infos') ? 1 : 2;
            const title = req_or_resp === 1 ? '请求头部全部字段' : '响应头部全部字段';
            const inportStatusElement  = btn.parentNode.querySelector('.import-status');

            btn.addEventListener('click', function(e) {
            
                e.stopPropagation();
                e.preventDefault();

                // 先看是否有缓存，每一次新建该模态框都需要进行一次网路请求 
                
                // 注意: 缓存内容是JSON字符串 而输入是JSON实体
                let cachedPatternInfos = this.dataset.patternInfos; 
                let inputPatternInfosMap = {};
                let curPatternInfosMap = {};

                // 先看是否已设置过缓存
                if(!cachedPatternInfos) {
                    const defaultCommonFields = req_or_resp === 2
                        ? JSON.parse(`[{"name":"消息总长度","idx":1,"byte_pos":4,"byte_len":4,"type":"UINT32","value":"H02090000"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H00000003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":"HA86D9F9F9A010000"}]`)
                        : JSON.parse(`[{"name":"消息总长度","idx":1,"byte_pos":4,"byte_len":4,"type":"UINT32","value":"H0209"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H0003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":""}]`);

                    curPatternInfosMap = {
                        "special_fields": specialFields || {},
                        "common_fields": KitProxy.api.isMockMode() ? defaultCommonFields : [],
                    }

                    // this.dataset.patternInfos = JSON.stringify(curPatternInfosMap);
                    console.log('no cache patterninfos!!!!!!!');
                } else {
                    curPatternInfosMap = JSON.parse(cachedPatternInfos);
                    console.log('curPatternInfosMap!!!!!');
                }

                // TODO: 这里先不区分 请求 or 响应 
                const specialFieldsTmp = curPatternInfosMap["special_fields"];
                if(1/*1 === req_or_resp*/) {
                    if(specialFieldsTmp) {
                        inputPatternInfosMap["special_fields"] = {};
                        Object.keys(specialFieldsTmp).forEach(key => {
                            inputPatternInfosMap["special_fields"][key]  = createPatternField('', key, specialFieldsTmp[key]);
                        });
                    }
                }


                const commonFieldsTmp = curPatternInfosMap["common_fields"];
                if(commonFieldsTmp) {
                    inputPatternInfosMap["common_fields"] = [];
                    commonFieldsTmp.forEach(item => {
                        inputPatternInfosMap["common_fields"].push(createPatternField('', '', item));
                    });
                }

                console.log('inputPatternInfosMap: ', inputPatternInfosMap);

                createCustomTcpPatternModal(this, title, inputPatternInfosMap, inportStatusElement, false);

            });
        });

        // 关闭模态框（注意:关闭时需要将当前网络请求也取消）
        KitProxy.utils.bindModalCloseActions(modal);

        bindBodyImportButtons(modal);

        // 处理表单提交
        modal.querySelector('#add-protocol-item-form').addEventListener('submit', async function(e) {
            e.preventDefault();
            const loading = showLoading('正在添加协议项...');
            
            try {
                const itemName = document.getElementById('protocol-item-name').value;

                const reqFuncCodeStr = modal.querySelector('.req-function-code-value').value;
                const respFuncCodeStr = modal.querySelector('.resp-function-code-value').value;
    
                // 获取导入的Body内容
                const requestBodyType = document.getElementById('request-body-type').value;
                const responseBodyType = document.getElementById('response-body-type').value;
                const requestBody = document.getElementById('request-body-type').dataset.importedContent || '';
                const responseBody = document.getElementById('response-body-type').dataset.importedContent || '';

                // TCP 协议项仍复用 addHTTPProtocol 这条添加链路，差异体现在 cfg_header.type 和 req/resp cfg。
                if(!itemName.trim()) {
                    throw new Error('协议项名称不能为空');
                }

                if(reqFuncCodeStr && !reqFuncCodeStr.startsWith('H')) {
                    throw new Error('请求功能码必须以 H 开头');
                }

                if(respFuncCodeStr && !respFuncCodeStr.startsWith('H')) {
                    throw new Error('响应功能码必须以 H 开头');
                }

                const requestValidation = KitProxy.bodySyntax
                    ? KitProxy.bodySyntax.validate(requestBody, requestBodyType)
                    : { valid: KitProxy.utils.validateJsonText(requestBody), message: '校验请求Body不是合法 JSON' };
                if(!requestValidation.valid) {
                    throw new Error(`校验请求Body格式错误：${requestValidation.message}`);
                }

                const responseValidation = KitProxy.bodySyntax
                    ? KitProxy.bodySyntax.validate(responseBody, responseBodyType)
                    : { valid: KitProxy.utils.validateJsonText(responseBody), message: '目标响应Body不是合法 JSON' };
                if(!responseValidation.valid) {
                    throw new Error(`目标响应Body格式错误：${responseValidation.message}`);
                }

                const projectId = ExtractId(serviceCard.id);
                let reqCommonFields = [];
                let respCommonFields = [];

                // 获取请求头部
                let cachedPatternInfos = document.getElementById('req-pattern-infos').dataset.patternInfos;
                if(cachedPatternInfos) {
                    const tmp = JSON.parse(cachedPatternInfos);
                    reqCommonFields = tmp["common_fields"];
                }

                cachedPatternInfos = document.getElementById('resp-pattern-infos').dataset.patternInfos;
                if(cachedPatternInfos) {
                    const tmp = JSON.parse(cachedPatternInfos);
                    respCommonFields = tmp["common_fields"];
                }

                const submit_protocol = {};
                submit_protocol.cfg_header = {
                    "name": itemName,
                    "type": "TCP",
                    "project_id": projectId,
                    "req_body_type": requestBodyType,
                    "resp_body_type": responseBodyType,
                    "is_endian": 1,
                };
                submit_protocol.req_cfg = {
                    "function_code_filed_value": reqFuncCodeStr,
                    "common_fields": reqCommonFields,
                };
                submit_protocol.resp_cfg = {
                    "function_code_filed_value": respFuncCodeStr,
                    "common_fields": respCommonFields,
                };

                submit_protocol.request_body = requestBody;
                submit_protocol.response_body = responseBody;

                console.info('当前协议信息:', submit_protocol);

                addHTTPProtocol(serviceCard, submit_protocol);

                KitProxy.utils.removeDomNode(modal);

            } catch (error) {
                console.error('添加协议项出错:', error);
                alert('添加协议项失败: ' + error.message);
                hideLoading(loading);
            } 

            await delay(500);
            hideLoading(loading);
            
        });

        return modal;

    }
};
