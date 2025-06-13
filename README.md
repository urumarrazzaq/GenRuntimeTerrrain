# GenRuntimeTerrrain

Procedurally generate landscapes at runtime in Unreal Engine 5 using grayscale heightmaps.  
Supports up to 4K heightmap textures and provides both asynchronous and chunked terrain generation pipelines.

---

## 🚀 Features

- ✅ Runtime terrain generation from grayscale heightmaps
- 🎮 Supports async and tick-based chunked generation
- 🖼️ Apply custom surface textures via dynamic material instance
- 📏 Adjustable height scale and grid spacing
- 🧵 Threaded generation using UE's `Async` system
- 🧪 Optional progress reporting and cancellation support
- 🧩 Easily extensible for erosion, biome painting, or LOD support

---

## 📸 Preview

*(Add GIF or screenshot of terrain generation here)*

---

## 🧩 Module Setup

This project is structured as a standalone Unreal Engine module.

### Dependencies

- `ProceduralMeshComponent`
- `ImageWrapper`, `RenderCore`, `RHI`

Make sure these are added in your `.uproject` file and `Build.cs`.

### Build.cs Snippet

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
  "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent"
});

PrivateDependencyModuleNames.AddRange(new string[] {
  "ImageWrapper", "RenderCore", "RHI"
});
