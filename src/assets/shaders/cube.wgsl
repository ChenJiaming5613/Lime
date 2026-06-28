struct Uniforms {
  mvpMatrix: mat4x4f,
};

@group(0) @binding(0)
var<uniform> uniforms: Uniforms;

struct VertexOutput {
  @builtin(position) position: vec4f,
  @location(0) color: vec3f,
};

@vertex
fn vs_main(
  @location(0) position: vec3f,
  @location(1) color: vec3f,
) -> VertexOutput {
  var out: VertexOutput;
  out.position = uniforms.mvpMatrix * vec4f(position, 1.0);
  out.color = color;
  return out;
}

@fragment
fn fs_main(@location(0) color: vec3f) -> @location(0) vec4f {
  return vec4f(color, 1.0);
}
