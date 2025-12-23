# C44Matrix

A Nuke plugin for applying 4x4 matrix transformations to pixel data — an extended version of Nuke's built-in ColorMatrix.

Originally created by [Ivan Busquets](https://www.nukepedia.com/tools/plugins/colour/c44matrix/). This fork adds **Axis node support** in addition to the original Camera input.

## Features

- Apply 4x4 matrix transformations to RGBA pixel data
- Automatic matrix extraction from **Camera** or **Axis** nodes
- Transform position passes between coordinate spaces (world, camera, NDC, screen)
- Manual matrix input for custom transformations
- Matrix operations: invert, transpose, W-divide

## Compatibility

| Platform | Nuke Version |
|----------|--------------|
| Windows  | 8.0+         |
| Linux    | 8.0+         |
| macOS    | 8.0+         |

Pre-built binaries available in `COMPILED/`

## Installation

1. Copy the plugin files from `COMPILED/` to your `.nuke` directory
2. Add to your `init.py`:
   ```python
   nuke.pluginAddPath('/path/to/C44Matrix')
   ```
3. Restart Nuke

## Usage

C44Matrix transforms RGBA channels as a 4D vector (X, Y, Z, W). For position pass transformations, ensure alpha is set to 1.

### Input Modes

**Manual Input**
Enter matrix values directly in the 4x4 grid — useful for matrices from Python scripts or render metadata.

**From Camera** *(original)*
Connect a Camera node to automatically extract transformation matrices.

**From Axis** *(new in this fork)*
Connect any Axis node (or Axis-based node like Locator, Light, etc.) to extract its transformation matrix.

### Available Matrices

| Matrix | Description |
|--------|-------------|
| **Transform** | Full transformation (translation + rotation + scale). World Space ↔ Camera/Object Space |
| **Translation** | Translation component only |
| **Rotation** | Rotation component only |
| **Scale** | Scale component only |
| **Projection** | Camera projection matrix (aperture, focal, win_translate, win_scale, roll). Camera Space ↔ NDC |
| **Format** | NDC (-1 to 1) → pixel coordinates for current format |

### Options

| Option | Description |
|--------|-------------|
| **Invert** | Apply the inverse of the matrix |
| **Transpose** | Swap rows and columns |
| **W Divide** | Divide result by W component (typically needed for projection matrices) |

## Common Use Cases

- Converting world position passes to camera space
- Projecting 3D positions to screen coordinates
- Transforming position data to match relocated 3D elements
- Building coordinate space conversion gizmos

## Building from Source

Platform-specific CMake files are provided:
- `CMakeLists_WIN.txt`
- `CMakeLists_LINUX.txt`
- `CMakeLists_MAC.txt`

See `building_step_by_step.txt` for detailed instructions.

### Requirements
- CMake
- Nuke NDK (included with Nuke installation)
- C++ compiler (MSVC on Windows, GCC on Linux, Clang on macOS)

## Changes in This Fork

- **Axis node support** — Connect any Axis-based node to extract transformation matrices, not just Cameras

## Credits

- **Original Author:** [Ivan Busquets](https://www.nukepedia.com/tools/plugins/colour/c44matrix/)
- **Axis Support:** [Peter Mercell](https://github.com/petermercell)

## License

Based on the original C44Matrix by Ivan Busquets from Nukepedia.
