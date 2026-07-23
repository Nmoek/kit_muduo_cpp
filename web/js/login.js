(function initLoginPage(global) {
    const NOTE_PATTERN = /^[A-Za-z0-9]{3,32}$/;
    // 未正式发版前使用 Beta；后续发版只需要修改这个版本标识。
    const LOGIN_VERSION_LABEL = 'Beta';

    /**
     * @param {HTMLElement | null} errorElement
     * @param {string} message
     */
    function showLoginError(errorElement, message) {
        const text = String(message || '').trim();
        if (errorElement) {
            errorElement.textContent = text;
            errorElement.style.display = text ? 'block' : 'none';
        }
        if (text && global.KitProxy && KitProxy.utils && typeof KitProxy.utils.showGlobalError === 'function') {
            KitProxy.utils.showGlobalError(text);
        }
    }

    /**
     * note 是后端识别用户的登录名，第一版只允许英文字母和数字。
     * @param {string} note
     * @returns {string}
     */
    function validateNote(note) {
        if (!note) return '请输入 note';
        if (!NOTE_PATTERN.test(note)) return 'note 必须是 3-32 位英文字母和数字';
        return '';
    }

    document.addEventListener('DOMContentLoaded', function() {
        const form = document.getElementById('loginForm');
        const noteInput = document.getElementById('note');
        const passwordInput = document.getElementById('password');
        const modeSwitch = document.getElementById('loginModeSwitch');
        const passwordGroup = document.getElementById('adminPasswordGroup');
        const submitBtn = document.getElementById('loginSubmit');
        const errorElement = document.getElementById('loginError');
        const loginBox = document.querySelector('.login-box');
        const versionBadge = document.getElementById('loginVersionBadge');
        let isAdminLogin = false;

        if (!form || !noteInput || !passwordInput || !modeSwitch || !passwordGroup || !submitBtn) return;
        if (versionBadge) versionBadge.textContent = LOGIN_VERSION_LABEL;

        /**
         * @param {boolean} enabled
         */
        function setAdminMode(enabled) {
            isAdminLogin = Boolean(enabled);
            form.dataset.loginMode = isAdminLogin ? 'admin' : 'normal';
            loginBox?.classList.toggle('is-admin-login', isAdminLogin);
            modeSwitch.classList.toggle('is-admin-mode', isAdminLogin);
            modeSwitch.setAttribute('aria-pressed', String(isAdminLogin));
            modeSwitch.setAttribute('aria-label', isAdminLogin ? '切换为普通用户登录' : '切换为管理员登录');
            const modeText = modeSwitch.querySelector('.login-mode-switch-text');
            if (modeText) modeText.textContent = isAdminLogin ? '切换普通登录' : '切换管理员登录';
            passwordGroup.hidden = false;
            passwordGroup.classList.toggle('is-visible', isAdminLogin);
            passwordGroup.setAttribute('aria-hidden', String(!isAdminLogin));
            passwordInput.required = isAdminLogin;
            passwordInput.disabled = !isAdminLogin;
            passwordInput.tabIndex = isAdminLogin ? 0 : -1;
            if (!isAdminLogin) passwordInput.value = '';
            showLoginError(errorElement, '');
        }

        modeSwitch.addEventListener('click', function() {
            setAdminMode(!isAdminLogin);
        });
        noteInput.addEventListener('input', function() {
            showLoginError(errorElement, '');
        });
        passwordInput.addEventListener('input', function() {
            showLoginError(errorElement, '');
        });

        form.addEventListener('submit', async function(event) {
            event.preventDefault();

            const note = noteInput.value.trim();
            const password = passwordInput.value;
            const loginType = isAdminLogin ? 'admin' : 'normal';
            const noteError = validateNote(note);

            if (noteError) {
                showLoginError(errorElement, noteError);
                noteInput.focus();
                return;
            }

            if (loginType === 'admin' && !password) {
                showLoginError(errorElement, '管理员登录需要填写密码');
                passwordInput.focus();
                return;
            }

            submitBtn.disabled = true;
            submitBtn.textContent = '登录中...';

            try {
                const user = await KitProxy.api.login(note, loginType, password);
                if (KitProxy.auth && typeof KitProxy.auth.applyCurrentUser === 'function') {
                    KitProxy.auth.applyCurrentUser(user);
                }
                global.location.href = KitProxy.auth && typeof KitProxy.auth.buildPostLoginUrl === 'function'
                    ? KitProxy.auth.buildPostLoginUrl()
                    : (KitProxy.auth ? KitProxy.auth.buildPageUrl('main.html') : 'main.html');
            } catch (error) {
                showLoginError(errorElement, error && error.message ? error.message : '登录失败，请稍后重试');
            } finally {
                submitBtn.disabled = false;
                submitBtn.textContent = '登录';
            }
        });

        setAdminMode(false);
    });
})(typeof window !== 'undefined' ? window : globalThis);
