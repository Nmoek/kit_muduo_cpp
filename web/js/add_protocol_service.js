document.addEventListener('DOMContentLoaded', function() {
    const form = document.getElementById('add-service-form');
    const cancelBtn = document.querySelector('.cancel-btn');
    const closeBtn = document.querySelector('.close-modal');

    // 关闭模态框
    function closeModal() {
        window.close();
    }

    // 监听测试模式变化
    function setupTestModeListener() {
        const testModeSelect = document.getElementById('protocol-type');
        if (testModeSelect) {
            testModeSelect.addEventListener('change', function() {
                const isServerMode = this.value === 'HTTP' || this.value === 'WebSocket';
                document.querySelector('.server-mode-control').style.display = isServerMode ? 'grid' : 'none';
                document.querySelector('.client-mode-control').style.display = isServerMode ? 'none' : 'grid';
            });
            // 初始化状态
            testModeSelect.dispatchEvent(new Event('change'));
        }
    }

    // 提交表单
    form.addEventListener('submit', function(e) {
        e.preventDefault();
        
        const serviceName = document.getElementById('service-name').value;
        const protocolType = document.getElementById('protocol-type').value;
        const servicePort = document.getElementById('service-port').value;
        const targetAddress = document.getElementById('target-address')?.value;

        // 将数据传回父窗口
        window.opener.postMessage({
            type: 'NEW_SERVICE',
            data: {
                name: serviceName,
                protocol: protocolType,
                port: servicePort,
                target: targetAddress
            }
        }, '*');

        closeModal();
    });

    // 取消按钮
    cancelBtn.addEventListener('click', closeModal);
    closeBtn.addEventListener('click', closeModal);

    // 初始化
    setupTestModeListener();
});
