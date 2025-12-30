#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include <memory>
#include <bitset>
class Window;
class RenderSystem;
class RHI;
class AssetManager;


class EngineContext {
public:
    enum StartMode{
        Asset_ = 0,
        Render_ = 1,
        Window_ = 2,
        Log_ = 3,
    };
    std::bitset<8> mode_;
    ~EngineContext();
	static void init(std::bitset<8> mode );
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