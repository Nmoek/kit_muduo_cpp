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
