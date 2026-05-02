/**
 * 生成不同协议项配置子类网格
 * @param {派生子类} item 
 * @param {协议项信息} protocol 
 * @returns 
 */
function createProtocolItemGrids(item, protocol) {
    return item.create(protocol);
}


var httpProtocolItemGrids = {
    create: function(protocol) {
        const grids = document.createElement('div');
        grids.className = 'details-grid http';
        grids.innerHTML = `
            <div class="protocol-field req-cfg" data-field-name="method">
                <label>期望请求方法</label>
                <div class="value">${protocol.req_cfg.method}</div>
            </div>
            <div class="protocol-field req-cfg" data-field-name="path">
                <label>请求路径</label>
                <div class="value">${protocol.req_cfg.path}</div>
            </div>
            <div class="protocol-field request-body">
                <label><span class="body-indicator ${protocol.req_body_status === 1 ? 'has' : 'no'}"></span>校验请求Body</label>
                <div class="value">${protocol.req_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
            <div class="protocol-field response-body">
                <label><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has' : 'no'}"></span>目标响应Body</label>
                <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
        `;

        // 请求方法编辑功能 
        const methodField = grids.querySelector('.protocol-field[data-field-name="method"]');
        if (methodField) {
            methodField.style.cursor = 'pointer';
            
            methodField.addEventListener('click', function(e) {

                e.stopPropagation();
                e.preventDefault();

                const valueElement = this.querySelector('.value');
                if (!valueElement) {
                    console.error('未能找到value元素');
                    return;
                }
                
                const currentMethod = valueElement.textContent.trim();
                
                const modal = document.createElement('div');
                modal.className = 'modal-overlay';
                modal.innerHTML = `
                    <div class="edit-method-modal">
                        <div class="modal-header">
                            <h3>修改请求方法</h3>
                            <button class="close-modal">&times;</button>
                        </div>
                        <div class="modal-body">
                            <div class="method-radios">
                                <label><input type="radio" name="edit-method" value="GET" ${currentMethod.includes('GET') ? 'checked' : ''}> GET</label>
                                <label><input type="radio" name="edit-method" value="POST" ${currentMethod.includes('POST') ? 'checked' : ''}> POST</label>
                                <label><input type="radio" name="edit-method" value="PUT" ${currentMethod.includes('PUT') ? 'checked' : ''}> PUT</label>
                                <label><input type="radio" name="edit-method" value="DELETE" ${currentMethod.includes('DELETE') ? 'checked' : ''}> DELETE</label>
                            </div>
                            <div class="form-actions">
                                <button type="button" class="cancel-btn">取消</button>
                                <button type="button" class="confirm-btn">确定修改</button>
                            </div>
                        </div>
                    </div>
                `;
                document.body.appendChild(modal);

                // 处理确定按钮
                modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                    // 获取当前选中按钮的值
                    const newMethod = modal.querySelector('input[name="edit-method"]:checked').value;
                    let ok = true;

                    if(valueElement.textContent != newMethod) {
                        // 注意: 这里需要去组装整个req_cfg才能去发出
                        ok = await updateProtocolCfg(protocol.id, 1, 'method', newMethod);
                    }

                    if (ok) {
                        valueElement.textContent = newMethod;
                        alert("http请求方法修改成功!");
                    } else {
                        alert("http请求方法修改失败!");
                    }

                    KitProxy.utils.removeDomNode(modal);
                });

                KitProxy.utils.bindModalCloseActions(modal);
            }); // 结束 methodField 点击事件
        } 

        // 添加请求路径编辑功能
        const pathField = grids.querySelector('[data-field-name="path"]');
        if (pathField) {
            pathField.style.cursor = 'pointer';
            
            pathField.addEventListener('click', function(e) {
  
                e.stopPropagation();
                e.preventDefault();
                
                const valueElement = this.querySelector('.value');
                if (!valueElement) {
                    console.error('未能找到value元素');
                    return;
                }
                
                const currentPath = valueElement.textContent.trim();
                
                const modal = document.createElement('div');
                modal.className = 'modal-overlay';
                modal.innerHTML = `
                    <div class="edit-path-modal">
                        <div class="modal-header">
                            <h3>修改请求路径</h3>
                            <button class="close-modal">&times;</button>
                        </div>
                        <div class="modal-body">
                            <div class="form-group">
                                <label for="new-path">请求路径</label>
                                <input type="text" id="new-path" value="${currentPath}" placeholder="输入请求路径" required>
                                <div class="path-hint">必须以/开头，例如：/api/v1/test</div>
                            </div>
                            <div class="form-actions">
                                <button type="button" class="cancel-btn">取消</button>
                                <button type="button" class="confirm-btn">确定修改</button>
                            </div>
                        </div>
                    </div>
                `;
                
                document.body.appendChild(modal);

                // 处理确定按钮
                modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                    const newPath = document.getElementById('new-path').value.trim();

                    // TODO 优化 本质上是将 /// abc ///  最内圈的字段内容提取出来
                    if (KitProxy.utils.validateHttpPath(newPath)) {
                        let ok = true;
                        if(valueElement.textContent != newPath) {
                            
                            ok = await updateProtocolCfg(protocol.id, 1, 'path', newPath);
                        }

                        if (ok) {
                            valueElement.textContent = newPath;
                            alert("http路径修改成功!");
                        } else {
                            alert("http路径修改失败!");
                        }

                        KitProxy.utils.removeDomNode(modal);

                    } else {
                        alert('路径格式不正确, 请检查!');
                    }
                });

                KitProxy.utils.bindModalCloseActions(modal);
            });
        }


        return grids;
    }
};

async function getAllPatternFieldsReq(protocolId, req_or_resp) {
    
    const protocolItem = document.getElementById(`protocol-item-${protocolId}`);

    const projectId = ExtractId(protocolItem.dataset.projectId);


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
        
        return pattern_info.special_fields || [];
    } catch (error) {
        console.error('获取特殊字段信息失败!');
        throw error;
    }
}

async function getPatternFields(protocolId, req_or_resp) {

    try {

        const [pattern_info, common_fields] = await getAllPatternFieldsReq(protocolId, req_or_resp)

        return {
            "special_fields": pattern_info.special_fields || [],
            "common_fields": common_fields || [],
        }

    } catch (error) {
        console.error('获取所有字段信息失败!');
        throw error;
    }

}

var customTcpProtocolItemGrids = {
    create: function(protocol) {

        const grids = document.createElement('div');
        grids.className = 'details-grid tcp';
        grids.innerHTML = `
            <div class="protocol-field req-cfg" data-field-name="function_code_filed_value">
                <label>请求功能码</label>
                <div class="value">${protocol.req_cfg.function_code_filed_value}</div> 
            </div>
            <div class="protocol-field req-cfg" data-field-name="common_fields">
                <label><span class="header-fields-indicator ${protocol.req_cfg.common_fields.length ? 'has' : 'no'}"></span>请求头部字段</label>
                <div class="value" id="${protocol.id}-header-fields">${protocol.req_cfg.common_fields.length ? '已设置' : '未设置'}</div>
            </div>

            <div class="protocol-field request-body">
                <label><span class="body-indicator ${protocol.req_body_status === 1 ? 'has' : 'no'}"></span>校验请求Body</label>
                <div class="value">${protocol.req_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>

            <div class="protocol-field resp-cfg" data-field-name="function_code_filed_value">
                <label>响应功能码</label>
                <div class="value">${protocol.resp_cfg.function_code_filed_value}</div> 
            </div>
            <div class="protocol-field resp-cfg" data-field-name="common_fields">
                <label><span class="header-fields-indicator ${protocol.resp_cfg.common_fields.length ? 'has' : 'no'}"></span>响应头部字段</label>
                <div class="value">${protocol.resp_cfg.common_fields.length ? '已设置' : '未设置'}</div>
            </div>
            <div class="protocol-field response-body">
                <label><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has' : 'no'}"></span>目标响应Body</label>
                <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
        `;

        // 功能码编辑功能(请求和响应相同)
        grids.querySelectorAll('[data-field-name="function_code_filed_value"]').forEach(field => {
            field.style.cursor = 'pointer';
            const req_or_resp = field.className.includes('req-cfg') ? 1 : 2;

            field.addEventListener('click', function(e) {
                e.stopPropagation();
                e.preventDefault();
                
                const valueElement = this.querySelector('.value');
                if (!valueElement) {
                    console.error('未能找到value元素');
                    return;
                }
                
                const currentCode = valueElement.textContent.trim();
                
                const modal = document.createElement('div');
                modal.className = 'modal-overlay';
                modal.innerHTML = `
                    <div class="edit-path-modal">
                        <div class="modal-header">
                            <h3>修改功能码</h3>
                            <button class="close-modal">&times;</button>
                        </div>
                        <div class="modal-body">
                            <div class="form-group">
                                <label for="new-func-code">功能码值</label>
                                <input type="text" id="new-func-code" value="${currentCode}" placeholder="输入功能码" required>
                                <div class="path-hint">必须H开头表示十六进制的单字节流，例如：H010203</div>
                            </div>
                            <div class="form-actions">
                                <button type="button" class="cancel-btn">取消</button>
                                <button type="button" class="confirm-btn">确定修改</button>
                            </div>
                        </div>
                    </div>
                `;
                
                document.body.appendChild(modal);

                // 处理确定按钮
                modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                    const newCode = document.getElementById('new-func-code').value.trim();

                    // TODO 优化 本质上是将 /// abc ///  最内圈的字段内容提取出来
                    if (newCode && newCode.startsWith('H')) {
                        let ok = true;
                        if(valueElement.textContent != newCode) {

                            ok = await updateProtocolCfg(protocol.id, req_or_resp, 'function_code_filed_value', newCode);
                        }

                        if (ok) {
                            valueElement.textContent = newCode;
                            alert("功能码修改成功!");
                        } else {
                            alert("功能码修改失败!");
                        }

                        KitProxy.utils.removeDomNode(modal);

                    } else {
                        alert('功能码格式不正确, 请检查!');
                    }
                });

                KitProxy.utils.bindModalCloseActions(modal);
            });
            
        });

        // 头部字段编辑功能
        grids.querySelectorAll('[data-field-name="common_fields"]').forEach(field => {
            

            field.style.cursor = 'pointer';
            const req_or_resp = field.className.includes('req') ? 1 : 2;
            const title = req_or_resp === 1 ? '请求头部全部字段' : '响应头部全部字段';

            
            field.addEventListener('click', async function(e) {
                e.stopPropagation();
                e.preventDefault();

                const loading = showLoading('加载格式字段信息...');

                const inputPatternInfosMap = {};
                let curPatternInfosMap;
                const headerIndicator = this.querySelector('.header-fields-indicator');
                const valueElement = this.querySelector('.value');

                // 注意: 这里不需要对字段进行缓存
                try {
                    curPatternInfosMap = await getPatternFields(protocol.id, req_or_resp);

                } catch (error) {
                    console.error('获取格式字段信息失败: ', error);
                    hideLoading(loading);
                    alert('获取格式字段信息失败!');
                    return;
                }
                await delay(200);
                hideLoading(loading);


                console.log('curPatternInfosMap:: ', curPatternInfosMap);

                // 注意: 对外发送的报文不需要特殊字段 收到的报文才需要
                // if(1 === req_or_resp) {

                    const specialFieldsTmp = curPatternInfosMap["special_fields"];
                    if(specialFieldsTmp) {
                        inputPatternInfosMap["special_fields"] = {};
                        Object.keys(specialFieldsTmp).forEach(key => {
                            inputPatternInfosMap["special_fields"][key]  = createPatternField('', key, specialFieldsTmp[key]);
                        });
                    }
                // }

                const commonFieldsTmp = curPatternInfosMap["common_fields"];
                if(commonFieldsTmp) {
                    inputPatternInfosMap["common_fields"] = [];
                    commonFieldsTmp.forEach(item => {
                        inputPatternInfosMap["common_fields"].push(createPatternField('', '', item));
                    });
                }

                console.log('inputPatternInfosMap: ', inputPatternInfosMap);

                createCustomTcpPatternModal(field, title, inputPatternInfosMap, null, false, async function(patternFieldInfos) {
                    
                    // 删除缓存
                    delete field.dataset.patternInfos;
                    
                    console.log(curPatternInfosMap, ',', patternFieldInfos);

                    // 没有改动则直接返回
                    if(JSON.stringify(curPatternInfosMap["common_fields"]) === JSON.stringify(patternFieldInfos["common_fields"])
                    ) {
                        console.log('字段信息没有发生变化!');
                        return;
                    }

                    const loading = showLoading();

                    const ok = await updateProtocolCfg(protocol.id, req_or_resp, 'common_fields', patternFieldInfos["common_fields"]);

                    if (!ok) {
                        alert("字段修改失败!");
                        return;
                    }

                    if (patternFieldInfos["common_fields"].length > 0) {
            
                        valueElement.textContent = '已设置';
                        headerIndicator.classList.add('has');
                        headerIndicator.classList.remove('no');

                    } else {
                        valueElement.textContent = '未设置';
                        headerIndicator.classList.remove('has');
                        headerIndicator.classList.add('no');
                    }
    
                    await delay(200);
                    hideLoading(loading);

                });
                
            });
        });

        return grids;
    }
};

/**
 * 生成body编辑框
 * @param {*} body_type 
 * @param {*} body_data 
 */
function createProtocolItemBodyModal(body_type, body_data, handleCb, options = {}) {

    let currentBody = KitProxy.bodySyntax
        ? KitProxy.bodySyntax.decodeBodyData(body_data)
        : new TextDecoder().decode(body_data || new Uint8Array());

    try {
        if(currentBody && KitProxy.bodySyntax) {
            currentBody = KitProxy.bodySyntax.format(currentBody, body_type);
        }
    } catch(error) {
        console.error('请求体数据解析出错! ', error.message);
    }
    
    console.info("body_type: ", body_type);
    console.info("body: ", currentBody);

    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.innerHTML = `
        <div class="edit-body-modal">
            <div class="modal-header">
                <h3>编辑Body</h3>
                <button class="close-modal">&times;</button>
            </div>
            <div class="modal-body">
                <div class="form-group">
                    <label>Body内容</label>
                    <div id="body-editor-host"></div>
                </div>
                <div class="form-actions">
                    <button type="button" class="cancel-btn">取消</button>
                    <button type="button" class="confirm-btn">确定修改</button>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);

    const bodyEditor = KitProxy.bodyEditor.create(modal.querySelector('#body-editor-host'), {
        idPrefix: 'protocol-body',
        value: currentBody,
        bodyType: body_type,
        allowedTypes: KitProxy.protocolTypes && typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function'
            ? KitProxy.protocolTypes.getBodyTypeOptions(options.protocolType || 'HTTP')
            : undefined,
        placeholder: options.placeholder || '输入 Body 内容...',
    });

    
    // 处理确定按钮
    modal.querySelector('.confirm-btn').addEventListener('click', async function(e) {
        e.stopPropagation();
        e.preventDefault();

        const validation = bodyEditor.validate();
        if(!validation.valid) {
            alert(validation.message);
            return;
        }

        const newBodyType = bodyEditor.getType();
        const newBody = bodyEditor.getValue().trim();

        await handleCb(newBodyType, newBody);

        KitProxy.utils.removeDomNode(modal);
    });

    KitProxy.utils.bindModalCloseActions(modal);
}
