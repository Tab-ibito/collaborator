/* variable and const definition */
const $cells = document.getElementsByClassName('grid-cell');
const $x = document.getElementById('xInput');
const $y = document.getElementById('yInput');
const $submitBtn = document.getElementById('submitBtn');
const $colorInput = document.getElementById('colorInput');
const rows = 16;
const cols = 16;
const canvas_width = 320;
const $warningMessage = document.getElementById("warningMessage");
const $opHistory = document.getElementById("opHistory");
const $gridBoard = document.getElementById("grid");

const $fileListBtn = document.getElementById("fileListBtn");
const $onlineUsersBtn = document.getElementById("onlineUsersBtn");
const $infoList = document.getElementById("infoList");
const $fileEntry = document.getElementsByClassName('file-entry');
const $fileNameInput = document.getElementById('fileNameInput');
const $createFile = document.getElementById('createFile');
const $createFileBtn = document.getElementById('createFileBtn');
const $undoBtn = document.getElementById('undoBtn');

const queryString = window.location.search;
const urlParams = new URLSearchParams(queryString);
const currentUsername = urlParams.get('username');

/* helper functions */
// 初始化画布
(() => {
    const size = canvas_width / cols;
    $gridBoard.style.gridTemplateColumns = `repeat(${cols}, ${size}px)`;
    $gridBoard.innerHTML = `<div class="grid-cell" style='width: ${size}px; height: ${size}px;' role="gridcell"></div>`.repeat(rows * cols);
})();

const getGridIndex = () => {
    return parseInt($x.value) + parseInt($y.value) * rows;
};

const getGridPosition = (index) => {
    return [index % cols, Math.floor(index / rows)];
}

const exceptionHandler = (message) => {
    console.warn(message);
    if (message === "invalid_username") {
        window.location.href = "./login.html";
    } else if (message === "fail_to_create_file") {
        $warningMessage.textContent = "Failed to create file, please try again.";
    } else if (message === "fail_to_open_file") {
        $warningMessage.textContent = "Failed to open file, please try again.";
    } else if (message === "fail_to_update_pixel") {
        $warningMessage.textContent = "Failed to update pixel, please try again.";
    } else if (message === "fail_to_undo") {
        $warningMessage.textContent = "No edits to undo.";
    } else if (message === "file_not_found") {
        $warningMessage.textContent = "File not found.";
    } else if (message === "unknown_message_type") {
        $warningMessage.textContent = "Unknown message sent.";
    } else if (message === "invalid_pixel_update_data") {
        $warningMessage.textContent = "Invalid pixel update data.";
    } else if (message === "filename_required") {
        $warningMessage.textContent = "File name required";
    }
}

/* event listeners */
// 给每一个文件栏绑定一个onclick监听
const addFileEntryListener = () => {
    Array.from($fileEntry).forEach((item) => {
        item.addEventListener("click", async(event) => {
            event.preventDefault();
            const payload = {
                "type": "switch_file",
                "filename": item.textContent
            }
            console.log(payload);
            ws.send(JSON.stringify(payload));
        })
    })
}

// 给每一个像素绑定一个onclick监听
Array.from($cells).forEach((item) => {
    item.addEventListener("click", async(event) => {
        event.preventDefault();
        const index = Array.from($cells).indexOf(item);
        await updateSubmission(index);
    })
})

// 通过submit按钮方式提交更新
$submitBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const index = getGridIndex();
    await updateSubmission(index);
});

// 获取目录内文件名单
$fileListBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const payload = {type: "get_file_list"};
    ws.send(JSON.stringify(payload));
})

// 提交新文件创建
$createFileBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const payload = {
        type: "create_file",
        filename: $fileNameInput.value,
    };
    ws.send(JSON.stringify(payload));
    $createFile.style.display = "none";
})

// 撤销操作
$undoBtn.addEventListener('click', async (event) => {
    const payload = {
        type: "undo",
    }
    ws.send(JSON.stringify(payload));
})

// 获取在线用户名单
$onlineUsersBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const payload = {type: "get_user_list"};
    ws.send(JSON.stringify(payload));
})

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
const updateSubmission = async (index) => {
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
}
// 收到广播消息更改像素
ws.onmessage = (event) => {
    const update = JSON.parse(event.data);
    switch (update.type) {
        case "user_joined":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} joined the document`+"</p>" + $opHistory.innerHTML;
            break;
        case "pixel_update":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} changed index：${update.index} in color ${update.color}`+"</p>" + $opHistory.innerHTML;
            $cells[update.index].style.backgroundColor = update.color;
            break;
        case "user_left":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} left the document`+"</p>" + $opHistory.innerHTML;
            break;
        case "canvas":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`Get canvas from server`+"</p>" + $opHistory.innerHTML;
            let i = 0;
            update.canvas.forEach((item) => {
                console.log(item);
                $cells[i].style.backgroundColor = item;
                i++;
            })
            break;
        case "user_list":
            $infoList.innerHTML = "";
            update.users.forEach((item) => {
                $infoList.innerHTML += `<p>${item}</p>`;
            })
            break;
        case "file_list":
            $infoList.innerHTML = "";
            update.files.forEach((item) => {
                $infoList.innerHTML += `<p class="file-entry">${item}</p>`;
            })
            $infoList.innerHTML += `<p style="color: red"> Current Working on: ${update.current_working} </p>`;
            addFileEntryListener();
            break;
        case "user_switched_file":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} switched current file to ${update.filename}`+"</p>" + $opHistory.innerHTML;
            break;
        case "user_undone":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} undone：${update.index} in color ${update.color}`+"</p>" + $opHistory.innerHTML;
            $cells[update.index].style.backgroundColor = update.color;
            break;
        case "exception":
            exceptionHandler(update.message);
            break;
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
