#include "engine/function/asset/asset.h"

#include <cereal/types/vector.hpp> 

class PNGAsset : public Asset {
public:
    int width = 0;
    int height = 0;
    int channels = 0;
    
    // 存储解压后的原始像素数据 (R, G, B, A, R, G, B, A...)
    // unsigned char 对应 0-255 的颜色值
    std::vector<unsigned char> pixels;

    // 返回资源类型名
    std::string get_asset_type_name() const override { return "Texture2D"; }

    template<class Archive>
    void serialize(Archive& archive) {
        archive(cereal::base_class<Asset>(this));
        
        // 2. 序列化图片元数据
        archive(CEREAL_NVP(width), 
                CEREAL_NVP(height), 
                CEREAL_NVP(channels));

        // 3. 序列化像素二进制数据
        // Cereal 对 vector 有专门的优化，在二进制模式下非常快
        archive(CEREAL_NVP(pixels)); 
    }
};