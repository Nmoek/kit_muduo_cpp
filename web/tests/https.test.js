import { describe, expect, it, vi } from 'vitest';
import {
    createBrowserContext, createProtocolItemFormContext, loadCoreScripts,
    loadProtocolListScripts, loginMockUser, flushPromises, runScript,
} from './helpers/browser_context.js';

async function prepareForm(mode = 'create') {
    const context = createProtocolItemFormContext(`?apiMode=mock&projectId=1${mode === 'edit' ? '&protocolId=1' : ''}`);
    loadCoreScripts(context);
    await loginMockUser(context, { note: 'admin', loginType: 'admin', password: 'admin123' });
    context.KitProxy.mocks.state.projects[0].protocol_type = 3;
    context.KitProxy.mocks.state.protocols[0].type = 'HTTPS';
    ['js/tcp_pattern_modal.js', 'js/protocol_item.js', 'js/protocol_registry.js']
        .forEach(file => runScript(context, file));
    context.KitProxy.__disableAutoInitProtocolItemForm = true;
    runScript(context, 'js/protocol_item_form.js');
    await context.KitProxy.protocolItemForm.initPage();
    await flushPromises(8);
    return context;
}

describe('HTTPS test services', () => {
    /** 测试思路：HTTPS 复用 HTTP 详情和新增弹窗，例：HTTPS GET /secure 显示方法、路径和状态码。 */
    it('registers HTTPS fields and keeps the modal protocol type', () => {
        const context = createBrowserContext();
        loadCoreScripts(context);
        ['js/tcp_pattern_modal.js', 'js/add_protocol_modal.js', 'js/protocol_item.js', 'js/protocol_registry.js']
            .forEach(file => runScript(context, file));
        const grid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            type: 'HTTPS', req_cfg: { method: 'GET', path: '/secure' }, resp_cfg: { status_code: 201 },
        });
        expect(grid.querySelector('.path .value').textContent).toBe('/secure');
        expect(grid.querySelector('.status .value').textContent).toBe('201');
        const modal = context.ProtocolTypeRegistry.getAddProtocolModal(3).create({ id: 'service-card-1' });
        expect(modal.dataset.projectProtocolType).toBe('3');
        expect(modal.querySelector('form')).toBeTruthy();
    });

    /** 测试思路：新增 HTTPS 渲染 HTTP 字段，提交保留 https；例：POST /secure 不转成 http。 */
    it('creates HTTPS payloads using HTTP method and path fields', async () => {
        const context = await prepareForm();
        const add = vi.spyOn(context.KitProxy.api, 'addProtocol').mockResolvedValue({ protocol_id: 99 });
        context.document.addEventListener('protocol-item-form:navigate-back', event => event.preventDefault());
        context.document.getElementById('protocol-item-name').value = 'HTTPS测试';
        context.document.getElementById('request-path').value = '/secure';
        context.document.querySelector('input[name="request-method"][value="POST"]').checked = true;
        context.document.getElementById('protocol-item-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(16);
        expect(add).toHaveBeenCalledTimes(1);
        expect(add.mock.calls[0][0].cfg_header.type).toBe('https');
        expect(add.mock.calls[0][0].req_cfg).toMatchObject({ method: 'POST', path: '/secure' });
    });

    /** 测试思路：HTTPS 编辑沿用 HTTP 配置，监听端口保持数字；例：路径可编辑，端口显示 18080。 */
    it('loads HTTPS protocol edit fields', async () => {
        const context = await prepareForm('edit');
        expect(context.document.getElementById('request-path').value).toBe('/api/test1');
        expect(context.document.getElementById('req-http-headers')).toBeTruthy();
        expect(context.KitProxy.protocolItemForm.pageState.protocolType).toBe(3);
        expect(context.KitProxy.protocolItemForm.pageState.initialReqCfg.path).toBe('/api/test1');
        const portField = Array.from(context.document.querySelectorAll('#protocol-form-project-context .service-context-item'))
            .find(field => field.querySelector('.meta-label').textContent === '监听端口');
        expect(portField.querySelector('.meta-value').textContent).toBe('18080');
    });

    /** 测试思路：HTTPS 服务的监听端口只展示端口号，并暴露创建入口；例：listen_port=8443 显示 8443。 */
    it('shows HTTPS port numbers and an add-service option', () => {
        const context = createBrowserContext();
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const endpoint = context.getProjectEndpointDisplay({
            protocol_type: 3, mode: 1, listen_port: 8443, status: 1, runtime_state: 1,
        });
        expect(endpoint).toBe(8443);
        expect(context.addNewServiceCardModalHTML()).toContain('<option value=3>HTTPS</option>');
    });
});
