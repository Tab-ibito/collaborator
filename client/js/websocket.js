/* variable and const definition */
const $cells = document.getElementsByClassName('grid-cell');
const $x = document.getElementById('xInput');
const $y = document.getElementById('yInput');
const $submitBtn = document.getElementById('submitBtn');
const rows = 4;
const cols = 4;
let currentColor = "#FFA500";

/* grid processing function */
const getGridIndex = () => {
    return parseInt($x.value) + parseInt($y.value) * rows;
};

/* websocket connection for edit */
const ws = new WebSocket('ws://localhost:1145/ws/edit');

// 建立连接
ws.onopen = () => {
    console.log("WebSocket connection opened");
    ws.send("test");
};

// 提交更改像素的请求，并顺带更改本地
$submitBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const index = getGridIndex();
    const color = currentColor;
    const payload = {
        index: index,
        color: color
    }
    console.log(index);
    $cells[index].style.backgroundColor = currentColor;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(payload));
        console.log(`send to server：index ${index} in color ${currentColor} ！`);
    } else {
        console.warn("WebSocket disconnected");
    }
});

// 收到广播消息更改像素
ws.onmessage = (event) => {
    const update = JSON.parse(event.data);
    const index = parseInt(update.index);
    if (update.type === 'pixel_update') {
        console.log(`index：${update.index} in color ${update.color}`);
    }
    $cells[index].style.backgroundColor = currentColor;
};

ws.onclose = () => {
    console.log("WebSocket disconnected");
};

ws.onerror = (error) => {
    console.error("Network error, ", error);
};