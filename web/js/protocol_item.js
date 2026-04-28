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

// xml文本解析辅助函数
function formatXMLHelper(xmlString) {
    // 使用DOMParser解析XML
    const parser = new DOMParser();
    const xmlDoc = parser.parseFromString(xmlString, "application/xml");
    
    // 检查解析错误
    const parserError = xmlDoc.querySelector("parsererror");
    if (parserError) {
        throw new Error("XML解析错误: " + parserError.textContent);
    }
    
    // 递归格式化节点
    function formatNode(node, indentLevel) {
        const indent = ' '.repeat(indentLevel * 4);
        let output = '';
        
        // 处理元素节点
        if (node.nodeType === Node.ELEMENT_NODE) {
            // 开始标签
            output += `${indent}<${node.tagName}`;
            
            // 添加属性
            for (let i = 0; i < node.attributes.length; i++) {
                const attr = node.attributes[i];
                output += ` ${attr.name}="${attr.value}"`;
            }
            
            // 检查子节点
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
                
                // 处理子节点
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
        } 
        // 处理文本节点
        else if (node.nodeType === Node.TEXT_NODE) {
            const text = node.textContent.trim();
            if (text) {
                output += `${indent}${text}\n`;
            }
        }
        // 处理注释节点
        else if (node.nodeType === Node.COMMENT_NODE) {
            output += `${indent}<!--${node.data}-->\n`;
        }
        // 处理文档类型节点
        else if (node.nodeType === Node.DOCUMENT_TYPE_NODE) {
            output += `${indent}<!DOCTYPE ${node.name}>\n`;
        }
        // 处理处理指令节点（如 <?xml ... ?>）
        else if (node.nodeType === Node.PROCESSING_INSTRUCTION_NODE) {
            output += `${indent}<?${node.target} ${node.data}?>\n`;
        }
        
        return output;
    }
    
    // 从文档元素开始格式化
    return formatNode(xmlDoc.documentElement, 0);
};

/**
 * 生成body编辑框
 * @param {*} body_type 
 * @param {*} body_data 
 */
function createProtocolItemBodyModal(body_type, body_data, handleCb) {

    /* TODO: 不同格式文本解析 又能单独拆分一套控件出来 */
    // 需要根据body_type 预处理一下body数据
    let currentBody = '';
    try {
        if(body_data && body_data.length > 0) {

            // 注意: 如果属于文本数据 需要先解码成字符串
            if('json' === body_type) {
                const root = JSON.parse(new TextDecoder().decode(body_data));
                currentBody = JSON.stringify(root, null, 2);


            } else if('xml' == body_type) {

                currentBody = formatXMLHelper(new TextDecoder().decode(body_data));

            } else if('binary' == body_type) {
            
            
            }

        }
    } catch(error) {
        console.error('请求体数据解析出错! ', error.message);
        currentBody = body_data;
    }
    
    console.info("body_type: ", body_type);
    console.info("body: ", currentBody);

    // TODO: 文本框可以根据不同body类型进行自适应切换显示，并且提供对应的格式校验功能

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
                    <label>Body类型</label>
                    <select id="body-type">
                        <option value="json">JSON</option>
                        <!-- <option value="xml">Xml</option> -->
                    </select>
                </div>
                <div class="form-group">
                    <label>Body内容</label>
                    <textarea id="body-content" placeholder="输入校验请求Body内容..." style="min-height: 200px"></textarea>
                </div>
                <div class="form-actions">
                    <button type="button" class="cancel-btn">取消</button>
                    <button type="button" class="confirm-btn">确定修改</button>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);

    document.getElementById('body-type').value = body_type;
    document.getElementById('body-content').value = currentBody;

    
    // 处理确定按钮
    modal.querySelector('.confirm-btn').addEventListener('click', async function(e) {
        e.stopPropagation();
        e.preventDefault();

        const newBodyType = document.getElementById('body-type').value;
        const newBody = document.getElementById('body-content').value.trim();

        if(newBodyType === 'json' && !KitProxy.utils.validateJsonText(newBody)) {
            alert('Body内容不是合法 JSON，请检查后再提交');
            return;
        }

        await handleCb(newBodyType, newBody);

        KitProxy.utils.removeDomNode(modal);
    });

    KitProxy.utils.bindModalCloseActions(modal);
}
