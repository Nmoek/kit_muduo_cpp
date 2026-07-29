function envString(name, fallback) {
    const value = process.env[name];
    return value == null || value === '' ? fallback : value;
}

function envInteger(name, fallback) {
    const value = Number(envString(name, String(fallback)));
    if (!Number.isInteger(value) || value <= 0) {
        throw new Error(`${name} must be a positive integer`);
    }
    return value;
}

export const REAL_E2E = Object.freeze({
    baseURL: envString('PLAYWRIGHT_REAL_BASE_URL', 'http://172.25.66.135:5555'),
    adminNote: envString('PLAYWRIGHT_REAL_ADMIN_NOTE', 'admin'),
    adminPassword: envString('PLAYWRIGHT_REAL_ADMIN_PASSWORD', 'admin123'),
    normalNote: envString('PLAYWRIGHT_REAL_NORMAL_NOTE', 'lijianke5'),
    normalProjectId: envInteger('PLAYWRIGHT_REAL_NORMAL_PROJECT_ID', 4),
    normalProtocolId: envInteger('PLAYWRIGHT_REAL_NORMAL_PROTOCOL_ID', 6),
    projectId: envInteger('PLAYWRIGHT_REAL_PROJECT_ID', 1),
    protocolId: envInteger('PLAYWRIGHT_REAL_PROTOCOL_ID', 1),
    projectTcpHost: envString('PLAYWRIGHT_REAL_PROJECT_TCP_HOST', '127.0.0.1'),
    projectTcpPort: envInteger('PLAYWRIGHT_REAL_PROJECT_TCP_PORT', 43817),
    attachmentProjectId: envInteger('PLAYWRIGHT_REAL_ATTACHMENT_PROJECT_ID', 2),
    attachmentProtocolId: envInteger('PLAYWRIGHT_REAL_ATTACHMENT_PROTOCOL_ID', 4),
    attachmentHttpHost: envString('PLAYWRIGHT_REAL_ATTACHMENT_HTTP_HOST', '127.0.0.1'),
    attachmentHttpPort: envInteger('PLAYWRIGHT_REAL_ATTACHMENT_HTTP_PORT', 42627),
    attachmentHttpPath: envString('PLAYWRIGHT_REAL_ATTACHMENT_HTTP_PATH', '/api/test4'),
});

export function protocolItemsPath(projectId = REAL_E2E.projectId) {
    return `/html/protocol_items.html?projectId=${encodeURIComponent(projectId)}`;
}

export function protocolItemSelector(protocolId = REAL_E2E.protocolId) {
    return `.protocol-item[data-protocol-id="${String(protocolId)}"]`;
}

/**
 * Real 交互用例依赖实际 ProjectServer；隔离数据库中的项目可能是停止态，
 * 因此在进入协议项页面前显式建立运行实例。
 * @param {import('@playwright/test').Page} page
 * @param {number} projectId
 */
export async function ensureRealProjectRunning(page, projectId = REAL_E2E.projectId) {
    await page.evaluate(async id => {
        const project = (await window.KitProxy.api.getProject(id))[0];
        if (!project) throw new Error(`Real fixture project ${id} 不存在`);
        if (Number(project.runtime_state) !== 1) {
            await window.KitProxy.api.setProjectRuntimeState(id, true);
        }
    }, projectId);
}
