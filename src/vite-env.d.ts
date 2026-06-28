/// <reference types="vite/client" />
/// <reference types="@webgpu/types" />

declare module '*.wgsl' {
  const content: string;
  export default content;
}
