// 登录表单处理
document.getElementById('loginForm')?.addEventListener('submit', async function(event) {
    // 阻止表单的默认提交行为
    event.preventDefault();
    
    // 获取表单元素
    const form = event.target;
    const submitBtn = form.querySelector('button[type="submit"]');
    
    // 获取用户名和密码输入框的值，并去除两端的空白字符
    const username = document.getElementById('username').value.trim();
    const password = document.getElementById('password').value.trim();
    
    // 如果用户名或密码为空，弹出提示信息并返回
    if (!username || !password) {
        alert('请输入用户名和密码');
        return;
    }

    // 显示加载状态
    submitBtn.disabled = true;
    submitBtn.textContent = '登录中...';

    alert('登录成功！');
    window.location.href = 'main.html';

    // try {
    //     // 发送登录请求
    //     const url = new URL('/api/login', location.origin);

    //     console.log('url:', url);

    //     const response = await fetch(url, {
    //         method: 'POST',
    //         headers: {
    //             'Content-Type': 'application/json',
    //         },
    //         body: JSON.stringify({
    //             username: username,
    //             password: password
    //         })
    //     });

    //     console.log('响应状态:', response.status);
        
    //     if(!response.ok) {
    //         throw new Error('登录失败!');
    //     }

    //     const data = await response.json();
        
    //     if (data.code == 0) {
    //         alert('登录成功！');
    //         window.location.href = 'dashboard.html';
    //     } else {
    //         throw new Error(data.message || '登录失败');
    //     }
    // } catch (error) {
    //     console.error('登录请求错误:', error);
    //     alert(error.message);
    // } finally {
    //     // 恢复按钮状态
    //     submitBtn.disabled = false;
    //     submitBtn.textContent = '登录';
    // }
});
