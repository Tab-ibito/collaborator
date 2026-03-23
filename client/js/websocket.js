/* variable and const definition */
const $cells = document.getElementsByClassName('grid-cell');
const $x = document.getElementById('xInput');
const $y = document.getElementById('yInput');
const $submitBtn = document.getElementById('submitBtn');
const $colorInput = document.getElementById('colorInput');
const rows = 4;
const cols = 4;
const $warningMessage = document.getElementById("warningMessage");

const queryString = window.location.search;
const urlParams = new URLSearchParams(queryString);
const currentUsername = urlParams.get('username');
console.log(currentUsername);

/* grid processing function */
const getGridIndex = () => {
    return parseInt($x.value) + parseInt($y.value) * rows;
};

/* websocket connection for edit */
const ws = new WebSocket('ws://localhost:1145/ws/edit');

// 建立连接
ws.onopen = () => {
    console.log("WebSocket connection opened");
    const payload = {
        type: 'join',
        username: currentUsername
    }
    ws.send(JSON.stringify(payload));
};

// 提交更改像素的请求，并顺带更改本地
$submitBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const index = getGridIndex();
    const color = $colorInput.value;
    const payload = {
        type: 'pixel_update',
        index: index,
        color: color,
    }
    if (!CSS.supports("color", color)) {
        $warningMessage.textContent = "Invalid color";
        return;
    }
    $cells[index].style.backgroundColor = color;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(payload));
        console.log(`send to server：index ${index} in color ${color} ！`);
    } else {
        console.warn("WebSocket disconnected");
    }
});

// 收到广播消息更改像素
ws.onmessage = (event) => {
    const update = JSON.parse(event.data);
    switch (update.type) {
        case "invalid_username":
            window.location.href = "./login.html";
            break;
        case "user_joined":
            console.log(`${update.time} User ${update.username} joined the document`);
            break;
        case "pixel_update":
            console.log(`${update.time} User ${update.username} changed index：${update.index} in color ${update.color}`);
            $cells[update.index].style.backgroundColor = update.color;
            break;
        case "user_left":
            console.log(`${update.time} User ${update.username} left the document`);
            break;
        case "canvas":
            console.log(`get canvas`);
            let i = 0;
            update.canvas.forEach((item) => {
                console.log(item);
                $cells[i].style.backgroundColor = item;
                i++;
            })
        default:
            break;
    }
};

ws.onclose = () => {
    console.log("WebSocket disconnected");
};

ws.onerror = (error) => {
    console.error("Network error, ", error);
};

// helper functions
