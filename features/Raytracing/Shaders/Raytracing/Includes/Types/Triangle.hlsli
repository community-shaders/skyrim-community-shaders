#ifndef TRIANGLE_HLSI
#define TRIANGLE_HLSI

struct Triangle
{
	uint16_t x;
	uint16_t y;
	uint16_t z;
};
#ifdef __cplusplus
static_assert(sizeof(Triangle) == 6);
#endif

#endif