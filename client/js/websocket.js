/* variable and const definition */
const $cells = document.getElementsByClassName('grid-cell');
const $x = document.getElementById('xInput');
const $y = document.getElementById('yInput');
const $submitBtn = document.getElementById('submitBtn');
const $colorInput = document.getElementById('colorInput');
let rows = 16;
let cols = 16;
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
const $brushSize = document.getElementById('brushModeSelect');
const $widthInput = document.getElementById('widthInput');
const $heightInput = document.getElementById('heightInput');

const queryString = window.location.search;
const urlParams = new URLSearchParams(queryString);
const currentUsername = urlParams.get('username');

/* helper functions */
// 初始化画布
const init_canvas = (width, height) => {
    const size = canvas_width / width;
    $gridBoard.style.gridTemplateColumns = `repeat(${width}, ${size}px)`;
    $gridBoard.innerHTML = `<div class="grid-cell" style='width: ${size}px; height: ${size}px;' role="gridcell"></div>`.repeat(width * height);

    // 给每一个像素绑定一个onclick监听
    Array.from($cells).forEach((item) => {
        item.addEventListener("click", async(event) => {
            event.preventDefault();
            const index = Array.from($cells).indexOf(item);
            await updateSubmission(index, parseInt($brushSize.value));
        })
    })
}

const getGridIndex = () => {
    return parseInt($x.value) + parseInt($y.value) * rows;
};

const getGridPosition = (index) => {
    return [index % cols, Math.floor(index / rows)];
}

const getIndexIndices = (index, size) => {
    const parsedIndex = parseInt(index, 10);
    const parsedSize = parseInt(size, 10);

    if (Number.isNaN(parsedIndex) || parsedIndex < 0 || parsedIndex >= rows * cols) {
        return [];
    }

    const blockSize = Number.isNaN(parsedSize) || parsedSize < 1 ? 1 : parsedSize;
    const centerX = parsedIndex % cols;
    const centerY = Math.floor(parsedIndex / cols);

    // Keep index as center cell for odd sizes; for even sizes, extend one extra cell to the right/bottom.
    const left = centerX - Math.floor((blockSize - 1) / 2);
    const right = centerX + Math.floor(blockSize / 2);
    const top = centerY - Math.floor((blockSize - 1) / 2);
    const bottom = centerY + Math.floor(blockSize / 2);

    const indices = [];
    for (let y = top; y <= bottom; y++) {
        for (let x = left; x <= right; x++) {
            if (x >= 0 && x < cols && y >= 0 && y < rows) {
                indices.push(y * cols + x);
            }
        }
    }

    return indices;
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

// 导出png
class MiniPillow {
    constructor(width, height) {
        // 在内存里偷偷创建一个不可见的透明画板喵
        this.canvas = document.createElement('canvas');
        this.canvas.width = width;
        this.canvas.height = height;
        this.ctx = this.canvas.getContext('2d');
    }

    // 完美复刻 Python 的 putpixel 习惯喵！
    putpixel(x, y, color) {
        this.ctx.fillStyle = color; // 支持 '#FF0000' 或 'rgba(255,0,0,1)' 格式
        this.ctx.fillRect(x, y, canvas_width/this.canvas.width, canvas_width/this.canvas.height); // 画一个 1x1 大小的方块
    }

    // 完美复刻 Python 的 save 习惯喵！
    save(filename = 'my-art.bmp') {
        this.canvas.toBlob((blob) => {
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            a.click();
            URL.revokeObjectURL(url); // 乖乖打扫战场喵
        }, 'image/bmp');
    }
}

function exportMyDivArt() {
    const img = new MiniPillow(rows, cols); // 新建画布
    Array.from($cells).forEach((cell) => {
        const index = Array.from($cells).indexOf(cell);
        const [x, y] = getGridPosition(index);
        const color = window.getComputedStyle(cell).backgroundColor;
        img.putpixel(x, y, color);
    });
    img.save('my-idol-pixel.bmp');
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

// 通过submit按钮方式提交更新
$submitBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const index = getGridIndex();
    await updateSubmission(index, parseInt($brushSize.value));
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
        width: parseInt($widthInput.value),
        height: parseInt($heightInput.value)
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
    exportMyDivArt();
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
const updateSubmission = async (index, size) => {
    const color = $colorInput.value;
    let payload = {};
    if (size === 1){
        payload = {
            type: 'pixel_update',
            index: index,
            color: color,
        }
    } else {
        payload = {
            type: 'square_update',
            index: index,
            color: color,
            size: size
        }
    }

    if (!CSS.supports("color", color)) {
        $warningMessage.textContent = "Invalid color";
        return;
    }
    getIndexIndices(index, size).forEach((i) => {
        $cells[i].style.backgroundColor = color;
    });
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(payload));
        console.log(`send to server：index ${index} in color ${color} in size ${size}! `);
    } else {
        console.warn("WebSocket disconnected");
    }
}
// 收到广播消息更改像素
ws.onmessage = (event) => {
    const update = JSON.parse(event.data);
    let i = 0;
    switch (update.type) {
        case "user_joined":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} joined the document`+"</p>" + $opHistory.innerHTML;
            break;
        case "pixel_update":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} changed index：${update.index} in color ${update.color} for file ${update.filename}`+"</p>" + $opHistory.innerHTML;
            $cells[update.index].style.backgroundColor = update.color;
            break;
        case "square_update":
            getIndexIndices(update.index, update.size).forEach((i) => {
                $cells[i].style.backgroundColor = update.color;
            });
            break;
        case "user_left":
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} left the document`+"</p>" + $opHistory.innerHTML;
            break;
        case "canvas":
            rows = update.width;
            cols = update.height;
            init_canvas(rows, cols);
            console.log(update);
            $opHistory.innerHTML = "<p class=\"log-item\">"+`Get canvas from server`+"</p>" + $opHistory.innerHTML;
            update.canvas.forEach((item) => {
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
            $opHistory.innerHTML = "<p class=\"log-item\">"+`${update.time} User ${update.username} undone：${update.index} in color ${update.color} for file ${update.filename}`+"</p>" + $opHistory.innerHTML;
            update.indices.forEach((item) => {
                $cells[item].style.backgroundColor = update.colors[i];
                i++;
            })
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
