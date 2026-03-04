# fal-3d-unreal

Generate 3D models from text prompts inside Unreal Engine 5 using the [fal.ai Hunyuan 3D API](https://fal.ai/models/fal-ai/hunyuan-3d/v3.1/pro/text-to-3d). Type a prompt in-game, and the generated GLB model spawns right into your world.

Built entirely in C++ with a programmatic UMG widget — no Blueprint widgets needed.

![Goku spawned in-game from a text prompt](https://github.com/blendi-remade/fal-3d-unreal/assets/goku-demo.png)

## How It Works

1. Press **Tab** to open the generator panel
2. Type a text prompt (e.g. "Son Goku")
3. Click **Generate 3D Model**
4. Wait ~2 minutes while fal.ai generates the model
5. The GLB model spawns 300 units in front of your character
6. Press **Tab** again to close the panel and resume playing

## Architecture

| Class | Role |
|-------|------|
| `UFalApiClient` | HTTP client for fal.ai queue API (submit, poll, fetch result) |
| `UFalGeneratorWidget` | Programmatic UMG panel (text input, button, status text) |
| `Afal3DDemoCharacter` | Owns widget + API client, toggles panel, spawns GLB actors |

The generated GLB is loaded at runtime using the [glTFRuntime](https://github.com/rdeioris/glTFRuntime) plugin.

## Prerequisites

- **Unreal Engine 5.5**
- A **fal.ai API key** — get one at [fal.ai/dashboard/keys](https://fal.ai/dashboard/keys)

## Setup

### 1. Clone the repo

```bash
git clone --recursive https://github.com/blendi-remade/fal-3d-unreal.git
```

> The `--recursive` flag is required to pull the glTFRuntime plugin submodule.

### 2. Add stock Epic content

The repo excludes large stock assets (StarterContent, Characters) to keep the repo size manageable. You need to copy them from a fresh UE5 Third Person template:

1. In UE5, create a new **Third Person** project (call it anything)
2. Copy these folders from the new project's `Content/` into `fal3DDemo/Content/`:
   - `Characters/`
   - `StarterContent/`

### 3. Set your API key

Create a `.env` file in the `fal3DDemo/` folder:

```
FAL_KEY=your-api-key-here
```

See `.env.example` for reference. The code also supports setting `FAL_KEY` as an OS environment variable as a fallback.

### 4. Open the project

Double-click `fal3DDemo/fal3DDemo.uproject` to open in Unreal Editor. It will compile the C++ code automatically.

### 5. Play

Click **Play** (or press Alt+P), then press **Tab** to open the generator panel.

## Project Structure

```
fal3DDemo/
  Source/fal3DDemo/
    FalApiClient.h/.cpp        # fal.ai HTTP submit/poll/result pipeline
    FalGeneratorWidget.h/.cpp   # Programmatic UMG panel
    fal3DDemoCharacter.h/.cpp   # Character with panel toggle + GLB spawning
    fal3DDemo.Build.cs          # Module dependencies
  Content/ThirdPerson/
    Input/Actions/IA_TogglePanel.uasset  # Tab key input action
    Input/IMC_Default.uasset             # Input mapping context
    Blueprints/BP_ThirdPersonCharacter.uasset
  Plugins/glTFRuntime/          # glTF/GLB runtime loader (submodule)
  .env                          # Your API key (gitignored)
  .env.example                  # Template for .env
```

## License

MIT
