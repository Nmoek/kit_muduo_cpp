const servicePageState = KitProxy.pagination.createState();

// 更新协议项显示
function updateProtocolItem(id_str, protocol) {

    console.info('updateProtocolItem: ', protocol);

    const protocolItem = document.getElementById(id_str);

    protocolItem.querySelector(".protocol-name").textContent = protocol.name;

    // 方法名称
    protocolItem.querySelector('.protocol-field[data-field-name="method"]').querySelector('.value').textContent = protocol.req_cfg.method;
    // 请求路径
    protocolItem.querySelector('.protocol-field[data-field-name="path"]').querySelector(".value").textContent = protocol.req_cfg.path;

    // 请求Body
    const request_body_div =  protocolItem.querySelector('.protocol-field[data-field-name="request-body"]');
    request_body_div.querySelector('.body-indicator').className = `body-indicator ${protocol.req_body_status === 1 ? "has-body" : "no-body"}`;
    request_body_div.querySelector('.value').textContent = `${protocol.req_body_status === 1 ? '已设置' : '未设置'}`;
    // 响应Body
    const response_body_div =  protocolItem.querySelector('.protocol-field[data-field-name="response-body"]');
    response_body_div.querySelector('.body-indicator').className = `body-indicator ${protocol.resp_body_status === 1 ? "has-body" : "no-body"}`;
    response_body_div.querySelector('.value').textContent = `${protocol.resp_body_status === 1 ? '已设置' : '未设置'}`;


}


// 协议请求体配置
async function updateProtocolBodyReq(protocolId, projectId, req_or_resp, protocolType, bodyType, body) {
    // 兼容旧调用名，实际请求和 Mock/Real 切换都交给 KitProxy.api。
    try {
        await KitProxy.api.updateProtocolBody(protocolId, projectId, req_or_resp, protocolType, bodyType, body);
        console.log('协议项请求配置修改请求成功');
        return true;
    } catch (error) {
        console.error('协议项请求配置修改请求失败! ', error.message);
        return false;
    }
}



// 协议请求项配置
async function updateProtocolCfgReq(protocolId, projectId, req_or_resp, cfg_json) {
    // req_or_resp: 1 表示请求配置，2 表示响应配置；后端字段名保持原样。
    try {
        console.log('updateProtocolCfgReq: ', JSON.stringify({
            id: protocolId,
            project_id: projectId,
            req_or_resp: req_or_resp, 
            cfg_data: cfg_json
        }));

        await KitProxy.api.updateProtocolCfg(protocolId, projectId, req_or_resp, cfg_json);
        console.log('协议项配置修改请求成功');
        return true;

    } catch (error) {
        console.error('协议项配置修改请求失败! ', error.message);
        return false;
    }
}


async function getProtocolBodyTypeReq(protocolId) {
    try {


    } catch (error) {
        
    }
}

async function getProtocolItemBody(idStr, req_or_resp) {

    const protocolItemId = ExtractId(idStr);

    try {
        // Body 数据可能是二进制，返回值保持 [bodyType, Uint8Array]。
        return await KitProxy.api.getProtocolBody(protocolItemId, req_or_resp);
    } catch(error) {
        console.error("获取协议请求", error);
        throw error;
    }
}

async function getProtocolItemBodyV2(idStr, req_or_resp) {
    try {
        return await getProtocolItemBody(idStr, req_or_resp);
    } catch(error) {
        console.error("Body信息请求出错: ", error);
        throw error;
    }
}

function getCurProtocolItemCfgV1(idStr, req_or_resp_str) {
    const protocolItem = document.getElementById(idStr);

    let root = {};
    // 把所有属于请求配置的控件中的值提取并组装
    protocolItem.querySelector('.protocol-details').querySelectorAll(`.${req_or_resp_str}`).forEach(field => {
        const key = field.dataset.fieldName;
        // 值的来源可能是textContent 也可能是缓存
        let val;

        // 注意: 这里涉及的问题 不同对话框间的大数据怎么缓存的问题?
        // 1. dataset
        // 2. localcache
        // 3. localDb

        const valueElement = field.querySelector('.value');
        if(valueElement) {

            val = valueElement.textContent;
        }

            // if(valueElement.id) {
            //     const div = document.getElementById(valueElement.id);
            //     if(div) {
            //         val = JSON.parse(div.dataset.cacheData);
            //     }
            // } else if() {
            //     val = valueElement.textContent;

            // } else {
            //     console.error('valueElement 获取值失败!');
            //     return;
            // }

        if(key && val) {
            root[key] = val;
        } 

    });

    console.log('getCurProtocolItemCfg: ', root);

    return root;
}



async function updateProtocolBody(idStr, req_or_resp, protocolType, newBodyType, newBody) {
    const protocolItemId = ExtractId(idStr);
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.dataset.projectId);

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        const ok = await updateProtocolBodyReq(protocolItemId, serviceCardId, req_or_resp, protocolType, newBodyType, newBody);
        if(!ok) {
            throw new Error("修改协议项请求体失败!");
        }

    } catch(error) {
        console.error('修改协议项请求体失败!');
        return false;
    }

    return true;
}


async function updateProtocolTcpFuncCode(protocolItemId, newCode) {
    const idStr = `protocol-item-${protocolItemId}`;
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.dataset.projectId);

    // 获取出来所有req_cfg  返回一个JSON结构
    // 不同协议获取的JSON结构不同
    let req_cfg_json = getCurProtocolItemCfg(idStr, 'req-cfg');
    req_cfg_json["path"] = newPath;
    console.info('req_cfg_json: ', req_cfg_json);

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        // !!!!!!! cfg配置项更新有问题，怎么做到json路径级别更新??
        //  sqlite3支持 3.22以上版本 性能比较有限
        //  posgreSQL支持
        //  MySQL支持
        const ok = await updateProtocolCfgReq(protocolItemId, serviceCardId, 'HTTP', req_cfg_json);
        if(!ok) {
            throw new Error("http修改路径失败");
        }

    } catch {
        console.error('http修改路径失败!');
        return false;
    }

    return true;
};


async function updateProtocolHttpUrl(protocolItemId, newPath) {
    const idStr = `protocol-item-${protocolItemId}`;
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.dataset.projectId);

    // 获取出来所有req_cfg  返回一个JSON结构
    // 不同协议获取的JSON结构不同
    let req_cfg_json = getCurProtocolItemCfg(idStr, 'req-cfg');
    req_cfg_json["path"] = newPath;
    console.info('req_cfg_json: ', req_cfg_json);

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        // !!!!!!! cfg配置项更新有问题，怎么做到json路径级别更新??
        //  sqlite3支持 3.22以上版本 性能比较有限
        //  posgreSQL支持
        //  MySQL支持
        const ok = await updateProtocolCfgReq(protocolItemId, serviceCardId, 'HTTP', req_cfg_json);
        if(!ok) {
            throw new Error("http修改路径失败");
        }

    } catch {
        console.error('http修改路径失败!');
        return false;
    }

    return true;
};


async function updateProtocolHttpMethod(protocolItemId, newMethod) {
    const idStr = `protocol-item-${protocolItemId}`;
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.dataset.projectId);

    console.info(idStr + ' :: ' + protocolItemId, protocolItem.dataset.projectId + '-' + serviceCardId);
    // 获取出来所有req_cfg  返回一个JSON结构
    // 不同协议获取的JSON结构不同
    let req_cfg_json = getCurProtocolItemCfg(idStr, 'req-cfg');
    req_cfg_json["method"] = newMethod;

    // !! 暂时不更换为json路径级别更新
    // let req_cfg_json = {
    //     "method": newMethod
    // };

    console.info('req_cfg_json: ', req_cfg_json);

    try {

        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        const ok = await updateProtocolCfgReq(protocolItemId, serviceCardId, 'HTTP', req_cfg_json);
        if(!ok) {
            throw new Error("http修改请求方法失败");
        }

    } catch {
        console.error('http修改请求方法失败!');
        return false;
    }

    return true;
};


// 按json字段更新
async function updateProtocolCfg(protocolItemId, req_or_resp, key, newValue) {
    const idStr = `protocol-item-${protocolItemId}`;
    const protocolItem = document.getElementById(idStr);
    const projectId = ExtractId(protocolItem.dataset.projectId);

    console.info(idStr + ' :: ' + protocolItemId, protocolItem.dataset.projectId + '-' + projectId);
    // 获取出来所有req_cfg  返回一个JSON结构
    // 不同协议获取的JSON结构不同

    // !! 暂时不更换为json路径级别更新
    // let req_cfg_json = {
    //     "method": newMethod
    // };

    try {

        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }


        if(req_or_resp != 1 &&
            req_or_resp != 2)  {
            throw new Error("req_or_resp fail");
        }

        let root = {};
        root[key] = newValue;

        const ok = await updateProtocolCfgReq(protocolItemId, projectId, req_or_resp, root);
        if(!ok) {
            throw new Error("修改协议项配置数据失败");
        }

    } catch(e) {
        console.error('修改协议项配置数据失败! ', e.message);
        return false;
    }

    return true;
};



// 发起修改协议名称请求
async function updateProtocolNameReq(protocol_id, name) {

    try {
        await KitProxy.api.updateProtocolName(protocol_id, name);
        console.log('修改协议项标题请求成功');
        return true;

    } catch (error) {
        console.error('修改协议项标题请求失败:', error.message);
        return false;
    }

}



async function updateProtocolName(id_str, tilte_name) {
    const protocolItemId = ExtractId(id_str)

    const protocolItem = document.getElementById(id_str);

    const loading = showLoading();

    try {

        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        const ok = await updateProtocolNameReq(protocolItemId, tilte_name);
        if(!ok) {
            throw Error("修改测试服务标题发起请求失败");
        }

        // 动态更新页面数据
        protocolItem.querySelector(".protocol-name").textContent = tilte_name;

    } catch(error) {
        console.error('修改测试服务标题失败:', error);
        hideLoading(loading);
        return false;
    } 


    await delay(500);
    hideLoading(loading);
    return true;
}

function bindProtocolTitleEdit(protocolItem) {
    const protocolTitle = protocolItem.querySelector('.protocol-name.editable');
    if (!protocolTitle) return;

    const editControls = document.createElement('div');
    editControls.className = 'title-edit-controls';
    editControls.style.display = 'none';
    editControls.innerHTML = `
        <button class="save-btn">✓</button>
        <button class="cancel-btn">✗</button>
    `;
    protocolTitle.parentNode.appendChild(editControls);

    let originalTitle = protocolTitle.textContent;

    function exitEditMode(save = false) {
        if (!save) {
            protocolTitle.textContent = originalTitle;
        }
        protocolTitle.contentEditable = false;
        editControls.style.display = 'none';
    }

    protocolTitle.addEventListener('click', function(e) {
        e.stopPropagation();
        if (e.target === protocolTitle && !protocolTitle.isContentEditable) {

            originalTitle = protocolTitle.textContent;
            protocolTitle.contentEditable = true;
            protocolTitle.focus();
            editControls.style.display = 'flex';
        }
    });

    editControls.querySelector('.save-btn').addEventListener('click', async function(e) {
        e.preventDefault();
        e.stopPropagation();

        const ok = await updateProtocolName(protocolItem.id, protocolTitle.textContent);
        if(ok) {
            alert('标题编辑成功！');
        } else {
            protocolTitle.textContent = originalTitle;
            alert('标题编辑失败!');
        }

        exitEditMode(true);
    });

    editControls.querySelector('.cancel-btn').addEventListener('click', function(e) {
        e.stopPropagation();
        exitEditMode(false);
    });

    document.addEventListener('click', function(e) {
        if (protocolTitle.isContentEditable && !protocolTitle.contains(e.target) && !editControls.contains(e.target)) {
            exitEditMode(false);
        }
    });
}

function bindProtocolDeleteAction(protocolItem) {
    const deleteBtn = protocolItem.querySelector('.delete-protocol-btn');
    if (!deleteBtn) return;

    deleteBtn.addEventListener('click', async function(e) {
        e.stopPropagation();
        if (confirm('确定要删除这个协议吗？')) {
            
            const ok = await delProtocol(protocolItem.id);
            if(true === ok) {
                if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.handleProtocolDeleted === 'function') {
                    await KitProxy.protocolItemsPage.handleProtocolDeleted(protocolItem);
                } else {
                    protocolItem.remove();
                }
                alert('删除协议项成功!');

            } else {
                alert('删除协议项失败!');
            }
        }
    });
}

function bindProtocolBodyEditor(protocolItem) {
    protocolItem.querySelectorAll('.protocol-field.request-body, .protocol-field.response-body').forEach(bodyField => {
        bodyField.style.cursor = 'pointer';

        bodyField.addEventListener('click', async function(e) {
            
            e.stopPropagation();
            e.preventDefault();
                    
            const req_or_resp = this.className.includes('request') ? 1 : 2;

            const loading = showLoading('加载请求体信息...');

            const valueElement = this.querySelector('.value');
            const bodyIndicator = this.querySelector('.body-indicator');
            if (!valueElement || !bodyIndicator) {
                hideLoading(loading);
                return;
            }
            
            let body_type;
            let body_data;
            try {
                [body_type, body_data] = await getProtocolItemBody(protocolItem.id, req_or_resp);
            } catch(error) {
                console.error('获取协议请求体信息失败: ', error);
                hideLoading(loading);
                alert('获取协议请求体信息失败!');
                return;
            }

            await delay(500);
            hideLoading(loading);
 
            console.log('click body: ', this.className, ',', req_or_resp);
            
            createProtocolItemBodyModal(body_type, body_data, async function(newBodyType, newBody) {

                console.info("newBody.length: ", newBody.length);
                console.info("newBody: ", newBody);
        
                const protocolType = protocolItem.dataset.protocolType || 'HTTP';
                const ok = await updateProtocolBody(protocolItem.id, req_or_resp, protocolType, newBodyType, newBody);
                if(!ok) {
                    alert("修改协议请求体信息失败!");
                    return;
                }
        
                if (newBody) {
                    valueElement.textContent = '已设置';
                    bodyIndicator.classList.add('has');
                    bodyIndicator.classList.remove('no');
                } else {
                    valueElement.textContent = '未设置';
                    bodyIndicator.classList.remove('has');
                    bodyIndicator.classList.add('no');
                }
            });
        });
    });
}


// 页面展示添加协议项
function addProtocolItem(serviceCard, protocol, pos = -1) {

    const protocolList = serviceCard.querySelector('.protocol-list');
    if (!protocolList) return null;

    const protocolItem = document.createElement('div');
    protocolItem.className = `protocol-item ${protocol.type.toLowerCase()}`;
    protocolItem.id = `protocol-item-${protocol.id}`;
    protocolItem.dataset.projectId = serviceCard.id; //使用dataset存储
    protocolItem.dataset.protocolType = protocol.type;

    // 注意: 这里只需改变卡片内部子项的呈现，不需要更改整个布局
    protocolItem.innerHTML =`
        <div class="protocol-header">
            <div>
                <span class="protocol-tag ${protocol.type.toLowerCase()}">${protocol.type}</span>
                <span class="protocol-name editable" data-default="Undef默认测试协议项">${protocol.name}</span>
                <div class="protocol-time">
                    <span class="last-update-time">最后一次修改时间: ${protocol.utime}</span>
                    <span class="create-time">创建时间: ${protocol.ctime}</span>
                </div>
            </div>
        </div>
        <div class="protocol-details">
            <div class="details-grid"> </div>
            <div class="protocol-actions-container">
                <button class="delete-protocol-btn">删除协议</button>
            </div>
        </div>
`;

    // 根据协议类型注册表生成不同协议项的详情网格。
    const oldDetailsGrid = protocolItem.querySelector('.details-grid');
    oldDetailsGrid.replaceWith(ProtocolTypeRegistry.createProtocolItemGrid(protocol));

    bindProtocolTitleEdit(protocolItem);
    bindProtocolDeleteAction(protocolItem);

    // 将协议项卡片插入到列表中
    if(-1 === pos) {
        protocolList.appendChild(protocolItem);
    } else {
        protocolList.insertBefore(protocolItem, protocolList.children[pos] || null);
    }

    // (暂不实现)更新协议数量统计
    // const protocolCount = protocolList.querySelectorAll('.protocol-item').length;
    // serviceCard.querySelector('.project-protocol-cnt').textContent = protocolCount;



    bindProtocolBodyEditor(protocolItem);
    return protocolItem;

}


// 获取协议项列表
async function getProtocolList(project_id, offset = 0, limit = 10) {

    try {
        const protocols = await KitProxy.api.getProtocolList(project_id, offset, limit);
        console.log('获取协议项列表请求成功:', protocols);
        return protocols;
    } catch (error) {
        console.error('获取协议项列表请求出错:', error);
        throw error;
    }
}


// 获取单个协议项请求
async function getProtocolReq(protocol_id) {
    try {
        const protocols = await KitProxy.api.getProtocol(protocol_id);

        if(!Array.isArray(protocols) || protocols.length <= 0) {
            throw new Error('获取单个协议项请求为null');
        }
 
        console.log('获取单个协议项请求成功:', protocols);

        return protocols;
    } catch (error) {
        console.error('获取单个协议项请求失败:', error);
        throw error;
    }
}

// 表单body数据辅助组装
function formBodyDataHeler(formData, body, body_type, body_data) {

    if(body_data.length <= 0)
        return;

    const body_key = body === 1 ? 'protocol_req_body' : 'protocol_resp_body';
    let root;
    let body_value = '';
    
    console.info('body_data: ', body_data);

    if(body_type.includes('json') && body_data[0] === '{') {
        root = JSON.parse(body_data || ''); // TODO 校验放在输入阶段
        body_value = JSON.stringify(root);
    } else if(body_type.includes('xml')) {
        body_value = new XMLSerializer(body_data);
    } else if(body_type.includes('binary')) {
        body_value = new Blob([body_data])
    }

    console.info('bady_value: ', body_value);

    formData.append(body_key, body_value);
}


async function  addHTTPProtocolReq(protocol) {

    try {
        const addResult = await KitProxy.api.addProtocol(protocol);
        console.log('协议项添加请求成功:', addResult);
        return addResult;
    } catch(error) {
        console.error('添加协议项请求失败:', error);

        throw error;
    }

}


// 添加HTTP协议项
async function  addHTTPProtocol(serviceCard, submit_protocol) {

    const loading = showLoading();
    let protocols;
    try {

        // 1. 先添加服务
        const addResult = await addHTTPProtocolReq(submit_protocol);
        console.info('添加完成的单个协议项id:', addResult.protocol_id);
        
        // 2. 再获取单个项
        protocols = await getProtocolReq(addResult.protocol_id);
        console.info('获取的单个协议项:', protocols);

    } catch (error) {
        console.error('添加测试协议项失败: ', error);
        hideLoading(loading);
        alert('添加测试协议项失败!');
        return;
    }

    await delay(1000);
    hideLoading(loading);
    alert("添加测试协议项成功!");

    // 3. 添加到页面显示；协议项独立页需要回到第一页并刷新当前列表。
    if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.handleProtocolAdded === 'function') {
        await KitProxy.protocolItemsPage.handleProtocolAdded(protocols[0]);
    } else {
        addProtocolItem(serviceCard, protocols[0], 1);
    }

}

function getDefaultTcpPatternInfo() {
    // 新建 TCP 格式时的兜底字段，保持和旧页面默认值一致。
    return {
        least_byte_len: 26,
        special_fields: {
            start_magic_num_field: {
                name: '起始标识',
                idx: 0,
                byte_pos: 0,
                byte_len: 4,
                type: 'INT32',
                value: 'H23232323',
            },
            function_code_field: {
                name: '功能码',
                idx: 3,
                byte_pos: 12,
                byte_len: 2,
                type: 'UINT16',
                value: '',
            },
            body_length_field: {
                name: '报文体长度',
                idx: 4,
                byte_pos: 14,
                byte_len: 4,
                type: 'UINT32',
                value: '',
            },
        },
    };
}

function buildTcpPatternModalInput(patternInfoText) {
    const patternInfo = patternInfoText ? JSON.parse(patternInfoText) : getDefaultTcpPatternInfo();
    const inputPatternInfosMap = {};

    inputPatternInfosMap.least_byte_len = patternInfo.least_byte_len;

    const specialFields = patternInfo.special_fields;
    if (specialFields) {
        inputPatternInfosMap.special_fields = {};
        Object.keys(specialFields).forEach(key => {
            inputPatternInfosMap.special_fields[key] = createPatternField('', key, specialFields[key]);
        });
    }

    return inputPatternInfosMap;
}

function openTcpPatternConfig(targetField, statusElement) {
    const patternInfoText = targetField.dataset.patternInfos || JSON.stringify(getDefaultTcpPatternInfo());
    const inputPatternInfosMap = buildTcpPatternModalInput(patternInfoText);

    console.log('specialPatternFieldMap: ', inputPatternInfosMap);
    createCustomTcpPatternModal(targetField, '头部特殊字段', inputPatternInfosMap, statusElement, true);
}

function bindTcpPatternTypeControls(selectElement, configButton, statusElement, initialType = 0) {
    // Pattern 类型切换会清空已配置内容，这个规则在“新增服务”和“编辑服务”里保持一致。
    let previousPatternType = Number(initialType || 0);

    selectElement.addEventListener('change', function() {
        const type = Number(this.value);
        const targetField = configButton;

        if (previousPatternType !== type && targetField.dataset.patternInfos) {
            if(!confirm('格式已配置, 切换会清空，是否继续?')) {
                this.value = String(previousPatternType);
                return;
            }

            delete targetField.dataset.patternInfos;
            if (statusElement) {
                statusElement.style.display = 'none';
            }
        }

        previousPatternType = type;
        configButton.disabled = type === PatternType.STANDARD;
    });

    configButton.disabled = Number(selectElement.value || 0) === PatternType.STANDARD;
    configButton.addEventListener('click', function() {
        openTcpPatternConfig(configButton, statusElement);
    });
}

function tcpPatternControlHTML() {
    return `
        <div class="pattern-header">
            <label for="pattern-type">格式类型</label>
            <scan class="import-status" id="first-pattern-import-status" style="display: none">格式已设置</scan>
        </div>
        <div class="pattern-container">
            <select id="pattern-type" required>
                <option value="">请选择格式类型</option>
                <option value=1>${PatternTypeStr[1]}</option>
                <option value=2>${PatternTypeStr[2]}</option>
                <option value=3>${PatternTypeStr[3]}</option>
            </select>
            <button type="button" class="pattern-config-btn" id="pattern-infos">配置</button>
        </div>
    `;
}

// 添加测试服务模态框
function addNewServiceCardModalHTML() {
    return`<div class="add-service-modal">
            <div class="modal-header">
                <h3>添加新测试服务</h3>
                <button class="close-modal">&times;</button>
            </div>
            <div class="modal-body">
                <form id="add-service-form">
                    <div class="form-columns-container">
                        <div class="form-group">
                            <label for="service-name">服务名称</label>
                            <input type="text" id="service-name" placeholder="输入服务名称" required>
                        </div>

                        <div class="form-group">
                            <label for="protocol-type">协议类型</label>
                            <select id="protocol-type" required>
                                <option value="">请选择协议类型</option>
                                <option value=1>HTTP</option>
                                <option value=2>TCP</option>
                                <!-- <option value=3>HTTPS</option> -->
                            </select>
                        </div>

                        <div class="form-group">
                            <label for="service-mode">测试模式</label>
                            <select id="service-mode" required>
                                <option value="">请选择测试模式</option>
                                <option value=1>服务器模式</option>
                                <!-- <option value=2>客户端模式</option> -->
                            </select>
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
}

function renderServiceEndpointControl(formContainer, mode) {
    const existingPortControl = formContainer.querySelector('.port-control');
    if (existingPortControl) {
        formContainer.removeChild(existingPortControl);
    }

    if (mode === ProjectMode.SERVER) {
        const portControl = document.createElement('div');
        portControl.className = 'form-group port-control';
        portControl.innerHTML = `
            <label for="service-port">监听端口</label>
            <input type="number" id="service-port" min="1" max="65535" placeholder="1-65535" required>
        `;
        formContainer.appendChild(portControl);
    } else if (mode === ProjectMode.CLIENT) {
        const portControl = document.createElement('div');
        portControl.className = 'form-group port-control';
        portControl.innerHTML = `
            <label for="target-address">目标IP/端口</label>
            <input type="text" id="target-address" 
                placeholder="IP:端口" 
                pattern="^((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d|[01]?\\d\\d?):([1-9]\\d{0,3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])$"
                title="请输入有效的IP地址和端口号，格式如：192.168.1.1:8080"
                required>
            <div class="error-message" style="color:red;font-size:12px;display:none"></div>
        `;
        formContainer.appendChild(portControl);

        const addressInput = portControl.querySelector('#target-address');
        const errorMessage = portControl.querySelector('.error-message');
        
        addressInput.addEventListener('input', function() {
            if (!KitProxy.utils.validateIpPort(this.value)) {
                errorMessage.textContent = '请输入有效的IP:端口格式，如：192.168.1.1:8080';
                errorMessage.style.display = 'block';
                this.setCustomValidity('IP地址或端口号格式不正确');
            } else {
                errorMessage.style.display = 'none';
                this.setCustomValidity('');
            }
        });
    }
}

function renderProjectPatternControl(formContainer, protocolType) {
    ProtocolTypeRegistry.renderAddServiceExtraControl(formContainer, protocolType);
}

function bindAddServiceDynamicControls(modal) {
    const formContainer = modal.querySelector('.form-columns-container');
    const serviceModeSelect = modal.querySelector('#service-mode');
    const protocolTypeSelect = modal.querySelector('#protocol-type');

    if (serviceModeSelect && formContainer) {
        serviceModeSelect.addEventListener('change', function() {
            renderServiceEndpointControl(formContainer, Number(this.value));
        });
    }

    if (protocolTypeSelect && formContainer) {
        protocolTypeSelect.addEventListener('change', function() {
            const type = Number(this.value);
            console.info('选择的协议类型: ', type);
            renderProjectPatternControl(formContainer, type);
        });
    }
}

function collectAddServicePayload(modal) {
    const serviceName = modal.querySelector('#service-name').value;
    const protocolType = Number(modal.querySelector('#protocol-type').value);
    const serviceMode = Number(modal.querySelector('#service-mode').value);
    let servicePort = 0;
    let targetIp = '';

    if(!serviceName.trim()) {
        throw new Error('服务名称不能为空');
    }

    if(!protocolType) {
        throw new Error('请选择协议类型');
    }

    if(!serviceMode) {
        throw new Error('请选择测试模式');
    }

    if (serviceMode === ProjectMode.SERVER) {
        const portInput = modal.querySelector('#service-port');
        servicePort = portInput ? Number(portInput.value) : 0;
        if(!KitProxy.utils.validatePort(servicePort)) {
            throw new Error('监听端口必须是 1-65535 的整数');
        }
    } else if (serviceMode === ProjectMode.CLIENT) {
        const addressInput = modal.querySelector('#target-address');

        if (!addressInput || !addressInput.reportValidity()) {
            throw new Error('请输入有效的IP:端口格式，如：192.168.1.1:8080');
        }
        if(!KitProxy.utils.validateIpPort(addressInput.value)) {
            throw new Error('请输入有效的IP:端口格式，如：192.168.1.1:8080');
        }
        targetIp = addressInput.value;
    }

    const extraPayload = ProtocolTypeRegistry.collectAddServiceExtraPayload(modal, protocolType);

    return {
        name: serviceName,
        mode: serviceMode,
        protocol_type: protocolType,
        listen_port: servicePort,
        target_ip: targetIp,
        pattern_type: extraPayload.pattern_type,
        pattern_info: extraPayload.pattern_info,
    };
}



async function updateTcpPatternInfoReq(pattern_type, pattern_fields) {
    // 当前后端接口规划里尚未明确“更新 TCP Pattern”的独立路径，V1 先保持原有占位行为。
    console.info('updateTcpPatternInfoReq placeholder:', pattern_type, pattern_fields);
    return true;
}

async function getTcpPatternInfoReq(project_id) {
    
    try {
        const patternInfo = await KitProxy.api.getProjectPatternInfo(project_id);
        console.log('获取TCP格式信息请求成功:', patternInfo);
        return patternInfo;

    }catch(error) {
        console.error('获取TCP格式信息请求失败:', error);
        throw error;
    }

}

async function updateProjectNameReq(project_id, name) {

    try {
        await KitProxy.api.updateProjectName(project_id, name);
        console.log('修改测试服务标题请求成功');

    } catch (error) {
        console.error('修改测试服务标题请求失败:', error.message);
        throw error;
    }

}

async function updateProjectName(id_str, tilte_name) {
    const serviceCardId = ExtractId(id_str)

    const serviceCard = document.getElementById(id_str);
    let projects;

    const loading = showLoading();

    try {

        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }

        await updateProjectNameReq(serviceCardId, tilte_name);

        projects = await getProjectReq(serviceCardId);

        // 动态更新页面数据
        updateServiceCard(id_str, projects[0]) 

    } catch(error) {
        console.error('修改测试服务标题失败:', error);
        hideLoading(loading);
        return false;
    } 


    await delay(500);
    hideLoading(loading);
    return true;
}

// 添加测试服务 http请求
async function addProjectReq(project) {
    
    try {

        console.info('addProjectReq body: ', project);
        const addResult = await KitProxy.api.addProject(project);
        console.log('服务添加成功:', addResult);
        return addResult;

    } catch (error) {
        console.error('添加服务请求出错:', error.message);
        throw error;
    }
}

// 获取项目列表
async function getProjectList(offset, limit) {
    try {
        const projects = await KitProxy.api.getProjectList(offset, limit);
        console.log('获取测试服务列表成功:', projects);
        return projects;
    } catch (error) {
        console.error('获取项目列表出错:', error);
        throw error;
    }
}

// 获取单个测试服务 http请求
async function getProjectReq(project_id) {

    try {
        const projects = await KitProxy.api.getProject(project_id);

        if(!Array.isArray(projects) || projects.length <= 0) {
            throw new Error('获取单个测试服务请求为null');
        }
        console.log("projects.length: ", projects.length);
        
        console.log('获取单个测试服务请求成功:', projects);

        return projects;
    } catch (error) {
        console.error('获取单个测试服务请求失败:', error);
        throw error;
    }
}

// 删除单个测试服务 http请求
async function delProtocolReq(protocol_id, project_id) {

    try {
        await KitProxy.api.deleteProtocol(protocol_id, project_id);
        console.log('删除协议项成功');

    } catch (error) {
        console.error('删除协议项请求出错:', error);
        throw error;
    }
}

async function delProtocol(id_str) {
    // 获取当前测试服务卡片ID
    const protocolItemId = ExtractId(id_str);
    const protocolItem = document.getElementById(id_str);
    const serviceCardId = ExtractId(protocolItem.dataset.projectId);

    const loading = showLoading();

    try {

        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        await delProtocolReq(protocolItemId, serviceCardId);

    } catch(error) {

        console.error('删除协议项失败:', error);

        hideLoading(loading);
        return false;
    } 

    await delay(500);
    hideLoading(loading);
    return true;

}



// 删除单个测试服务 http请求
async function delProjectReq(project_id) {

    try {
        await KitProxy.api.deleteProject(project_id);
        console.log('删除测试服务成功');

        return true;
    } catch (error) {
        console.error('删除测试服务请求出错:', error);
        throw error;
    }
}

async function delProject(id_str) {
    // 获取当前测试服务卡片ID
    const serviceCardId = ExtractId(id_str);

    const serviceCard = document.getElementById(id_str);


    const loading = showLoading();

    try {
        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }
        
        await delProjectReq(serviceCardId);

    } catch(error) {
        console.error('删除测试服务失败:', error);
        hideLoading(loading);
        return false;
    } 

    await delay(500);
    hideLoading(loading);
    return true;

}

async function addProject(project) {

    const loading = showLoading();

    try {
        // 1. 先添加服务
        const addResult = await addProjectReq(project);
        console.info('添加完成的单个项目id:', addResult.project_id);

        // 2. 再获取单个项
        project = await getProjectReq(addResult.project_id);
        console.info('获取的单个项目:', project);

    } catch (error) {
        console.error('添加测试服务失败: ', error);
        hideLoading(loading);
        alert('添加测试服务失败!');
        return;
    }

    await delay(1000);
    hideLoading(loading);
    alert("添加测试服务成功!");
    await loadAllProjects(1);
    
}


// 组装卡片页面
/**
 * @param {any} id
 * @param {any} name
 * @param {number} protocol
 * @param {any} port
 * @param {number} mode
 * @param {string | number} pattern_type
 */
function serviceCardHTML(id, name, protocol, port, mode, pattern_type, status = false) {


    console.info(id, name, protocol, port, mode, pattern_type, status);

    const project = {
        id,
        protocol_type: protocol,
        pattern_type,
    };

return `<div class="service-header">
            <h3 class="service-title editable" data-default="Undef默认测试服务">${name || `默认测试服务${id}`}</h3>
            <div class="title-edit-controls" style="display:none">
                <button class="save-btn">✓</button>
                <button class="cancel-btn">✗</button>
            </div>
        </div>
            <div class="service-form">
            <div class="service-fields-container">
                <div class="service-field project-protocol-type" data-protocol_type='${protocol}'>
                    <span class="field-label">协议种类</span>
                    <span class="field-value">${ProtocolTypeStr[protocol]}</span>
                </div>
                <div class="service-field project-mode">
                    <span class="field-label">测试模式</span>
                    <span class="field-value">${ProjectModeStr[mode]}</span>
                </div>
                <div class="service-field project-${mode === ProjectMode.SERVER ? 'listen-port' : 'target-ip'}">
                    <span class="field-label">${mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口'}</span>
                    <span class="field-value">${port || '未设置'}</span>
                </div>
                <div class="service-field project-protocol-cnt" style="display: none;">
                <span class="field-label">协议数量</span>
                <span class="field-value">0</span>
                </div>
                <div class="service-field project-status">
                    <span class="field-label">服务状态</span>
                    <span class="field-value status ${status ? 'status-active' : 'status-inactive'}">
                        ${status ? '开启' : '未开启'}
                    </span>
                </div>
                ${ProtocolTypeRegistry.serviceExtraFieldsHTML(project)}
            </div>
            <div class="service-actions-container">
                <button class="view-protocols-btn">查看协议项</button>
                <button class="delete-service-btn">删除</button>
            </div>
        </div>`
}



// 动态更新页面数据
function updateServiceCard(id_str, project) {

    // 字段转换
    let mode_str;
    let protocol_str;

    console.info('updateServiceCard: ', project.id, project.name, project.protocol_type, project.listen_port, project.mode, project.status);


    if(1 === project.mode) {
        mode_str = '服务器模式'
    }else if (2 === project.mode) {
        mode_str = '客户端模式'
    } else {
        mode_str = '未知模式'
    }

    if(1 === project.protocol_type) {
        protocol_str = 'HTTP'
    }else if(2 ===  project.protocol_type) {
        protocol_str = '自定义TCP'
    } else if(3 ===  project.protocol_type){
        protocol_str = 'HTTPS'
    } else {
        protocol_str = "未知协议"
    }

    const serviceCard = document.getElementById(id_str);

    serviceCard.querySelector(".service-title").textContent = project.name;

    // TODO: 待考虑 是否能修改
    // serviceCard.querySelector(".project-protocol-type .field-value").textContent = protocol_str;
    // serviceCard.querySelector(".project-mode .field-value").textContent = mode_str;


    // if(1 === project.mode) {
    //     serviceCard.querySelector(".project-listen-port .field-value").textContent = project.listen_port || '未设置';
    // } else if(2 === project.mode) {
    //     serviceCard.querySelector(".project-target-ip .field-value").textContent = project.target_ip || '未设置';
    // }
    // serviceCard.querySelector(".project-protocol-cnt .field-value").textContent = project.protocol_cnt || '0';
    // TODO: 待考虑 是否能修改

    serviceCard.querySelector(".project-status .field-value").textContent = project.status === 1 ? '开启' : '未开启';
    serviceCard.querySelector(".project-status .field-value").className = `field-value status ${project.status === 1 ? 'status-active' : 'status-inactive'}`;
    
}

/**
 * @param {HTMLDivElement} serviceCard
 */
function bindServiceCardToggle(serviceCard) {
    const toggleIcon = serviceCard.querySelector('.toggle-icon');
    if (!toggleIcon) return;

    toggleIcon.addEventListener('click', function(e) {
        e.stopPropagation();
        serviceCard.classList.toggle('expanded');
        toggleIcon.textContent = serviceCard.classList.contains('expanded') ? '▲' : '▼';
    });
}

/**
 * 生成协议项页 URL，并保留 mock/后端调试参数。
 * @param {number | string} projectId
 * @returns {string}
 */
function buildProtocolItemsUrl(projectId) {
    const params = new URLSearchParams(window.location.search);
    params.set('projectId', String(projectId));
    return `protocol_items.html?${params.toString()}`;
}

/**
 * 服务页点击服务卡片或按钮时进入该服务的协议项页。
 * @param {HTMLDivElement} serviceCard
 * @param {any} project
 */
function bindOpenProtocolItemsAction(serviceCard, project) {
    function openProtocolItems() {
        window.location.href = buildProtocolItemsUrl(project.id);
    }

    const viewBtn = serviceCard.querySelector('.view-protocols-btn');
    if (viewBtn) {
        viewBtn.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            openProtocolItems();
        });
    }

    serviceCard.addEventListener('click', function(e) {
        const ignored = e.target.closest('button, .service-title, .title-edit-controls, .project-pattern');
        if (ignored) return;
        openProtocolItems();
    });
}

function bindServiceDeleteAction(serviceCard) {
    const deleteBtn = serviceCard.querySelector('.delete-service-btn');
    if (!deleteBtn) return;

    deleteBtn.addEventListener('click', async function(e) {
        e.stopPropagation();
        if (confirm('确定要删除这个测试服务吗？')) {

            const ok = await delProject(serviceCard.id);
            if(true === ok) {
                const visibleCountBeforeDelete = document.querySelectorAll('.service-cards .service-card').length;
                const nextPage = KitProxy.pagination.nextPageAfterDelete(servicePageState, visibleCountBeforeDelete);
                await loadAllProjects(nextPage);
                alert('删除测试服务成功!');
            } else {
                alert('删除测试服务失败!');
            }
        }
    });
}

function bindAddProtocolAction(serviceCard, project) {
    const addProtocolBtn = serviceCard.querySelector('.add-protocol-btn');
    if (!addProtocolBtn) return;

    addProtocolBtn.addEventListener('click', function(e) {
        e.stopPropagation();

        // 根据不同协议类型生成不同的框
        const modal = createAddProtocolModal(ProtocolTypeRegistry.getAddProtocolModal(project.protocol_type), serviceCard);
        if(modal) {
            document.body.appendChild(modal);
        }
    });
}

function insertServiceCard(serviceCard, pos = -1) {
    const serviceCards = document.querySelector('.service-cards');
    if(!serviceCards) return;

    if(-1 === pos) {
        serviceCards.appendChild(serviceCard);
    } else if(pos >= 1) {
        serviceCards.insertBefore(serviceCard, serviceCards.childNodes[pos]);
    }
}


/** 态生成测试服务卡片页面 + 事件监听
 * @param {{ id: string; name: any; protocol_type: number; listen_port: any; mode: any; pattern_type: any; status: boolean | undefined; }} project
 */
function addServiceCard(project, pos = -1) {

    const serviceCard = document.createElement('div');
    serviceCard.className = 'service-card';
    serviceCard.id = String("service-card-" + project.id);

    serviceCard.innerHTML = serviceCardHTML(project.id, project.name, project.protocol_type, project.listen_port, project.mode, project.pattern_type, project.status);

    /* 标题编辑功能 */
    const titleElement = serviceCard.querySelector('.service-title');
    const editControls = serviceCard.querySelector('.title-edit-controls');
    let originalTitle = titleElement.textContent;

    function exitEditMode(save = false) {
        if (!save) {
            titleElement.textContent = originalTitle;
        }
        titleElement.contentEditable = false;
        editControls.style.display = 'none';
    }

    titleElement.addEventListener('click', function(e) {
        e.stopPropagation();
        if (e.target === titleElement && !titleElement.isContentEditable) {
            originalTitle = titleElement.textContent;
            titleElement.contentEditable = true;
            titleElement.focus();
            editControls.style.display = 'flex';
        }
    });

    document.addEventListener('click', function(e) {
        if (titleElement.isContentEditable && !titleElement.contains(e.target) && !editControls.contains(e.target)) {
            exitEditMode(false);
        }
    });

    serviceCard.querySelector('.save-btn').addEventListener('click', async function(e) {
        e.stopPropagation();

        const ok = await updateProjectName(serviceCard.id, titleElement.textContent)
        if(ok) {
            alert('标题编辑成功！');
        } else {
            titleElement.textContent = originalTitle;
            alert('标题编辑失败!');
        }

        exitEditMode(true);
    });

    serviceCard.querySelector('.cancel-btn').addEventListener('click', function(e) {
        e.stopPropagation();
        exitEditMode(false);
    });

    // 阻止点击标题时展开卡片
    titleElement.addEventListener('click', function(e) {
        e.stopPropagation();
    });
    /* 标题编辑功能 */

    ProtocolTypeRegistry.bindServiceExtraActions(serviceCard, project);
    bindOpenProtocolItemsAction(serviceCard, project);
    bindServiceDeleteAction(serviceCard);
    insertServiceCard(serviceCard, pos);

    return serviceCard;
}

// 刷新所有页面状态
function refreshServiceCards(projects) {
    projects.forEach(project => {
        addServiceCard(project);
    });
}

/**
 * 渲染服务列表分页条。
 */
function renderServicePagination() {
    KitProxy.pagination.render(document.getElementById('service-pagination'), servicePageState, {
        onPrev: function() {
            loadAllProjects(servicePageState.currentPage - 1);
        },
        onNext: function() {
            loadAllProjects(servicePageState.currentPage + 1);
        },
    });
}

// 加载当前页测试服务数据
async function loadAllProjects(page = servicePageState.currentPage) {
    const serviceCards = document.querySelector('.service-cards');
    if (!serviceCards) return;

    const loading = showLoading("正在加载测试服务列表...");

    try {
        servicePageState.currentPage = Math.max(1, Number(page) || 1);

        // 获取测试服务列表
        const projects = await getProjectList(
            KitProxy.pagination.getOffset(servicePageState),
            KitProxy.pagination.getRequestLimit(servicePageState),
        );
        if(!Array.isArray(projects)) {
            throw new Error("数据格式错误");
        }

        console.info('获取的项目列表:', projects);

        // 更新页面显示
        serviceCards.innerHTML = '';
        refreshServiceCards(KitProxy.pagination.takeVisibleItems(projects, servicePageState));
        checkEmptyState();
        renderServicePagination();

    } catch(error) {
        console.error('加载服务列表出错:', error);
        alert('加载服务列表出错： ' + error.message);

    }finally{
        await delay(1000);
        hideLoading(loading);
    }

}


// 页面加载入口
document.addEventListener('DOMContentLoaded', function() {
    const serviceCards = document.querySelector('.service-cards');
    if (!serviceCards) return;

    // 监听添加服务按钮点击
    document.getElementById('add-service')?.addEventListener('click',  async function() {

        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.innerHTML = addNewServiceCardModalHTML();
        // 暂不考虑 模态框不需要重新再去获取
        // const modal = await loadModal('add_protocol_service.html');

        document.body.appendChild(modal);

        bindAddServiceDynamicControls(modal);

        // 处理表单提交
        modal.querySelector('#add-service-form').addEventListener('submit', async function(e) {
            e.preventDefault();

            let project;
            try {
                project = collectAddServicePayload(modal);
            } catch(error) {
                alert(error.message);
                return;
            }

            await addProject(project);

            KitProxy.utils.removeDomNode(modal);

        });

        KitProxy.utils.bindModalCloseActions(modal);
    });

    loadAllProjects();

});
