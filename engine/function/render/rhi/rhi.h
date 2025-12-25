#ifndef RHI_H
#define RHI_H
// act like rendering device



class RHI {
public:
    virtual ~RHI() = default;

    virtual void init(void* window_handle) = 0;
    virtual void draw_triangle_test() = 0;
    virtual void present() = 0;

private:
    // RHIDriver* driver_ = nullptr;
};

#endif