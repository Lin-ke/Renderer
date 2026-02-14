#include "Math.h" // 假设你的头文件名为 Math.h

namespace Math 
{
    Vec3 clamp_euler_angle(Vec3 angle)
    {
        Vec3 ret = angle;
        // 限制 Pitch (Z轴)
        if(ret.z() > 89.9f) ret.z() = 89.9f;
        if(ret.z() < -89.9f) ret.z() = -89.9f;

        // 对 Yaw (Y轴) 和 Pitch (Z轴) 取模，防止角度过大
        ret.y() = fmod(ret.y(), 360.0f);
        ret.z() = fmod(ret.z(), 360.0f);
        
        // 归一化到 [-180, 180]
        if(ret.y() > 180.0f) ret.y() -= 360.0f;
        if(ret.z() > 180.0f) ret.z() -= 360.0f;

        return ret;
    }

    // https://blog.csdn.net/WillWinston/article/details/125746107
    // 确保 pitch 的范围 [-PI/2, PI/2]
    // yaw 和 roll 都是 [-PI, PI]
    Vec3 to_euler_angle(const Quaternion& q)   
    {                                           
        double angles[3];

        // yaw (y-axis rotation)
        double sinr_cosp = 2 * (q.w() * q.y() - q.x() * q.z());
        double cosr_cosp = 1 - 2 * (q.y() * q.y() + q.z() * q.z());
        angles[0] = std::atan2(sinr_cosp, cosr_cosp);

        // pitch (z-axis rotation)
        double sinp = 2 * (q.w() * q.z() + q.x() * q.y());
        if (std::abs(sinp) >= 1)
            angles[1] = std::copysign(PI / 2, sinp); // use 90 degrees if out of range
        else
            angles[1] = std::asin(sinp);

        // roll (x-axis rotation)
        double siny_cosp = 2 * (q.w() * q.x() - q.y() * q.z());
        double cosy_cosp = 1 - 2 * (q.x() * q.x() + q.z() * q.z());
        angles[2] = std::atan2(siny_cosp, cosy_cosp);    

        angles[0] *= 180 / PI;  // 有一点点精度问题，可以忽略
        angles[1] *= 180 / PI;
        angles[2] *= 180 / PI; 

        // 返回顺序根据具体需求调整，这里对应原代码的 Vec3(angles[2], angles[0], angles[1])
        // 即 (Roll, Yaw, Pitch) -> (X, Y, Z)
        return Vec3(angles[2], angles[0], angles[1]);
    }

    Quaternion to_quaternion(Vec3 euler_angle)
    {
        Vec3 radians = to_radians(euler_angle);  // 右手系，Y向上
        
        // 注意：原代码使用 radians(1)作为Y, radians(2)作为Z, radians(0)作为X
        // 这通常意味着 euler_angle 传入的是 (Roll, Yaw, Pitch) 或者类似的自定义顺序
        Eigen::AngleAxisf yaw   = Eigen::AngleAxisf(radians(1), Vec3::UnitY()); 
        Eigen::AngleAxisf pitch = Eigen::AngleAxisf(radians(2), Vec3::UnitZ());  
        Eigen::AngleAxisf roll  = Eigen::AngleAxisf(radians(0), Vec3::UnitX());

        return yaw * pitch * roll;  // 旋转顺序
    }

    /* // 下面这一组也是对的，旋转顺序不一样
    Vec3 to_euler_angle_alt(const Quaternion& q)
    {
        double angles[3];

        // roll (x-axis rotation)
        double sinr_cosp = 2 * (q.w() * q.x() + q.y() * q.z());
        double cosr_cosp = 1 - 2 * (q.x() * q.x() + q.z() * q.z());
        angles[2] = std::atan2(sinr_cosp, cosr_cosp);

        // pitch (z-axis rotation)
        double sinp = 2 * (q.w() * q.z() - q.y() * q.x());
        if (std::abs(sinp) >= 1)
            angles[1] = std::copysign(PI / 2, sinp);
        else
            angles[1] = std::asin(sinp);

        // yaw (y-axis rotation)
        double siny_cosp = 2 * (q.w() * q.y() + q.x() * q.z());
        double cosy_cosp = 1 - 2 * (q.y() * q.y() + q.z() * q.z());
        angles[0] = std::atan2(siny_cosp, cosy_cosp);    

        angles[0] *= 180 / PI;
        angles[1] *= 180 / PI;
        angles[2] *= 180 / PI; 

        return Vec3(angles[0], angles[1], angles[2]);
    }

    Quaternion to_quaternion_alt(Vec3 euler_angle)
    {
        Vec3 radians = to_radians(euler_angle);
        Eigen::AngleAxisf yaw   = Eigen::AngleAxisf(radians(0), Vec3::UnitY()); 
        Eigen::AngleAxisf pitch = Eigen::AngleAxisf(radians(1), Vec3::UnitZ());  
        Eigen::AngleAxisf roll  = Eigen::AngleAxisf(radians(2), Vec3::UnitX());

        return roll * pitch * yaw;
    }
    */

    Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up)
    {
        // DX11 Left-handed coordinate system
        // In LH system: +X right, +Y up, +Z forward (into the screen, away from camera)
        // But we want view space +Z to point in front of camera (toward the target)
        // So forward vector = target - eye (points toward target, which is in front of camera)
        Vec3 f = (center - eye).normalized();  // Forward direction (eye -> center)
        Vec3 s = up.cross(f).normalized();     // Right vector (up x forward for LH)
        Vec3 u = f.cross(s);                   // Up vector

        Mat4 mat = Mat4::Identity();
        // Row 0: right vector
        mat(0, 0) = s.x();
        mat(0, 1) = s.y();
        mat(0, 2) = s.z();
        mat(0, 3) = -s.dot(eye);
        // Row 1: up vector  
        mat(1, 0) = u.x();
        mat(1, 1) = u.y();
        mat(1, 2) = u.z();
        mat(1, 3) = -u.dot(eye);
        // Row 2: forward vector (+Z points toward target, in front of camera)
        mat(2, 0) = f.x();
        mat(2, 1) = f.y();
        mat(2, 2) = f.z();
        mat(2, 3) = -f.dot(eye);

        return mat;
    }

    Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane)
    {
        // DX11 Left-handed perspective projection
        // Clip space: x/y in [-1, 1], z in [0, 1]
        Mat4 mat = Mat4::Zero();
        float tan_half_fovy = std::tan(fovy / 2.0f);
        
        mat(0, 0) = 1.0f / (aspect * tan_half_fovy);
        mat(1, 1) = 1.0f / tan_half_fovy;
        // For LH: z maps from [near, far] to [0, 1]
        mat(2, 2) = far_plane / (far_plane - near_plane);
        mat(2, 3) = -(far_plane * near_plane) / (far_plane - near_plane);
        mat(3, 2) = 1.0f;  // w = z (copy z to w for perspective divide)
        
        return mat;
    }

    Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane)
    {
        Mat4 mat = Mat4::Identity();
        mat(0, 0) = 2.0f / (right - left);
        mat(1, 1) = 2.0f / (top - bottom);
        mat(2, 2) = -1.0f / (far_plane - near_plane);
        mat(0, 3) = -(right + left) / (right - left);
        mat(1, 3) = -(top + bottom) / (top - bottom);
        mat(2, 3) = -near_plane / (far_plane - near_plane);
        return mat;
    }

    void mat3x4(Mat4 mat, float* new_mat)
    {
        new_mat[0]  = mat.row(0).x();
        new_mat[1]  = mat.row(0).y();
        new_mat[2]  = mat.row(0).z();
        new_mat[3]  = mat.row(0).w();
        new_mat[4]  = mat.row(1).x();
        new_mat[5]  = mat.row(1).y();
        new_mat[6]  = mat.row(1).z();
        new_mat[7]  = mat.row(1).w();
        new_mat[8]  = mat.row(2).x();
        new_mat[9]  = mat.row(2).y();
        new_mat[10] = mat.row(2).z();
        new_mat[11] = mat.row(2).w();
    }
}