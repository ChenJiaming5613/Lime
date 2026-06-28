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

const context = canvas.getContext('webgpu');
const presentationFormat = navigator.gpu.getPreferredCanvasFormat();

context.configure({
  device,
  format: presentationFormat,
  alphaMode: 'premultiplied',
});

const shaderCode = `
  struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
  };

  @vertex
  fn vs_main(
    @location(0) position: vec2f,
    @location(1) color: vec3f,
  ) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(position, 0.0, 1.0);
    out.color = color;
    return out;
  }

  @fragment
  fn fs_main(@location(0) color: vec3f) -> @location(0) vec4f {
    return vec4f(color, 1.0);
  }
`;

const shaderModule = device.createShaderModule({ code: shaderCode });

const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: {
    module: shaderModule,
    entryPoint: 'vs_main',
    buffers: [
      {
        arrayStride: 5 * 4, // 每个顶点 5 个 float（2 位置 + 3 颜色），共 20 字节
        attributes: [
          {
            shaderLocation: 0,
            offset: 0,
            format: 'float32x2',
          },
          {
            shaderLocation: 1,
            offset: 2 * 4,
            format: 'float32x3',
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

// 每个顶点: x, y, r, g, b
const vertices = new Float32Array([
   0.0,  0.8,   1.0, 0.0, 0.0,  // 顶部 - 红色
  -0.7, -0.5,   0.0, 1.0, 0.0,  // 左下 - 绿色
   0.7, -0.5,   0.0, 0.0, 1.0,  // 右下 - 蓝色
]);

const vertexBuffer = device.createBuffer({
  size: vertices.byteLength,
  usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
});

device.queue.writeBuffer(vertexBuffer, 0, vertices);

function frame() {
  const encoder = device.createCommandEncoder();
  const textureView = context.getCurrentTexture().createView();

  const renderPass = encoder.beginRenderPass({
    colorAttachments: [
      {
        view: textureView,
        clearValue: { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
        loadOp: 'clear',
        storeOp: 'store',
      },
    ],
  });

  renderPass.setPipeline(pipeline);
  renderPass.setVertexBuffer(0, vertexBuffer);
  renderPass.draw(3);
  renderPass.end();

  device.queue.submit([encoder.finish()]);
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);

function showError(msg) {
  errorEl.style.display = 'block';
  errorEl.textContent = msg;
}
