/* variable and const definition */
let height = 16;
let width = 16;
let canvas_width = 320;
let scale = 20;
const targetCanvasWidthPx = 500;
const maxOpHistory = 10;
let infoListMode = "none";

const setInfoListMode = (mode) => {
    infoListMode = mode;
    $fileListBtn.classList.toggle("file-list-mode-active", mode === "normal");
    $deleteFileBtn.classList.toggle("delete-mode-active", mode === "delete");
}

const $x = document.getElementById('xInput');
const $y = document.getElementById('yInput');
const $submitBtn = document.getElementById('submitBtn');
const $colorInput = document.getElementById('colorInput');

const $warningMessage = document.getElementById("warningMessage");
const $opHistory = document.getElementById("opHistory");

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
const $deleteFileBtn = document.getElementById('deleteFileBtn');

const $startXInput = document.getElementById('startXInput');
const $startYInput = document.getElementById('startYInput');
const $endXInput = document.getElementById('endXInput');
const $endYInput = document.getElementById('endYInput');
const $drawLineBtn = document.getElementById('drawLineBtn');

const $circleCenterXInput = document.getElementById('circleCenterXInput');
const $circleCenterYInput = document.getElementById('circleCenterYInput');
const $circleRadiusInput = document.getElementById('circleRadiusInput');
const $drawCircleBtn = document.getElementById('drawCircleBtn');

const queryString = window.location.search;
const urlParams = new URLSearchParams(queryString);
const currentUsername = urlParams.get('username');

const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const expandWorker = new Worker('js/worker.js'); // 后台读图线程

const $scaleInput = document.getElementById('scaleInput');
const $scaleBtn = document.getElementById('scaleBtn');

const $imageUploader = document.getElementById('imageUploader');

const applyScale = (nextScale) => {
    if (!Number.isFinite(nextScale) || nextScale < 1) {
        return;
    }

    scale = Math.round(nextScale);
    canvas_width = width * scale;
    canvas.style.width = `${width * scale}px`;
    canvas.style.height = `${height * scale}px`;
    $scaleInput.value = String(scale);
}

const autoAdjustScale = () => {
    if (!Number.isFinite(width) || width <= 0 || !Number.isFinite(height) || height <= 0) {
        return;
    }

    const nextScale = Math.max(1, Math.round(targetCanvasWidthPx / width));
    applyScale(nextScale);
}

const hideInfoList = () => {
    $infoList.style.display = "none";
    $infoList.innerHTML = "";
}

const showInfoList = () => {
    $infoList.style.display = "block";
}

const scrollPageToBottom = () => {
    requestAnimationFrame(() => {
        window.scrollTo({ top: document.documentElement.scrollHeight, behavior: "smooth" });
    });
}

/* helper functions */
const pixelPaint = (index, color) => {
    ctx.fillStyle = color;
    const position = getGridPosition(index);
    ctx.fillRect(position[0], position[1], 1, 1);
}

const getGridIndex = () => {
    return parseInt($x.value) + parseInt($y.value) * width;
};

const getGridPosition = (index) => {
    return [index % width, Math.floor(index / width)];
}

const getIndexIndices = (index, size) => {
    const parsedIndex = parseInt(index, 10);
    const parsedSize = parseInt(size, 10);

    if (Number.isNaN(parsedIndex) || parsedIndex < 0 || parsedIndex >= height * width) {
        return [];
    }

    const blockSize = Number.isNaN(parsedSize) || parsedSize < 1 ? 1 : parsedSize;
    const centerX = parsedIndex % width;
    const centerY = Math.floor(parsedIndex / width);

    // Keep index as center cell for odd sizes; for even sizes, extend one extra cell to the right/bottom.
    const left = centerX - Math.floor((blockSize - 1) / 2);
    const right = centerX + Math.floor(blockSize / 2);
    const top = centerY - Math.floor((blockSize - 1) / 2);
    const bottom = centerY + Math.floor(blockSize / 2);

    const indices = [];
    for (let y = top; y <= bottom; y++) {
        for (let x = left; x <= right; x++) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                indices.push(y * width + x);
            }
        }
    }

    return indices;
}

// 通过起始和终止坐标绘制直线（使用Bresenham算法）
const getLineIndices = (startIndex, endIndex) => {
    const [startX, startY] = getGridPosition(startIndex);
    const [endX, endY] = getGridPosition(endIndex);

    const indices = [];
    const dx = Math.abs(endX - startX);
    const dy = Math.abs(endY - startY);
    const sx = startX < endX ? 1 : -1;
    const sy = startY < endY ? 1 : -1;
    let err = dx - dy;

    let x = startX;
    let y = startY;

    while (true) {
        indices.push(y * width + x);

        if (x === endX && y === endY) break;

        const e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }

    return indices;
}

// 绘制圆
const getCircleIndices = (index, radius) => {
    const parsedIndex = parseInt(index, 10);
    const parsedRadius = parseInt(radius, 10);

    if (Number.isNaN(parsedIndex) || parsedIndex < 0 || parsedIndex >= height * width) {
        return [];
    }

    if (Number.isNaN(parsedRadius) || parsedRadius < 0) {
        return [];
    }

    const [centerX, centerY] = getGridPosition(parsedIndex);
    const indices = [];

    let x = parsedRadius;
    let y = 0;
    let err = 0;

    while (x >= y) {
        const points = [
            [centerX + x, centerY + y], [centerX + y, centerY + x],
            [centerX - y, centerY + x], [centerX - x, centerY + y],
            [centerX - x, centerY - y], [centerX - y, centerY - x],
            [centerX + y, centerY - x], [centerX + x, centerY - y],
        ];

        points.forEach(([px, py]) => {
            if (px >= 0 && px < width && py >= 0 && py < height) {
                indices.push(py * width + px);
            }
        });

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }

        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    return indices;
}

// 打印异常消息
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

// 添加操作历史记录显示
const addOpHistory = (entry) => {
    const logItem = document.createElement("p");
    logItem.className = "log-item";
    logItem.textContent = entry;
    $opHistory.prepend(logItem);

    while ($opHistory.children.length > maxOpHistory) {
        $opHistory.removeChild($opHistory.lastElementChild);
    }
}

/* event listeners */
canvas.addEventListener('mousedown', function (event) {
    const rect = canvas.getBoundingClientRect();
    const visualX = event.clientX - rect.left;
    const visualY = event.clientY - rect.top;

    const pixelX = Math.floor(visualX / scale);
    const pixelY = Math.floor(visualY / scale);

    if (pixelX >= 0 && pixelX < width && pixelY >= 0 && pixelY < height) {
        const index = pixelY * width + pixelX;
        updateSubmission(index, parseInt($brushSize.value));
    } else {
        $warningMessage.textContent = "Invalid update data.";
    }
});

// 给每一个文件栏绑定一个onclick监听
const addFileEntryListener = () => {
    Array.from($fileEntry).forEach((item) => {
        item.addEventListener("click", async (event) => {
            event.preventDefault();
            hideInfoList();
            setInfoListMode("none");
            const payload = {
                "type": "switch_file",
                "filename": item.textContent
            }
            console.log(payload);
            ws.send(JSON.stringify(payload));
        })
    })
}

// 删除模式下绑定左键删除
const addDeleteFileEntryListener = () => {
    Array.from($fileEntry).forEach((item) => {
        item.addEventListener("click", async (event) => {
            event.preventDefault();
            if (event.button !== 0) {
                return;
            }
            hideInfoList();
            setInfoListMode("none");
            const payload = {
                type: "delete_file",
                filename: item.textContent.trim()
            };
            ws.send(JSON.stringify(payload));
        });
    });
}

// 通过submit按钮方式提交更新
$submitBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const index = getGridIndex();
    await updateSubmission(index, parseInt($brushSize.value));
});

// 调整缩放
$scaleBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const nextScale = parseInt($scaleInput.value, 10);
    applyScale(nextScale);
})

// 删除文件
$deleteFileBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    if (infoListMode === "delete") {
        hideInfoList();
        setInfoListMode("none");
        return;
    }
    setInfoListMode("delete");
    showInfoList();
    const payload = { type: "get_file_list" };
    ws.send(JSON.stringify(payload));
})

// 绘制直线工具
$drawLineBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    const startIndex = parseInt($startXInput.value) + parseInt($startYInput.value) * width;
    const endIndex = parseInt($endXInput.value) + parseInt($endYInput.value) * width;
    if (startIndex >= 0 && endIndex >= 0 && startIndex < height * width && endIndex < height * width) {
        const payload = {
            "type": "line_update",
            "start_index": startIndex,
            "end_index": endIndex,
            "color": $colorInput.value
        }
        if (!CSS.supports("color", $colorInput.value)) {
            $warningMessage.textContent = "Invalid color";
            return;
        }
        ws.send(JSON.stringify(payload));
    } else {
        $warningMessage.textContent = "Invalid Index";
    }
});

// 绘制圆工具
if ($drawCircleBtn && $circleCenterXInput && $circleCenterYInput && $circleRadiusInput) {
    $drawCircleBtn.addEventListener('click', async (event) => {
        event.preventDefault();

        const centerX = parseInt($circleCenterXInput.value, 10);
        const centerY = parseInt($circleCenterYInput.value, 10);
        const radius = parseInt($circleRadiusInput.value, 10);

        if (Number.isNaN(centerX) || Number.isNaN(centerY) || Number.isNaN(radius)) {
            $warningMessage.textContent = "Invalid circle input";
            return;
        }

        const centerIndex = centerX + centerY * width;
        if (centerIndex < 0 || centerIndex >= height * width || radius < 0) {
            $warningMessage.textContent = "Invalid circle input";
            return;
        }

        if (!CSS.supports("color", $colorInput.value)) {
            $warningMessage.textContent = "Invalid color";
            return;
        }

        const payload = {
            type: "circle_update",
            index: centerIndex,
            radius: radius,
            color: $colorInput.value
        };
        ws.send(JSON.stringify(payload));
    });
}

// 获取目录内文件名单
$fileListBtn.addEventListener('click', async (event) => {
    event.preventDefault();
    if (infoListMode === "normal") {
        hideInfoList();
        setInfoListMode("none");
        return;
    }
    setInfoListMode("normal");
    showInfoList();
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
    setInfoListMode("none");
    showInfoList();
    const payload = {type: "get_user_list"};
    ws.send(JSON.stringify(payload));
})

// 把图片打包成二进制发给服务端
$imageUploader.addEventListener('change', async (event) => {
    event.preventDefault();
    const file = event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = function(e) {
        // 读图
        const img = new Image();
        img.onload = function() {
            const offscreenCanvas = document.createElement('canvas');
            offscreenCanvas.width = img.naturalWidth;   // 16
            offscreenCanvas.height = img.naturalHeight;  // 16
            const offCtx = offscreenCanvas.getContext('2d');
            offCtx.drawImage(img, 0, 0);

            // 包装二进制数据
            const imageData = offCtx.getImageData(0, 0, offscreenCanvas.width, offscreenCanvas.height).data;
            const buffer = new ArrayBuffer(4 + imageData.byteLength);
            const view = new DataView(buffer);
            view.setUint16(0, img.naturalWidth, true);
            view.setUint16(2, img.naturalHeight, true);
            const payloadView = new Uint8Array(buffer, 4);
            payloadView.set(imageData);
            ws.send(buffer);
            console.log("Uploaded image sent to server");
            event.target.value = '';
        }
        img.src = e.target.result;
    }
    reader.readAsDataURL(file);
})

/* websocket connection for edit */
const ws = new WebSocket('ws://localhost:1145/ws/edit');
ws.binaryType = 'arraybuffer';

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
    if (size === 1) {
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

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(payload));
        console.log(`send to server：index ${index} in color ${color} in size ${size}! `);
    } else {
        console.warn("WebSocket disconnected");
    }
}
// 收到广播消息更改像素
ws.onmessage = (event) => {
    if (typeof event.data === "string") {
        const update = JSON.parse(event.data);
        let i = 0;
        switch (update.type) {
            case "canvas_incoming":
                height = update.height;
                width = update.width;
                autoAdjustScale();
                break;
            case "user_joined":
                addOpHistory(`${update.time} User ${update.username} joined the document.`);
                break;
            case "pixel_update":
                addOpHistory(`${update.time} User ${update.username} changed index：${update.index} in color ${update.color} for file ${update.filename}`);
                pixelPaint(update.index, update.color);
                break;
            case "square_update":
                getIndexIndices(update.index, update.size).forEach((i) => {
                    pixelPaint(i, update.color);
                });
                break;
            case "line_update":
                getLineIndices(update.start_index, update.end_index).forEach((i) => {
                    pixelPaint(i, update.color);
                });
                addOpHistory(`${update.time} User ${update.username} drew a line in color ${update.color} for file ${update.filename}`);
                break;
            case "circle_update":
                console.log("Circle update");
                getCircleIndices(update.index, update.radius).forEach((i) => {
                    pixelPaint(i, update.color);
                });
                addOpHistory(`${update.time} User ${update.username} drew a circle in color ${update.color} for file ${update.filename}`);
                break;
            case "user_list":
                $infoList.innerHTML = "";
                update.users.forEach((item) => {
                    $infoList.innerHTML += `<p>${item}</p>`;
                })
                break;
            case "file_list":
                $infoList.innerHTML = "";
                if (infoListMode === "delete") {
                    update.files.forEach((item) => {
                        $infoList.innerHTML += `<p class="file-entry file-entry-delete">${item}</p>`;
                    });
                    addDeleteFileEntryListener();
                } else {
                    update.files.forEach((item) => {
                        $infoList.innerHTML += `<p class="file-entry">${item}</p>`;
                    });
                    $infoList.innerHTML += `<p style="color: red"> Current Working on: ${update.current_working} </p>`;
                    addFileEntryListener();
                }
                break;
            case "user_switched_file":
                addOpHistory(`${update.time} User ${update.username} switched current file to ${update.filename}`);
                break;
            case "user_undone":
                addOpHistory(`${update.time} User ${update.username} undone：${update.index} in color ${update.color} for file ${update.filename}`);
                update.indices.forEach((item) => {
                    pixelPaint(update.indices[i], update.colors[i]);
                    i++;
                })
                break;
            case "file_deleted":
                addOpHistory(`${update.time} ${update.filename} was deleted`);
                break;
            case "exception":
                exceptionHandler(update.message);
                break;
            default:
                break;
        }
    } else if (event.data instanceof ArrayBuffer) {
        expandWorker.postMessage(event.data, [event.data]);
    }
};

expandWorker.onmessage = function (event) {
    const finalBuffer = event.data;
    const clampedArray = new Uint8ClampedArray(finalBuffer);
    // 极其丝滑地包装成 ImageData 并上屏！
    const imageData = new ImageData(clampedArray, width, height);
    canvas.width = width;
    canvas.height = height;
    applyScale(scale);
    ctx.putImageData(imageData, 0, 0);
    scrollPageToBottom();
    console.log("imageData received and rendered on canvas");
};

ws.onclose = () => {
    console.log("WebSocket disconnected");
};

ws.onerror = (error) => {
    console.error("Network error, ", error);
};