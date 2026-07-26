import { expect } from '@playwright/test';

const MOCK_ORIGIN = 'http://127.0.0.1:4173';
const mockPersistenceContexts = new WeakSet();

function pagePath(url) {
    return new URL(url, MOCK_ORIGIN).pathname;
}

/**
 * Mock 数据脚本默认按页面内存初始化；测试计划中的保存后回读跨越多个独立
 * HTML 文档，因此只在测试 context 内把 mock state 放进 localStorage。
 * 生产页面和真实后端不使用这段注入逻辑。
 * @param {import('@playwright/test').BrowserContext} context
 */
export async function installMockStatePersistence(context) {
    if (mockPersistenceContexts.has(context)) return;
    mockPersistenceContexts.add(context);
    await context.addInitScript(() => {
        const key = '__kit_playwright_mock_state__';

        function restore() {
            if (!window.KitProxy || !window.KitProxy.mocks) return;
            const raw = window.localStorage.getItem(key);
            if (!raw) return;
            try {
                const saved = JSON.parse(raw);
                Object.assign(window.KitProxy.mocks.state, saved);
            } catch (error) {
                window.localStorage.removeItem(key);
            }
        }

        function persist() {
            if (!window.KitProxy || !window.KitProxy.mocks || !window.KitProxy.mocks.state) return;
            window.localStorage.setItem(key, JSON.stringify(window.KitProxy.mocks.state));
        }

        function installMutationSnapshots() {
            if (!window.KitProxy || !window.KitProxy.mocks) return;
            const mutationNames = [
                'addProject',
                'setProjectRuntimeState',
                'setProjectActive',
                'updateProjectName',
                'deleteProject',
                'restoreProject',
                'addProtocol',
                'setProtocolRuntime',
                'reconfigProtocol',
                'updateProtocolName',
                'deleteProtocol',
                'restoreProtocol',
                'updateProtocolCfg',
                'updateProtocolBody',
                'updateProjectPatternInfo',
                'addUser',
                'updateUser',
                'deleteUser',
                'restoreUser',
            ];
            for (const name of mutationNames) {
                const original = window.KitProxy.mocks[name];
                if (typeof original !== 'function' || original.__playwrightSnapshot) continue;
                const wrapped = function snapshotMutation(...args) {
                    const result = original.apply(this, args);
                    persist();
                    return result;
                };
                wrapped.__playwrightSnapshot = true;
                window.KitProxy.mocks[name] = wrapped;
            }
        }

        const kitProxy = window.KitProxy || (window.KitProxy = {});
        let mocksValue;
        Object.defineProperty(kitProxy, 'mocks', {
            configurable: true,
            enumerable: true,
            get() {
                return mocksValue;
            },
            set(value) {
                mocksValue = value;
                restore();
                installMutationSnapshots();
            },
        });

        window.addEventListener('DOMContentLoaded', () => {
            restore();
            installMutationSnapshots();
        }, { once: false });
        window.addEventListener('pagehide', persist);
        window.addEventListener('beforeunload', persist);
        // Mock mutation 与页面跳转之间可能没有足够时间触发卸载事件，
        // 用短周期快照保证跨独立 HTML 文档的保存后回读稳定。
        window.setInterval(persist, 25);
    });
}

/**
 * 通过真实登录页面建立 Mock 会话，避免各模块对登录流程各自造假。
 * @param {import('@playwright/test').Page} page
 * @param {import('@playwright/test').BrowserContext} context
 * @param {'admin'|'normal'} role
 * @param {string} returnTo
 * @param {string} note
 */
export async function loginAs(page, context, role, returnTo, note = role === 'admin' ? 'admin' : 'testuser') {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(returnTo)}`);

    if (role === 'admin') {
        await page.getByRole('button', { name: '切换为管理员登录' }).click();
        await page.getByLabel('密码').fill('admin123');
    }

    await page.getByLabel('note').fill(note);
    const targetPath = pagePath(returnTo);
    await Promise.all([
        page.waitForURL(url => new URL(url).pathname === targetPath),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

export async function loginAsAdmin(page, context, returnTo) {
    await loginAs(page, context, 'admin', returnTo, 'admin');
}

export async function loginAsNormal(page, context, returnTo, note = 'testuser') {
    await loginAs(page, context, 'normal', returnTo, note);
}

export async function expectMockPage(page, pathname) {
    await expect(page).toHaveURL(url => new URL(url).pathname === pathname);
}
