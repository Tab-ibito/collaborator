/* variable and const definition */
const $usernameInput = document.getElementById("username");
const $passwordInput = document.getElementById("password");
const $loginButton = document.getElementById("loginButton");
const $signupButton = document.getElementById("signupButton");
const $warningMessage = document.getElementById("warningMessage");

/* event listeners */
$loginButton.addEventListener("click", async (event) => {
    event.preventDefault();
    await postAuth("http://localhost:1145/api/login", wrapAuthPayload());
})

$signupButton.addEventListener("click", async (event) => {
    event.preventDefault();
    await postAuth("http://localhost:1145/api/register", wrapAuthPayload());
})

/* Helper functions */

// 包装载荷
const wrapAuthPayload = () => {
    const username = $usernameInput.value.trim();
    const password = $passwordInput.value.trim();

    if (!username) {
        $warningMessage.textContent = "Username is required";
        return;
    }

    if (!password) {
        $warningMessage.textContent = "Password is required";
    }

    // 包装内容
    return {
        username: username,
        password: password
    };
}

// 发送登录 / 注册请求
const postAuth = async (location, payload) => {
    try {
        // 包装请求头
        const response = await fetch(location, {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify(payload)
        });

        // 获取response包的json内容
        const result = await response.json();

        if (response.ok && result.success) {
            console.log("Successfully registered in!");
            window.location.href = "./edit.html?username=" + result.username;
        } else {
            console.log("Failed to register in!");
            $warningMessage.textContent = result.message;
        }
    } catch (error) {
        console.error("Request Failed, ", error);
    }
}