// Native (non-Emscripten) GraphicsAdapter.
//
// The WebGL/WebGPU rendering layer added by claw-web only exists in the browser
// build. Native targets (Linux, Nintendo Switch) render the game exclusively
// through SDL_Renderer, exactly like upstream OpenClaw, so the adapter here is
// a thin no-op that satisfies BaseGameApp's interface without pulling in any
// of the WASM graphics sources.

#ifndef __EMSCRIPTEN__

#include "../GraphicsAdapter.h"
#include "../GraphicsManager.h"

// GraphicsAdapter owns a unique_ptr<GraphicsManager> that is never populated
// natively; only its destructor is needed to link.
GraphicsManager::~GraphicsManager() {}

GraphicsAdapter::GraphicsAdapter() : m_isInitialized(false) {}

GraphicsAdapter::~GraphicsAdapter() { Shutdown(); }

bool GraphicsAdapter::Initialize() {
    m_isInitialized = true;
    return true;
}

bool GraphicsAdapter::Initialize(SDL_Renderer * /*existingRenderer*/) {
    m_isInitialized = true;
    return true;
}

void GraphicsAdapter::Shutdown() { m_isInitialized = false; }

void GraphicsAdapter::BeginFrame() {}
void GraphicsAdapter::EndFrame() {}

std::string GraphicsAdapter::GetRendererName() const { return "SDL_Renderer"; }

bool GraphicsAdapter::IsUsingWebGL() const { return false; }

std::string GraphicsAdapter::GetRendererStatus() const {
    return "Native build: rendering through SDL_Renderer";
}

float GraphicsAdapter::GetFrameTime() const { return 0.0f; }
int GraphicsAdapter::GetDrawCalls() const { return 0; }
void GraphicsAdapter::ResetStats() {}

#endif // !__EMSCRIPTEN__
