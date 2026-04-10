self.onmessage = function(event) {
    const rgbBuffer = event.data;
    const rgbView = new Uint8Array(rgbBuffer);
    const pixelCount = rgbView.length / 3;
    const rgbaView = new Uint8ClampedArray(pixelCount * 4);
    let j = 0;
    for (let i = 0; i < rgbView.length; i += 3) {
        rgbaView[j++] = rgbView[i];     // R
        rgbaView[j++] = rgbView[i+1];   // G
        rgbaView[j++] = rgbView[i+2];   // B
        rgbaView[j++] = 255;            // A
    }
    self.postMessage(rgbaView.buffer, [rgbaView.buffer]);
};