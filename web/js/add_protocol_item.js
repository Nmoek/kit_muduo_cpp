document.addEventListener('DOMContentLoaded', function() {
    const form = document.getElementById('add-protocol-item-form');
    const closeBtn = document.querySelector('.close-modal');
    const cancelBtn = document.querySelector('.cancel-btn');

    // 从父窗口获取协议类型
    const protocolType = window.opener?.serviceProtocolType || 'HTTP';
    document.getElementById('item-type').value = protocolType;

    form.addEventListener('submit', function(e) {
        e.preventDefault();
        const itemName = document.getElementById('item-name').value;
        const itemType = document.getElementById('item-type').value;
        
        // 通过opener访问父窗口的函数
        if (window.opener && window.opener.addProtocolItem) {
            window.opener.addProtocolItem(
                window.opener.document.querySelector('.service-card.expanded'), 
                itemName, 
                itemType
            );
        }
        window.close();
    });

    closeBtn.addEventListener('click', function() {
        window.close();
    });

    cancelBtn.addEventListener('click', function() {
        window.close();
    });
});
