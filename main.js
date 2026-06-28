// ====== 1. 获取 GPU 适配器和设备 ======
const canvas = document.getElementById('canvas');
const errorEl = document.getElementById('error');

if (!navigator.gpu) {
  showError('当前浏览器不支持 WebGPU，请使用 Chrome 113+ 或 Edge 113+');
  throw new Error('WebGPU not supported');
}

const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  showError('无法获取 GPU 适配器');
  throw new Error('No adapter');
}

const device = await adapter.requestDevice();

// ====== 2. 配置 Canvas 上下文 ======
const context = canvas.getContext('webgpu');
const presentationFormat = navigator.gpu.getPreferredCanvasFormat();

context.configure({
  device,
  format: presentationFormat,
  alphaMode: 'premultiplied',
});

// ====== 3. WGSL 着色器代码 ======
// 顶点着色器：将顶点坐标输出到裁剪空间
// 片段着色器：为每个像素输出颜色
const shaderCode = `
  @vertex
  fn vs_main(@location(0) position: vec2f) -> @builtin(position) vec4f {
    return vec4f(position, 0.0, 1.0);
  }

  @fragment
  fn fs_main() -> @location(0) vec4f {
    return vec4f(0.2, 0.6, 1.0, 1.0);  // 蓝色
  }
`;

const shaderModule = device.createShaderModule({ code: shaderCode });

// ====== 4. 创建渲染管线 ======
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: {
    module: shaderModule,
    entryPoint: 'vs_main',
    buffers: [
      {
        arrayStride: 2 * 4, // 每个顶点 2 个 float，共 8 字节
        attributes: [
          {
            shaderLocation: 0,
            offset: 0,
            format: 'float32x2',
          },
        ],
      },
    ],
  },
  fragment: {
    module: shaderModule,
    entryPoint: 'fs_main',
    targets: [
      {
        format: presentationFormat,
      },
    ],
  },
  primitive: {
    topology: 'triangle-list',
  },
});

// ====== 5. 创建顶点缓冲区（三角形） ======
// 裁剪空间坐标范围: x[-1, 1], y[-1, 1]
const vertices = new Float32Array([
   0.0,  0.8,   // 顶部顶点
  -0.7, -0.5,   // 左下顶点
   0.7, -0.5,   // 右下顶点
]);

const vertexBuffer = device.createBuffer({
  size: vertices.byteLength,
  usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
});

device.queue.writeBuffer(vertexBuffer, 0, vertices);

// ====== 6. 渲染循环 ======
function frame() {
  // 创建命令编码器
  const encoder = device.createCommandEncoder();

  // 获取当前纹理视图
  const textureView = context.getCurrentTexture().createView();

  // 开始渲染通道
  const renderPass = encoder.beginRenderPass({
    colorAttachments: [
      {
        view: textureView,
        clearValue: { r: 0.05, g: 0.05, b: 0.1, a: 1.0 },  // 深色背景
        loadOp: 'clear',
        storeOp: 'store',
      },
    ],
  });

  // 绑定渲染管线
  renderPass.setPipeline(pipeline);
  // 绑定顶点缓冲区
  renderPass.setVertexBuffer(0, vertexBuffer);
  // 绘制 3 个顶点（1 个三角形）
  renderPass.draw(3);

  // 结束渲染通道
  renderPass.end();

  // 提交命令
  device.queue.submit([encoder.finish()]);

  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);

function showError(msg) {
  errorEl.style.display = 'block';
  errorEl.textContent = msg;
}
