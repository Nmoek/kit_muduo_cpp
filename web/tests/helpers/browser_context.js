import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { vi } from 'vitest';
import { JSDOM } from 'jsdom';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '../..');

export function flushPromises(times = 4) {
    let chain = Promise.resolve();
    for (let idx = 0; idx < times; idx += 1) {
        chain = chain.then(() => Promise.resolve());
    }
    return chain;
}

/**
 * @param {vm.Context} context
 * @param {string} filePath
 */
export function runScript(context, filePath) {
    // 生产代码是经典 script，不是 ES Module；测试里用 vm 按浏览器脚本方式执行。
    const code = fs.readFileSync(path.join(repoRoot, filePath), 'utf8');
    vm.runInContext(code, context, { filename: filePath });
}

export function readRepoFile(filePath) {
    return fs.readFileSync(path.join(repoRoot, filePath), 'utf8');
}

export function repoFileExists(filePath) {
    return fs.existsSync(path.join(repoRoot, filePath));
}

export function createDomContext(markup, url) {
    const dom = new JSDOM(markup, { url });
    const context = vm.createContext(dom.window);
    context.console = console;
    context.fetch = vi.fn();
    context.TextEncoder = TextEncoder;
    context.TextDecoder = TextDecoder;
    return context;
}

export function createProtocolItemPageContext(projectId) {
    return createDomContext(`<!doctype html><html><body>
        <button id="add-protocol-item" disabled>添加协议项</button>
        <a id="back-service-list"></a>
        <h2 id="protocol-items-title"></h2>
        <div class="protocol-items-page">
            <div id="protocol-service-meta"></div>
            <div class="protocol-list"></div>
            <div id="protocol-pagination" class="pagination-bar"></div>
        </div>
    </body></html>`, `http://localhost/html/protocol_items.html?apiMode=mock&projectId=${projectId}`);
}

export function createProtocolItemFormContext(search) {
    return createDomContext(`<!doctype html><html><body>
        <a id="back-protocol-list"></a>
        <h2 id="protocol-form-title"></h2>
        <p id="protocol-form-subtitle"></p>
        <div id="protocol-form-project-context"></div>
        <form id="protocol-item-form">
            <input id="protocol-item-name">
            <div id="protocol-type-fields"></div>
            <button type="button" class="body-switch-btn is-active" data-body-tab="request">校验请求Body</button>
            <button type="button" class="body-switch-btn" data-body-tab="response">目标响应Body</button>
            <div id="protocol-body-editor-host"></div>
            <button id="save-protocol-form" type="submit"></button>
            <button id="save-protocol-menu-toggle" type="button"></button>
            <div id="save-protocol-menu" hidden>
                <button id="save-and-online-protocol" type="button"></button>
            </div>
            <button id="cancel-protocol-form" type="button"></button>
        </form>
    </body></html>`, `http://localhost/html/protocol_item_form.html${search}`);
}

export function createBrowserContext(search = '?apiMode=mock') {
    return createDomContext('<!doctype html><html><body><div class="service-cards"></div></body></html>', `http://localhost/html/main.html${search}`);
}

/**
 * @param {vm.Context} context
 */
export function loadCoreScripts(context) {
    // 这里的顺序必须和 main.html 保持一致，否则不能发现真实的脚本依赖问题。
    [
        'js/namespace.js',
        'js/config.js',
        'js/constants.js',
        'js/utils.js',
        'js/body_editor.js',
        'js/service_filters.js',
        'js/mock_data.js',
        'js/api.js',
        'js/pagination.js',
    ].forEach(filePath => runScript(context, filePath));
}

/**
 * @param {vm.Context} context
 */
export function loadProtocolListScripts(context) {
    [
        'js/tcp_pattern_modal.js',
        'js/protocol_item.js',
        'js/protocol_registry.js',
        'js/main.js',
        'js/protocol_items.js',
    ].forEach(filePath => runScript(context, filePath));
}

/**
 * Mock API 现在也模拟登录态；测试访问用户数据前必须明确建立 session。
 * @param {vm.Context} context
 * @param {{note?: string, loginType?: string, password?: string}=} options
 */
export async function loginMockUser(context, options = {}) {
    const note = options.note || 'testUser';
    const loginType = options.loginType || 'normal';
    const password = options.password || '';
    return context.KitProxy.api.login(note, loginType, password);
}
