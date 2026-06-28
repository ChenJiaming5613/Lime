import shaderCode from './assets/shaders/cube.wgsl?raw';
import { mat4, vec3 } from 'wgpu-matrix';

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const errorEl = document.getElementById('error') as HTMLElement;

if (!navigator.gpu) {
  showError('WebGPU is not supported. Please use Chrome 113+ or Edge 113+.');
  throw new Error('WebGPU not supported');
}

const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  showError('Failed to get GPU adapter.');
  throw new Error('No adapter');
}

const device = await adapter.requestDevice();

const context = canvas.getContext('webgpu') as GPUCanvasContext;
const presentationFormat = navigator.gpu.getPreferredCanvasFormat();

context.configure({
  device,
  format: presentationFormat,
  alphaMode: 'premultiplied',
});

let depthTexture = device.createTexture({
  size: [canvas.width, canvas.height],
  format: 'depth24plus',
  usage: GPUTextureUsage.RENDER_ATTACHMENT,
});

function resize() {
  const dpr = Math.min(window.devicePixelRatio, 2);
  const width = Math.floor(canvas.clientWidth * dpr);
  const height = Math.floor(canvas.clientHeight * dpr);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
    depthTexture.destroy();
    depthTexture = device.createTexture({
      size: [canvas.width, canvas.height],
      format: 'depth24plus',
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
  }
}
window.addEventListener('resize', resize);
resize();

const shaderModule = device.createShaderModule({ code: shaderCode });

const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: {
    module: shaderModule,
    entryPoint: 'vs_main',
    buffers: [
      {
        arrayStride: 6 * 4, // 3 position + 3 color = 24 bytes
        attributes: [
          {
            shaderLocation: 0,
            offset: 0,
            format: 'float32x3',
          },
          {
            shaderLocation: 1,
            offset: 3 * 4,
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
  depthStencil: {
    depthWriteEnabled: true,
    depthCompare: 'less',
    format: 'depth24plus',
  },
});

// Cube vertices: 6 faces x 4 vertices = 24 vertices (position + color)
const vertices = new Float32Array([
  // Front face (red)
  -0.5, -0.5,  0.5,  1.0, 0.0, 0.0,
   0.5, -0.5,  0.5,  1.0, 0.0, 0.0,
   0.5,  0.5,  0.5,  1.0, 0.0, 0.0,
  -0.5,  0.5,  0.5,  1.0, 0.0, 0.0,
  // Back face (green)
  -0.5, -0.5, -0.5,  0.0, 1.0, 0.0,
   0.5, -0.5, -0.5,  0.0, 1.0, 0.0,
   0.5,  0.5, -0.5,  0.0, 1.0, 0.0,
  -0.5,  0.5, -0.5,  0.0, 1.0, 0.0,
  // Top face (blue)
  -0.5,  0.5,  0.5,  0.0, 0.0, 1.0,
   0.5,  0.5,  0.5,  0.0, 0.0, 1.0,
   0.5,  0.5, -0.5,  0.0, 0.0, 1.0,
  -0.5,  0.5, -0.5,  0.0, 0.0, 1.0,
  // Bottom face (yellow)
  -0.5, -0.5,  0.5,  1.0, 1.0, 0.0,
   0.5, -0.5,  0.5,  1.0, 1.0, 0.0,
   0.5, -0.5, -0.5,  1.0, 1.0, 0.0,
  -0.5, -0.5, -0.5,  1.0, 1.0, 0.0,
  // Right face (cyan)
   0.5, -0.5,  0.5,  0.0, 1.0, 1.0,
   0.5, -0.5, -0.5,  0.0, 1.0, 1.0,
   0.5,  0.5, -0.5,  0.0, 1.0, 1.0,
   0.5,  0.5,  0.5,  0.0, 1.0, 1.0,
  // Left face (magenta)
  -0.5, -0.5,  0.5,  1.0, 0.0, 1.0,
  -0.5, -0.5, -0.5,  1.0, 0.0, 1.0,
  -0.5,  0.5, -0.5,  1.0, 0.0, 1.0,
  -0.5,  0.5,  0.5,  1.0, 0.0, 1.0,
]);

const indices = new Uint16Array([
  // Front
  0, 1, 2, 0, 2, 3,
  // Back
  5, 4, 7, 5, 7, 6,
  // Top
  8, 9, 10, 8, 10, 11,
  // Bottom
  12, 14, 13, 12, 15, 14,
  // Right
  16, 17, 18, 16, 18, 19,
  // Left
  20, 21, 22, 20, 22, 23,
]);

const vertexBuffer = device.createBuffer({
  size: vertices.byteLength,
  usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
});

const indexBuffer = device.createBuffer({
  size: indices.byteLength,
  usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
});

device.queue.writeBuffer(vertexBuffer, 0, vertices);
device.queue.writeBuffer(indexBuffer, 0, indices);

const uniformBuffer = device.createBuffer({
  size: 64, // 4x4 float matrix
  usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
});

const bindGroup = device.createBindGroup({
  layout: pipeline.getBindGroupLayout(0),
  entries: [
    {
      binding: 0,
      resource: { buffer: uniformBuffer },
    },
  ],
});

const startTime = performance.now();

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
    depthStencilAttachment: {
      view: depthTexture.createView(),
      depthClearValue: 1.0,
      depthLoadOp: 'clear',
      depthStoreOp: 'store',
    },
  });

  const time = (performance.now() - startTime) / 1000;
  const aspect = canvas.width / canvas.height;

  const model = mat4.rotation(vec3.normalize([1, 1, 1]), time);
  const view = mat4.lookAt([0, 0, 3], [0, 0, 0], [0, 1, 0]);
  const projection = mat4.perspective(60 * Math.PI / 180, aspect, 0.1, 100.0);
  const mvp = mat4.multiply(projection, mat4.multiply(view, model));

  device.queue.writeBuffer(uniformBuffer, 0, mvp);

  renderPass.setPipeline(pipeline);
  renderPass.setBindGroup(0, bindGroup);
  renderPass.setVertexBuffer(0, vertexBuffer);
  renderPass.setIndexBuffer(indexBuffer, 'uint16');
  renderPass.drawIndexed(36);
  renderPass.end();

  device.queue.submit([encoder.finish()]);
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);

function showError(msg: string) {
  errorEl.style.display = 'block';
  errorEl.textContent = msg;
}
