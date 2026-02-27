#include <iostream>
#include <cmath>
#include "engine/core/math/math.h"
#include "engine/core/math/transform.h"

void print_vec3(const char* name, const Vec3& v) {
    std::cout << name << ": (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
}

int main() {
    std::cout << "=== Test 1: Euler to Quaternion and back ===" << std::endl;
    
    // Test with yaw = 90
    Vec3 euler_in(0.0f, 90.0f, 0.0f);
    print_vec3("Input euler", euler_in);
    
    Quaternion q = Math::to_quaternion(euler_in);
    std::cout << "Quaternion: (" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")" << std::endl;
    
    Vec3 euler_out = Math::to_euler_angle(q);
    print_vec3("Output euler", euler_out);
    
    std::cout << "\n=== Test 2: Transform round-trip ===" << std::endl;
    
    // 模拟reflect_inspector的行为
    Transform val;
    val.set_position({0.0f, 0.0f, 0.0f});
    val.set_rotation({0.0f, 90.0f, 0.0f});
    val.set_scale({0.4f, 0.4f, 0.4f});
    
    print_vec3("Initial Position", val.get_position());
    print_vec3("Initial Rotation", val.get_euler_angle());
    print_vec3("Initial Scale", val.get_scale());
    
    // 模拟ImGui编辑流程
    Vec3 position = val.get_position();
    Vec3 rotation = val.get_euler_angle();
    Vec3 scale = val.get_scale();
    
    std::cout << "\n--- After getter ---" << std::endl;
    print_vec3("Local position", position);
    print_vec3("Local rotation", rotation);
    print_vec3("Local scale", scale);
    
    // 修改rotation
    rotation.y = 91.0f;
    
    // 设置回Transform
    val.set_position(position);
    val.set_rotation(rotation);
    val.set_scale(scale);
    
    std::cout << "\n--- After setter ---" << std::endl;
    print_vec3("Final Position", val.get_position());
    print_vec3("Final Rotation", val.get_euler_angle());
    print_vec3("Final Scale", val.get_scale());
    
    // 检查position是否变化
    bool pos_changed = std::abs(val.get_position().x) > 0.001f || 
                       std::abs(val.get_position().y) > 0.001f || 
                       std::abs(val.get_position().z) > 0.001f;
    
    if (pos_changed) {
        std::cout << "\n!!! BUG: Position changed when only rotation was modified !!!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Test passed: Position unchanged ===" << std::endl;
    
    // Test 3: 尝试不同的初始rotation值，看看是否有数值误差累积
    std::cout << "\n=== Test 3: Multiple round-trips ===" << std::endl;
    Transform t;
    t.set_position({10.0f, 20.0f, 30.0f});
    t.set_rotation({0.0f, 90.0f, 0.0f});
    
    for (int i = 0; i < 10; i++) {
        Vec3 p = t.get_position();
        Vec3 r = t.get_euler_angle();
        Vec3 s = t.get_scale();
        
        // 每次稍微修改rotation
        r.y += 1.0f;
        
        t.set_position(p);
        t.set_rotation(r);
        t.set_scale(s);
        
        std::cout << "Iteration " << i << ": Position=" 
                  << t.get_position().x << ","
                  << t.get_position().y << ","
                  << t.get_position().z << std::endl;
    }
    
    return 0;
}
