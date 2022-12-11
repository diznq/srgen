
/**
 * Stream video into container
 * @param {HTMLElement} container 
 */
async function StreamVideo(container){

    const VS_SHADER = `
attribute vec4 pos;
void main() {
    gl_Position = pos;
}`;

    const PS_SHADER = `
    precision highp float;
    uniform float w;
    uniform float h;
    uniform sampler2D uSampler;
    uniform sampler2D lut;

    float modI(float a, float b) {
        float m=a-floor((a+0.5)/b)*b;
        return floor(m+0.5);
    }

    vec3 lookup(vec3 textureColor) {
        textureColor = clamp(textureColor, 0.0, 1.0);
        vec2 bg = (vec2(textureColor.b, textureColor.g) / 8.0) * (255.0 / 256.0);
        float r = floor(textureColor.r * 255.0);
        float row = floor(r / 32.0) / 8.0;
        float col = floor(modI(r, 32.0) / 4.0) / 8.0;
        vec2 pos = clamp(vec2(col, row) + bg, 0.0, 1.0);
        return texture2D(lut, pos).rgb;
    }
    
    void main() {
        vec2 pos = gl_FragCoord.xy / vec2(w, h);
        pos.y = 1.0 - pos.y;
        vec3 color = texture2D(uSampler, pos).rgb;
        gl_FragColor = vec4(lookup(color), 1.0);
    }
    `;

    const canvas = container instanceof HTMLCanvasElement ? container : container.querySelectorAll("canvas")[0];
    /**
     * @type{WebGLRenderingContext}
     */
    const ctx = canvas.getContext("webgl");
    const video = document.createElement("video");
    const controls = container.querySelectorAll("div.video-controls")[0] || false;
    const videoTimestamp = container.querySelectorAll("div.video-time")[0] || false;
    const videoTotalTime = container.querySelectorAll("div.video-total-time")[0] || false;
    const videoSizer = container.querySelectorAll("div.video-track-sizer")[0] || false;
    const videoLine = container.querySelectorAll("div.video-track-line")[0] || false;
    const videoFullscreen = container.querySelectorAll("div.video-fullscreen")[0] || false;

    const videoBuffers = [];
    const shaderProgram = initShaderProgram(ctx, VS_SHADER, PS_SHADER);
    const texture = initTexture(ctx, true);
    const lut = initTexture(ctx);
    const buffers = initBuffers(ctx);

    const img = document.getElementById("palette");
    img.onload = () => {
        updateTexture(ctx, lut, img);
        if(video.paused) safeRequestFrame(onFrame);
    }

    updateTexture(ctx, lut, img);

    const programInfo = {
        program: shaderProgram,
        attribLocations: {
            vertexPosition: ctx.getAttribLocation(shaderProgram, "pos"),
        },
        uniformLocations: {
            uSampler: ctx.getUniformLocation(shaderProgram, "uSampler"),
            lut: ctx.getUniformLocation(shaderProgram, "lut"),
            w: ctx.getUniformLocation(shaderProgram, "w"),
            h: ctx.getUniformLocation(shaderProgram, "h")
        },
      };

    /**
     * Stringify time
     * @param {number} t 
     * @param {boolean?} includeHour 
     */
    function toHumanTime(t, includeHour){
        includeHour = includeHour || false;
        const s = Math.floor(t) % 60;
        const min = Math.floor(t / 60) % 60;
        const hr = Math.floor(t / 3600);
        const strSeconds = s < 10 ? ("0" + s) : s;
        const strMin = min < 10 ? ("0" + min) : min;
        const strHr = hr;
        if(includeHour){
            return `${strHr}:${strMin}:${strSeconds}`;
        }
        return `${strMin}:${strSeconds}`
    }

    video.addEventListener("loadedmetadata", function() {
        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;
        console.log([canvas.width, canvas.height, video.videoWidth, video.videoHeight])
        if(videoTotalTime){
            videoTotalTime.textContent = toHumanTime(video.duration, video.duration >= 3600);
        }
    });

    video.addEventListener("timeupdate", function(){
        if(videoTimestamp){
            const includeHour = video.duration >= 3600;
            videoTimestamp.textContent = toHumanTime(video.currentTime, includeHour);
        }
        if(videoLine){
            for(var i = videoBuffers.length; i < video.buffered.length; i++){
                const videoBuffer = document.createElement("div");
                videoBuffer.className = "video-track-buffer";
                videoLine.appendChild(videoBuffer);
                videoBuffers.push(videoBuffer);
            }
            for(var i=0; i<video.buffered.length; i++){
                const start = video.buffered.start(i);
                const length = video.buffered.end(i) - start;
                const left = 100 * start / video.duration;
                const width = 100 * length / video.duration;
                videoBuffers[i].style.left = left + "%";
                videoBuffers[i].style.width = width + "%"; 
            }
        }
        if(videoSizer){
            videoSizer.style.width = (100 * video.currentTime / video.duration) + "%";
        }
    });

    if(videoLine){
        videoLine.addEventListener("click", function(event){
            if(typeof(event.clientX) != "undefined"){
                const rect = videoLine.getBoundingClientRect();
                const width = rect.width;
                const left = rect.left;
                const posX = event.clientX - left;
                const t = posX / width * video.duration;
                video.pause();
                video.currentTime = t;
                playVideo();
            }
        });
    }

    let isFullscreen = false, lastEvent = Date.now()

    function toggleFullscreen(){
        if(isFullscreen){
            document.exitFullscreen();
        } else {
            container.requestFullscreen();
        }
    }

    if(videoFullscreen){
        videoFullscreen.addEventListener("click", toggleFullscreen);
    }

    canvas.addEventListener("dblclick", toggleFullscreen);
    canvas.addEventListener("mousemove", showControls);
    canvas.addEventListener("mousedown", showControls);
    canvas.addEventListener("touchstart", showControls);

    document.addEventListener("fullscreenchange", function () {
        var fullscreenElement = document.fullscreenElement || document.mozFullScreenElement || document.webkitFullscreenElement || document.msFullscreenElement;
        if (fullscreenElement != null) {
            isFullscreen = true;
        } else {
            isFullscreen = false;             
        }
    });

    let reqeustedFrame = false;

    function showControls(){
        lastEvent = Date.now();
        canvas.style.cursor = "";
        if(controls)
            controls.style.display = "";
    }

    function hideControls(){
        canvas.style.cursor = "none";
        if(controls)
            controls.style.display = "none";
    }

    function safeRequestFrame(fn){
        if(reqeustedFrame) return false;
        requestAnimationFrame( function(){
            reqeustedFrame = false;
            fn();
        });
    }

    function onFrame(){
        const rect = canvas.getBoundingClientRect();
        const cRatio = rect.width / rect.height;
        const vRatio = canvas.width / canvas.height;
        const size = { x: 0, y: 0, width: canvas.width, height: canvas.height };
        if(vRatio > cRatio){
            const ratio = vRatio / cRatio;
            size.height = canvas.height / ratio;
            size.y = (canvas.height - size.height) / 2;
        } else {
            const ratio = cRatio / vRatio;
            size.width = canvas.width / ratio;
            size.x = (canvas.width - size.width) / 2;
        }

        updateTexture(ctx, texture, video);

        ctx.clearColor(0.0, 0.0, 0.0, 1.0);
        ctx.clearDepth(1.0);
        ctx.enable(ctx.DEPTH_TEST);
        ctx.depthFunc(ctx.LEQUAL);
        ctx.clear(ctx.COLOR_BUFFER_BIT | ctx.DEPTH_BUFFER_BIT);
        
        ctx.viewport(0, 0, ctx.canvas.width, ctx.canvas.height);

        setPositionAttribute(ctx, buffers, programInfo);
        ctx.useProgram(programInfo.program);

        ctx.activeTexture(ctx.TEXTURE0);
        ctx.bindTexture(ctx.TEXTURE_2D, texture);
        ctx.activeTexture(ctx.TEXTURE1);
        ctx.bindTexture(ctx.TEXTURE_2D, lut);
        ctx.uniform1i(programInfo.uniformLocations.uSampler, 0);
        ctx.uniform1i(programInfo.uniformLocations.lut, 1);
        ctx.uniform1f(programInfo.uniformLocations.w, canvas.width)
        ctx.uniform1f(programInfo.uniformLocations.h, canvas.height)
      
        ctx.drawArrays(ctx.TRIANGLE_STRIP, 0, 4);
        
        if(!video.paused) {
            safeRequestFrame(onFrame);
            if(Date.now() - lastEvent > 2500){
                hideControls();
            }
        } else showControls();
    }

    function playVideo(){
        video.play();
        safeRequestFrame(onFrame);
    }

    canvas.addEventListener("click", () => {
        if(video.paused){
            playVideo();
        } else video.pause();
    });

    document.getElementById("video-selector").onchange = function(event) {
        var files = this.files;
        if(files.length > 0) {
            video.src = URL.createObjectURL(files[0]);
            document.getElementById("video-select-overlay").style.display = "none";
        }
    }

    document.getElementById("image-selector").onchange = function(event) {
        var files = this.files;
        if(files.length > 0) {
            img.src = URL.createObjectURL(files[0]);
        }
    }

    //video.src = "/kimi720.mp4";
}

window.StreamVideo = StreamVideo;