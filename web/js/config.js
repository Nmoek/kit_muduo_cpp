(function initKitProxyConfig(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const STORAGE_KEY = 'kitProxy.v1.config';

    // 默认必须是真实接口模式，避免生产环境忘记去掉 Mock 参数时误用模拟数据。
    const DEFAULT_CONFIG = Object.freeze({
        apiMode: 'real',
        // 空字符串表示继续使用 /projects/list 这类同源相对路径。
        apiBaseUrl: '',
        enableDebugLog: false,
    });

    function normalizeApiMode(value) {
        return value === 'mock' || value === 'real' ? value : undefined;
    }

    function normalizeBoolean(value) {
        if (value === true || value === 'true') return true;
        if (value === false || value === 'false') return false;
        return undefined;
    }

    function readStoredConfig() {
        try {
            if (!global.localStorage) return {};
            const raw = global.localStorage.getItem(STORAGE_KEY);
            return raw ? JSON.parse(raw) : {};
        } catch (error) {
            console.warn('读取本地开发配置失败:', error);
            return {};
        }
    }

    function readQueryConfig() {
        try {
            if (!global.location || !global.URLSearchParams) return {};

            // URL 参数优先级最高，方便临时用 ?apiMode=mock 调试，不必改源码。
            const params = new URLSearchParams(global.location.search);
            const queryConfig = {};
            const apiMode = normalizeApiMode(params.get('apiMode'));
            const apiBaseUrl = params.get('apiBaseUrl');
            const enableDebugLog = normalizeBoolean(params.get('enableDebugLog'));

            if (apiMode) queryConfig.apiMode = apiMode;
            if (apiBaseUrl !== null) queryConfig.apiBaseUrl = apiBaseUrl;
            if (typeof enableDebugLog === 'boolean') queryConfig.enableDebugLog = enableDebugLog;

            return queryConfig;
        } catch (error) {
            console.warn('读取 URL 开发配置失败:', error);
            return {};
        }
    }

    function sanitizeConfig(config) {
        // 所有外部输入都先规整，防止 localStorage 或 URL 写入非法值。
        const apiMode = normalizeApiMode(config.apiMode) || DEFAULT_CONFIG.apiMode;
        const apiBaseUrl = typeof config.apiBaseUrl === 'string' ? config.apiBaseUrl : DEFAULT_CONFIG.apiBaseUrl;
        const enableDebugLog = typeof config.enableDebugLog === 'boolean'
            ? config.enableDebugLog
            : DEFAULT_CONFIG.enableDebugLog;

        return {
            apiMode,
            apiBaseUrl,
            enableDebugLog,
        };
    }

    function saveConfig(partialConfig) {
        // 这个方法只给开发期调试用；业务代码不要通过它临时切换数据来源。
        const nextConfig = sanitizeConfig(Object.assign({}, KitProxy.config, partialConfig));
        Object.assign(KitProxy.config, nextConfig);

        try {
            if (global.localStorage) {
                global.localStorage.setItem(STORAGE_KEY, JSON.stringify(nextConfig));
            }
        } catch (error) {
            console.warn('保存本地开发配置失败:', error);
        }

        return KitProxy.config;
    }

    const config = sanitizeConfig(Object.assign({}, DEFAULT_CONFIG, readStoredConfig(), readQueryConfig()));

    KitProxy.config = config;
    KitProxy.configStorageKey = STORAGE_KEY;
    KitProxy.saveConfig = saveConfig;
    KitProxy.debugLog = function debugLog() {
        // 日志开关只控制输出，不参与 mock/real 的数据源选择。
        if (KitProxy.config.enableDebugLog) {
            console.log.apply(console, arguments);
        }
    };
})(typeof window !== 'undefined' ? window : globalThis);
