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
            if (isMockMode()) return KitProxy.mocks.addProject(project);

            return requestJson('/projects/add', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(project),
            }, '添加测试服务失败');
        },
        async updateProjectName(projectId, name) {
            if (isMockMode()) return KitProxy.mocks.updateProjectName(projectId, name);

            return requestJson('/projects/' + projectId + '/name', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ name }),
            }, '修改测试服务标题失败');
        },
        async deleteProject(projectId) {
            if (isMockMode()) return KitProxy.mocks.deleteProject(projectId);

            await requestJson('/projects/' + String(projectId), {
                method: 'DELETE',
            }, '删除测试服务失败');

            return true;
        },
        async getProjectPatternInfo(projectId) {
            if (isMockMode()) return KitProxy.mocks.getProjectPatternInfo(projectId);

            return requestJson('/projects/' + projectId + '/pattern_info', {
                method: 'GET',
            }, '获取TCP格式信息失败');
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
            if (isMockMode()) return KitProxy.mocks.addProtocol(protocol);

            const formData = KitProxy.utils.createAddProtocolFormData(protocol);

            return requestJson('/protocols/add', {
                method: 'POST',
                body: formData,
            }, '添加协议项失败');
        },
        async updateProtocolName(protocolId, name) {
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
        },
        async deleteProtocol(protocolId, projectId) {
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
        },
        async updateProtocolCfg(protocolId, projectId, reqOrResp, cfgJson) {
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
        },
        async getProtocolDetailsCfg(protocolId) {
            if (isMockMode()) return KitProxy.mocks.getProtocolDetailsCfg(protocolId);

            return requestJson(`/protocols/${protocolId}/details/cfg`, {
                method: 'GET',
            }, '查询协议项配置失败');
        },
        async updateProtocolBody(protocolId, projectId, reqOrResp, protocolType, bodyType, body) {
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

            return Promise.all([
                api.getProjectPatternInfo(projectId),
                api.getTcpCommonFields(protocolId, reqOrResp),
            ]);
        },
    };

    KitProxy.api = api;
})(typeof window !== 'undefined' ? window : globalThis);
