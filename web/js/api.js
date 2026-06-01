(function initKitProxyApi(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    // UI 层只调用 KitProxy.api；这里集中决定走 Mock 还是走真实后端。
    function isMockMode() {
        return KitProxy.config && KitProxy.config.apiMode === 'mock';
    }

    function apiUrl(path) {
        // apiBaseUrl 为空时保持同源部署；设置后可临时指向代理或后端调试地址。
        const baseUrl = KitProxy.config && KitProxy.config.apiBaseUrl ? KitProxy.config.apiBaseUrl : '';
        return `${baseUrl}${path}`;
    }

    async function parseJsonResponse(response, fallbackMessage) {
        // 后端业务协议统一为 { code, message, data }，这里把 HTTP 错误和业务错误收敛成异常。
        let result;

        try {
            result = await response.json();
        } catch (error) {
            if (!response.ok) {
                throw new Error(`${fallbackMessage || '请求失败'}: HTTP ${response.status}`);
            }
            throw new Error(`${fallbackMessage || '响应解析失败'}: ${error.message}`);
        }

        if (!response.ok) {
            throw new Error(result.message || `${fallbackMessage || '请求失败'}: HTTP ${response.status}`);
        }

        if (Number(result.code) !== 0) {
            throw new Error(result.message || `业务错误: code=${result.code}`);
        }

        return result.data;
    }

    async function requestJson(path, options, fallbackMessage) {
        // 普通 JSON 接口走这个入口；multipart 或二进制响应保留专门处理。
        const response = await fetch(apiUrl(path), options);
        return parseJsonResponse(response, fallbackMessage);
    }

    function normalizePatternInfoForBackend(patternInfo) {
        if (!patternInfo || Number(patternInfo.version) !== 2) return patternInfo;

        const normalized = Object.assign({}, patternInfo);
        if (normalized.byte_order && !normalized.default_order) {
            normalized.default_order = normalized.byte_order;
        }
        return normalized;
    }

    function normalizeProjectForBackend(project) {
        const normalized = Object.assign({}, project || {});
        if (normalized.pattern_info) {
            normalized.pattern_info = normalizePatternInfoForBackend(normalized.pattern_info);
        }
        return normalized;
    }

    function runMutation(key, action, message) {
        if (KitProxy.utils && typeof KitProxy.utils.runMutationOnce === 'function') {
            return KitProxy.utils.runMutationOnce(key, action, {
                message,
                mockVisibleDelayMs: 1200,
            });
        }

        return action();
    }

    const api = {
        isMockMode,
        apiUrl,
        async getProjectList(offset = 0, limit = 10) {
            if (isMockMode()) return KitProxy.mocks.getProjectList(offset, limit);

            return requestJson('/projects/list', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ offset, limit }),
            }, '获取测试服务列表失败');
        },
        async getProject(projectId) {
            if (isMockMode()) return KitProxy.mocks.getProject(projectId);

            return requestJson('/projects/' + String(projectId), {
                method: 'GET',
            }, '获取单个测试服务失败');
        },
        async addProject(project) {
            return runMutation('api-add-project', async function() {
                if (isMockMode()) return KitProxy.mocks.addProject(project);

                return requestJson('/projects/add', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify(normalizeProjectForBackend(project)),
                }, '添加测试服务失败');
            }, '正在添加测试服务...');
        },
        async setProjectActive(projectId, active) {
            return runMutation(`api-set-project-active-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.setProjectActive(projectId, active);

                const operation = active ? 1 : 0;
                return requestJson('/projects/' + String(projectId) + '/status?operation=' + String(operation), {
                    method: 'POST',
                }, active ? '启动测试服务失败' : '停止测试服务失败');
            }, active ? '正在启动测试服务...' : '正在停止测试服务...');
        },
        async updateProjectName(projectId, name) {
            return runMutation(`api-update-project-name-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProjectName(projectId, name);

                return requestJson('/projects/' + projectId + '/name', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({ name }),
                }, '修改测试服务标题失败');
            }, '正在保存测试服务名称...');
        },
        async deleteProject(projectId) {
            return runMutation(`api-delete-project-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.deleteProject(projectId);

                await requestJson('/projects/' + String(projectId), {
                    method: 'DELETE',
                }, '删除测试服务失败');

                return true;
            }, '正在删除测试服务...');
        },
        async getProjectPatternInfo(projectId) {
            if (isMockMode()) return KitProxy.mocks.getProjectPatternInfo(projectId);

            return requestJson('/projects/' + projectId + '/pattern_info', {
                method: 'GET',
            }, '获取TCP格式信息失败');
        },
        async updateProjectPatternInfo(projectId, patternInfo) {
            return runMutation(`api-update-project-pattern-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProjectPatternInfo(projectId, patternInfo);

                return requestJson('/projects/pattern_info', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: projectId,
                        pattern_info: normalizePatternInfoForBackend(patternInfo),
                    }),
                }, '修改TCP格式信息失败');
            }, '正在保存 TCP 格式信息...');
        },
        async getProtocolList(projectId, offset = 0, limit = 10) {
            if (isMockMode()) return KitProxy.mocks.getProtocolList(projectId, offset, limit);

            return requestJson('/protocols/list', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    project_id: projectId,
                    offset,
                    limit,
                }),
            }, '获取协议项列表失败');
        },
        async getProtocol(protocolId) {
            if (isMockMode()) return KitProxy.mocks.getProtocol(protocolId);

            return requestJson('/protocols/' + String(protocolId), {
                method: 'GET',
            }, '获取单个协议项失败');
        },
        async addProtocol(protocol) {
            const projectId = protocol && protocol.cfg_header ? protocol.cfg_header.project_id : 'unknown';
            return runMutation(`api-add-protocol-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.addProtocol(protocol);

                const formData = KitProxy.utils.createAddProtocolFormData(protocol);

                return requestJson('/protocols/add', {
                    method: 'POST',
                    body: formData,
                }, '添加协议项失败');
            }, '正在添加协议项...');
        },
        async updateProtocolName(protocolId, name) {
            return runMutation(`api-update-protocol-name-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProtocolName(protocolId, name);

                await requestJson('/protocols/name', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: protocolId,
                        name,
                    }),
                }, '修改协议项标题失败');

                return true;
            }, '正在保存协议项名称...');
        },
        async deleteProtocol(protocolId, projectId) {
            return runMutation(`api-delete-protocol-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.deleteProtocol(protocolId, projectId);

                await requestJson('/protocols/del', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: protocolId,
                        project_id: projectId,
                    }),
                }, '删除协议项失败');

                return true;
            }, '正在删除协议项...');
        },
        async updateProtocolCfg(protocolId, projectId, reqOrResp, cfgJson) {
            return runMutation(`api-update-protocol-cfg-${protocolId}-${reqOrResp}-${JSON.stringify(cfgJson || {})}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProtocolCfg(protocolId, reqOrResp, cfgJson);

                await requestJson('/protocols/details/cfg', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: protocolId,
                        project_id: projectId,
                        req_or_resp: reqOrResp,
                        cfg_data: cfgJson,
                    }),
                }, '修改协议项配置失败');

                return true;
            }, '正在保存协议项配置...');
        },
        async getProtocolDetailsCfg(protocolId) {
            if (isMockMode()) return KitProxy.mocks.getProtocolDetailsCfg(protocolId);

            return requestJson(`/protocols/${protocolId}/details/cfg`, {
                method: 'GET',
            }, '查询协议项配置失败');
        },
        async updateProtocolBody(protocolId, projectId, reqOrResp, protocolType, bodyType, body) {
            return runMutation(`api-update-protocol-body-${protocolId}-${reqOrResp}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProtocolBody(protocolId, reqOrResp, bodyType, body);

                const formData = KitProxy.utils.createProtocolBodyFormData(
                    protocolId,
                    projectId,
                    reqOrResp,
                    protocolType,
                    bodyType,
                    body,
                );

                await requestJson('/protocols/details/body', {
                    method: 'POST',
                    body: formData,
                }, '修改协议项Body失败');

                return true;
            }, reqOrResp === 1 ? '正在保存校验请求Body...' : '正在保存目标响应Body...');
        },
        async getProtocolBody(protocolId, reqOrResp) {
            if (isMockMode()) return KitProxy.mocks.getProtocolBody(protocolId, reqOrResp);

            // Body 类型是 JSON 响应，Body 数据是原始字节流，所以这里不能用 requestJson 合并处理。
            const [typeResponse, dataResponse] = await Promise.all([
                fetch(apiUrl('/protocols/details/body_type'), {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: protocolId,
                        req_or_resp: reqOrResp,
                    }),
                }),
                fetch(apiUrl('/protocols/details/body_data'), {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: protocolId,
                        req_or_resp: reqOrResp,
                    }),
                }),
            ]);

            const bodyType = await parseJsonResponse(typeResponse, '获取Body类型失败');

            if (!dataResponse.ok) {
                throw new Error('获取Body数据失败: HTTP ' + dataResponse.status);
            }

            const dataBuffer = await dataResponse.arrayBuffer();
            return [bodyType.body_type, new Uint8Array(dataBuffer)];
        },
        async getTcpCommonFields(protocolId, reqOrResp) {
            if (isMockMode()) return KitProxy.mocks.getTcpCommonFields(protocolId, reqOrResp);

            return requestJson('/protocols/details/tcp/common_fields', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    id: protocolId,
                    req_or_resp: reqOrResp,
                }),
            }, '获取普通字段信息失败');
        },
        async getAllPatternFields(projectId, protocolId, reqOrResp) {
            if (isMockMode()) return KitProxy.mocks.getAllPatternFields(projectId, protocolId, reqOrResp);

            const [patternInfo, cfgInfo] = await Promise.all([
                api.getProjectPatternInfo(projectId),
                api.getProtocolDetailsCfg(protocolId),
            ]);
            const sideCfg = Number(reqOrResp) === 1
                ? (cfgInfo && cfgInfo.req_cfg) || {}
                : (cfgInfo && cfgInfo.resp_cfg) || {};
            const fields = KitProxy.tcpPatternEditor
                ? KitProxy.tcpPatternEditor.patternInfoToItemFields(patternInfo, sideCfg)
                : [];
            return [patternInfo, fields];
        },
    };

    KitProxy.api = api;
})(typeof window !== 'undefined' ? window : globalThis);
