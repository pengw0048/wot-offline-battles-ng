#pragma once

#include <cstdint>

struct _object;
namespace stock_offline_query {
struct Context;
struct Hit {
    double point[3], normal[3], translation[3];
    std::uint32_t material, item, chunk;
};
enum Result { Unbound, Miss, Found, Failed };
// All ownership is synchronous and GIL-bound. Null means custom capabilities.
Context * begin(_object * capabilities);
void end(Context *);
// Flush observational state before Python effects, then refresh after them.
bool beforeEffect(Context *);
bool afterEffect(Context *);
bool groundAvailable(Context *);
void beginRay(Context *);
void finishRay(Context *, bool committed);
void rounded(const double *, double *);
void departureStep(const double * end, const double * point, double epsilon,
    double * result, double & length);
Result ray(Context *, _object * filter, bool ground, const double * start,
    const double * end, Hit &);
// Only effects materialize a six-field Python hit; the frontier caches it.
_object * materialize(const Hit &);
}
