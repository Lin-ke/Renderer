#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include <memory>

class Window;
class RenderSystem;
class RHI;
class AssetManager;

class EngineContext {
public:
    ~EngineContext();
	static void init();
    static void exit();
	static void main_loop();
    
    static RHI* get_rhi();
    static AssetManager* asset();

private:
	EngineContext();
	static std::unique_ptr<EngineContext> instance_;
    
    std::unique_ptr<Window> window_;
    std::unique_ptr<RenderSystem> render_system_;
    std::unique_ptr<AssetManager> asset_manager_;
	void main_loop_internal();
};

#endif