#ifndef ENGINE_CORE_MATH_EXTENT_H_
#define ENGINE_CORE_MATH_EXTENT_H_
#include <cmath>
#include <algorithm>
typedef struct Extent2D 
{
    uint32_t    width = 0;
    uint32_t    height = 0;

	friend bool operator==(const Extent2D& a, const Extent2D& b)
	{
		return 	a.width == b.width &&
				a.height == b.height;
	}

	const uint32_t MipSize() const 
	{ 
		return (uint32_t)(std::floor(std::log2((std::max)(width, height)))) + 1; 
	}
    
    const uint32_t mip_size() const { return MipSize(); }

} Extent2D;

typedef struct Extent3D 
{
    uint32_t    width = 0;
    uint32_t    height = 0;
    uint32_t    depth = 0;

	friend bool operator==(const Extent3D& a, const Extent3D& b)
	{
		return 	a.width == b.width &&
				a.height == b.height &&
				a.depth == b.depth;
	}
	
	const uint32_t MipSize() const 
	{ 
		return (uint32_t)(std::floor(std::log2((std::max)(width, (std::max)(height, depth))))) + 1; 
	}

    const uint32_t mip_size() const { return MipSize(); }

} Extent3D;

typedef struct Offset2D 
{
    uint32_t    x = 0;
    uint32_t    y = 0;

	friend Offset2D operator+(const Offset2D& a, const Offset2D& b)
	{
		return {
			.x = a.x + b.x,
			.y = a.y + b.y
		};
	}

	friend bool operator==(const Offset2D& a, const Offset2D& b)
	{
		return 	a.x == b.x &&
				a.y == b.y;
	}

} Offset2D;

typedef struct Offset3D 
{
    uint32_t    x = 0;
    uint32_t    y = 0;
    uint32_t    z = 0;

	friend Offset3D operator+(const Offset3D& a, const Offset3D& b)
	{
		return {
			.x = a.x + b.x,
			.y = a.y + b.y,
			.z = a.z + b.z
		};
	}

	friend bool operator==(const Offset3D& a, const Offset3D& b)
	{
		return 	a.x == b.x &&
				a.y == b.y &&
				a.z == b.z;
	}

} Offset3D;

typedef struct Rect2D 
{
    Offset2D    offset;
    Extent2D    extent;
} Rect2D;

typedef struct Color3 {
    float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
} Color3;

typedef struct Color4 
{
    float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 0.0f;
} Color4;

#endif