import { describe, expect, it, vi } from 'vitest';
import {
    createDomContext,
    loadCoreScripts,
    runScript,
} from './helpers/browser_context.js';

function loadAuthContext(url, options = {}) {
    const context = createDomContext('<!doctype html><html><body><header class="service-manager-header"></header></body></html>', url);
    loadCoreScripts(context);
    if (options.persistence) runScript(context, 'js/protocol_interaction_persistence.js');
    runScript(context, 'js/auth.js');
    return context;
}

function loadLoginContext(url) {
    const context = createDomContext(`<!doctype html><html><body>
        <div class="login-box"><form id="loginForm">
            <button type="button" id="loginModeSwitch"><span class="login-mode-switch-text"></span></button>
            <input id="note"><input id="password">
            <div id="adminPasswordGroup"></div><button id="loginSubmit" type="submit">登录</button>
            <div id="loginError"></div>
        </form></div>
    </body></html>`, url);
    loadCoreScripts(context);
    runScript(context, 'js/auth.js');
    runScript(context, 'js/login.js');
    context.document.dispatchEvent(new context.Event('DOMContentLoaded'));
    return context;
}

function snapshotFor(persistence, identity) {
    return {
        snapshotKey: identity.snapshotKey,
        schemaVersion: persistence.SCHEMA_VERSION,
        backendIdentity: identity.backendIdentity,
        userId: identity.userId,
        tabSessionId: identity.tabSessionId,
        projectId: identity.projectId,
        protocolId: identity.protocolId,
        expiresAt: Date.now() + 60 * 60 * 1000,
        visibleKeys: [],
        protocolPendingKeys: [],
        noticePendingKeys: [],
        reflowKeys: [],
        readRecordKeys: [],
        uiState: { drawerOpen: true },
        connectionIntent: { desiredConnected: true },
    };
}

describe('authentication and interaction workspace recovery', () => {
    /**
     * 测试思路：协议项页触发登录态过期时，登录 URL 必须保留当前页完整路径和查询参数。
     * 示例：/html/protocol_items.html?apiMode=mock&projectId=7 应在登录后回到同一项目的协议项页。
     */
    it('协议项页被动跳转登录时保留安全 returnTo', () => {
        const context = loadAuthContext(
            'http://localhost/html/protocol_items.html?apiMode=mock&apiBaseUrl=%2Fproxy&projectId=7&debugCase=drawer#records',
        );

        const loginUrl = new URL(context.KitProxy.auth.buildLoginUrl(), context.location.href);
        expect(loginUrl.pathname).toBe('/html/login.html');
        expect(loginUrl.searchParams.get('apiMode')).toBe('mock');
        expect(loginUrl.searchParams.get('apiBaseUrl')).toBe('/proxy');
        expect(loginUrl.searchParams.get('returnTo')).toBe(
            '/html/protocol_items.html?apiMode=mock&apiBaseUrl=%2Fproxy&projectId=7&debugCase=drawer#records',
        );
    });

    /**
     * 测试思路：登录页成功后优先使用已验证的 returnTo，而不是无条件回到 main.html。
     * 示例：returnTo 指向 projectId=7 的协议项页时，buildPostLoginUrl 应原样恢复该页。
     */
    it('成功登录后返回原协议项页', () => {
        const context = loadAuthContext(
            'http://localhost/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D7%26debugCase%3Ddrawer%23records',
        );

        expect(context.KitProxy.auth.buildPostLoginUrl()).toBe(
            '/html/protocol_items.html?apiMode=mock&projectId=7&debugCase=drawer#records',
        );
    });

    /**
     * 测试思路：不能只测 URL 辅助函数，登录表单提交本身也必须调用恢复地址构造逻辑。
     * 示例：登录 API 返回 user 后，login.js 调用 buildPostLoginUrl，测试用 hash 导航代替真实页面跳转。
     */
    it('登录表单成功提交使用协议项 returnTo', async () => {
        const context = loadLoginContext(
            'http://localhost/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FprojectId%3D7',
        );
        context.KitProxy.api.login = vi.fn(async () => ({ id: 9, note: 'testUser', role: 'normal' }));
        vi.spyOn(context.KitProxy.auth, 'buildPostLoginUrl').mockReturnValue('#returned-to-protocol-page');
        context.document.getElementById('note').value = 'testUser';
        context.document.getElementById('loginForm').dispatchEvent(new context.Event('submit', {
            bubbles: true,
            cancelable: true,
        }));

        await new Promise(resolve => setTimeout(resolve, 0));

        expect(context.KitProxy.api.login).toHaveBeenCalledWith('testUser', 'normal', '');
        expect(context.KitProxy.auth.buildPostLoginUrl).toHaveBeenCalledTimes(1);
        expect(context.location.hash).toBe('#returned-to-protocol-page');
    });

    /**
     * 测试思路：returnTo 是不可信输入，只允许当前 origin 下 /html/*.html 页面。
     * 示例：https://evil.example、//evil.example、javascript: 和 mailto: 都必须降级到 main.html。
     */
    it('拒绝恶意外部 returnTo', () => {
        const maliciousValues = [
            'https://evil.example/steal',
            '//evil.example/steal',
            'javascript:alert(1)',
            'mailto:evil@example.com',
            '/api/private',
            '/html/nested/page.html',
        ];

        maliciousValues.forEach(value => {
            const context = loadAuthContext(
                `http://localhost/html/login.html?returnTo=${encodeURIComponent(value)}`,
            );
            expect(context.KitProxy.auth.getSafeReturnToFromLoginUrl()).toBe('');
            expect(context.KitProxy.auth.buildPostLoginUrl()).toBe('/html/main.html');
        });
    });

    /**
     * 测试思路：401 是被动认证失败，不能被当作显式退出而清理本地工作区。
     * 示例：已有 snapshot、恢复 marker 和 tab session ID 时，getCurrentUser 返回 401 后三者都仍存在。
     */
    it('401 被动登录跳转不删除工作区 snapshot 或 restore marker', async () => {
        const context = loadAuthContext(
            'http://localhost/html/protocol_items.html?apiMode=mock&projectId=7',
            { persistence: true },
        );
        const persistence = context.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: '',
            location: context.location,
            user: { id: 9 },
            tabSessionId: context.sessionStorage.getItem('kit_protocol_interaction_tab_session_id'),
            projectId: 7,
            protocolId: 11,
        });
        await adapter.saveSnapshot(snapshotFor(persistence, identity));
        persistence.writeRestoreMarker(identity, { drawerOpen: true }, context.sessionStorage, () => 1000);
        const tabSessionId = context.sessionStorage.getItem('kit_protocol_interaction_tab_session_id');
        const originalClear = persistence.clearUserWorkspace;
        persistence.clearUserWorkspace = vi.fn(() => {
            throw new Error('401 不应调用用户级清理');
        });
        context.KitProxy.api.getCurrentUser = vi.fn(async () => {
            const error = new Error('登录态已过期');
            error.status = 401;
            throw error;
        });

        await expect(context.KitProxy.auth.loadCurrentUser({ redirectOnUnauthorized: false })).rejects.toMatchObject({ status: 401 });
        expect(persistence.clearUserWorkspace).not.toHaveBeenCalled();
        expect((await adapter.loadWorkspace(identity.snapshotKey)).status).toBe('hit');
        expect(persistence.readRestoreMarker(context.sessionStorage)).toMatchObject({
            snapshotKey: identity.snapshotKey,
            drawerOpen: true,
        });
        expect(context.sessionStorage.getItem('kit_protocol_interaction_tab_session_id')).toBe(tabSessionId);
        persistence.clearUserWorkspace = originalClear;
    });

    /**
     * 测试思路：显式退出与 401 语义相反，即使退出接口失败也必须清理当前用户工作区和 marker。
     * 示例：userId=9 的 snapshot 应删除，其他 userId 的 snapshot 不受影响，登录页不得携带 returnTo。
     */
    it('显式退出无论接口结果如何都清理当前用户工作区', async () => {
        const context = loadAuthContext(
            'http://localhost/html/protocol_items.html?apiMode=mock&projectId=7&debugCase=drawer',
            { persistence: true },
        );
        const persistence = context.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const currentIdentity = persistence.createWorkspaceIdentity({
            location: context.location,
            user: { id: 9 },
            tabSessionId: 'tab-a',
            projectId: 7,
            protocolId: 11,
        });
        const otherIdentity = persistence.createWorkspaceIdentity({
            location: context.location,
            user: { id: 10 },
            tabSessionId: 'tab-b',
            projectId: 7,
            protocolId: 11,
        });
        await adapter.saveSnapshot(snapshotFor(persistence, currentIdentity));
        await adapter.saveSnapshot(snapshotFor(persistence, otherIdentity));
        persistence.writeRestoreMarker(currentIdentity, { drawerOpen: true }, context.sessionStorage, () => 1000);
        persistence.clearUserWorkspace = input => persistence.clearUserWorkspace.__adapter.deleteUser(input);
        persistence.clearUserWorkspace.__adapter = adapter;
        context.KitProxy.api.logout = vi.fn(async () => {
            throw new Error('后端退出接口不可用');
        });
        context.KitProxy.auth.applyCurrentUser({ id: 9, note: 'testUser', role: 'normal' });

        await context.KitProxy.auth.logout();

        expect((await adapter.loadWorkspace(currentIdentity.snapshotKey)).status).toBe('miss');
        expect((await adapter.loadWorkspace(otherIdentity.snapshotKey)).status).toBe('hit');
        expect(persistence.readRestoreMarker(context.sessionStorage)).toBeNull();
        expect(new URL(context.KitProxy.auth.buildLoginUrl({ includeReturnTo: false }), context.location.href).searchParams.has('returnTo')).toBe(false);
    });
});
