/******************************************************************************
 *  PlaTec, a 2D terrain generator based on plate tectonics
 *  Copyright (C) 2012- Lauri Viitanen
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see http://www.gnu.org/licenses/
 *****************************************************************************/

#define _USE_MATH_DEFINES // Winblow$.
#include <cfloat>    // FT_EPSILON
#ifdef __MINGW32__ // this is to avoid a problem with the hypot function which is messed up by Python...
#undef __STRICT_ANSI__
#endif
#include <cmath>     // sin, cos
#include <cstdlib>   // rand
#include <vector>
#include <stdexcept> // std::invalid_argument
#include <assert.h>

#include "plate.hpp"
#include "heightmap.hpp"
#include "rectangle.hpp"

#define INITIAL_SPEED_X 1
#define DEFORMATION_WEIGHT 2

using namespace std;

plate::plate(long seed, const float* m, size_t w, size_t h, size_t _x, size_t _y,
             size_t plate_age, WorldDimension worldDimension) :
             _randsource(seed),
             width(w), height(h),
             mass(0), left(_x), top(_y), cx(0), cy(0), dx(0), dy(0),
             map(w, h), age_map(w, h), _worldDimension(worldDimension)
{
    if (NULL == m) {
        throw invalid_argument("the given heightmap should not be null");
    }
    if (w <= 0 || h <= 0) {
        throw invalid_argument("width and height of the plate should be greater than zero");
    }
    if (_x < 0 || _y <0) {
        throw invalid_argument("coordinates of the plate should be greater or equal to zero");
    }
    if (plate_age < 0) {
        throw invalid_argument("age of the plate should be greater or equal to zero");
    }

    const size_t plate_area = w * h;
    const double angle = 2 * M_PI * _randsource() / (double)_randsource.max();

    segment = new size_t[plate_area];

    velocity = 1;
    rot_dir = _randsource() & 1 ? 1 : -1;
    vx = cos(angle) * INITIAL_SPEED_X;
    vy = sin(angle) * INITIAL_SPEED_X;
    memset(segment, 255, plate_area * sizeof(size_t));

    size_t k;
    for (size_t y = k = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++k) {
            // Clone map data and count crust mass.
            mass += map[k] = m[k];

            // Calculate center coordinates weighted by mass.
            cx += x * m[k];
            cy += y * m[k];

            // Set the age of ALL points in this plate to same
            // value. The right thing to do would be to simulate
            // the generation of new oceanic crust as if the plate
            // had been moving to its current direction until all
            // plate's (oceanic) crust receive an age.
            age_map.set(x, y, plate_age & -(m[k] > 0));
        }
    }

    // Normalize center of mass coordinates.
    cx /= mass;
    cy /= mass;
}

plate::~plate() throw()
{
    delete[] segment;
    segment = NULL;
}

size_t plate::addCollision(size_t wx, size_t wy)
{
    ContinentId seg = getContinentAt(wx, wy);
    ++seg_data[seg].coll_count;
    return seg_data[seg].area;
}

void plate::addCrustByCollision(size_t x, size_t y, float z, size_t time, ContinentId activeContinent)
{
    // Add crust. Extend plate if necessary.
    setCrust(x, y, getCrust(x, y) + z, time);

    size_t index = getMapIndex(&x, &y);

    segment[index] = activeContinent;
    segmentData& data = seg_data[activeContinent];

    ++data.area;
    data.enlarge_to_contain(x, y);
}

void plate::addCrustBySubduction(size_t x, size_t y, float z, size_t t,
    float dx, float dy)
{
    // TODO: Create an array of coordinate changes that would create
    //       a circle around current point. Array is static and it is
    //       initialized at the first call to this function.
    //       After all points of the circle are checked around subduction
    //       point the one with most land mass around it will be chosen as
    //       "most in land" point and subducting crust is added there.
    //       However to achieve a little more "natural" look normal
    //       distributed randomness is added around the "center" point.
    //       Benefits:
    //           NEVER adds crust outside plate.
    //           ALWAYS goes inland as much as possible
    //       Drawbacks:
    //           Additional logic required
    //           Might place crust on other continent on same plate!
    size_t index = getMapIndex(&x, &y);

    // Take vector difference only between plates that move more or less
    // to same direction. This makes subduction direction behave better.
    //
    // Use of "this" pointer is not necessary, but it make code clearer.
    // Cursed be those who use "m_" prefix in member names! >(
    float dot = this->vx * dx + this->vy * dy;
    dx -= this->vx * (dot > 0);
    dy -= this->vy * (dot > 0);

    float offset = (float)_randsource() / (float)_randsource.max();
    offset *= offset * offset * (2 * (_randsource() & 1) - 1);
    dx = 10 * dx + 3 * offset;
    dy = 10 * dy + 3 * offset;

    x = (size_t)((int)x + dx);
    y = (size_t)((int)y + dy);

    if (width == _worldDimension.getWidth()) {
        x %= width;
    }
    if (height == _worldDimension.getHeight()) {
        y %= height;
    }

    index = y * width + x;
    if (index < width * height && map[index] > 0)
    {
        t = (map[index] * age_map[index] + z * t) / (map[index] + z);
        age_map[index] = t * (z > 0);

        map[index] += z;
        mass += z;
    }
}

float plate::aggregateCrust(plate* p, size_t wx, size_t wy)
{
try {    
    size_t lx = wx, ly = wy;
    const size_t index = getMapIndex(&lx, &ly);
    const ContinentId seg_id = segment[index];

    // This check forces the caller to do things in proper order!
    //
    // Usually continents collide at several locations simultaneously.
    // Thus if this segment that is being merged now is removed from
    // segmentation bookkeeping, then the next point of collision that is
    // processed during the same iteration step would cause the test
    // below to be true and system would experience a premature abort.
    //
    // Therefore, segmentation bookkeeping is left intact. It doesn't
    // cause significant problems because all crust is cleared and empty
    // points are not processed at all. (Test on (seg_id >= seg_data.size()) removed)

    // One continent may have many points of collision. If one of them
    // causes continent to aggregate then all successive collisions and
    // attempts of aggregation would necessarily change nothing at all,
    // because the continent was removed from this plate earlier!
    if (seg_data[seg_id].isEmpty()) {
        return 0;   // Do not process empty continents.
    }

    ContinentId activeContinent = p->selectCollisionSegment(wx, wy);

    // Wrap coordinates around world edges to safeguard subtractions.
    wx += _worldDimension.getWidth();
    wy += _worldDimension.getHeight();

    // Aggregating segment [%u, %u]x[%u, %u] vs. [%u, %u]@[%u, %u]\n",
    //      seg_data[seg_id].x0, seg_data[seg_id].y0,
    //      seg_data[seg_id].x1, seg_data[seg_id].y1,
    //      width, height, lx, ly);

    float old_mass = mass;

    // Add all of the collided continent's crust to destination plate.
    for (size_t y = seg_data[seg_id].getTop(); y <= seg_data[seg_id].getBottom(); ++y)
    {
      for (size_t x = seg_data[seg_id].getLeft(); x <= seg_data[seg_id].getRight(); ++x)
      {
        const size_t i = y * width + x;
        if ((segment[i] == seg_id) && (map[i] > 0))
        {
            p->addCrustByCollision(wx + x - lx, wy + y - ly,
                map[i], age_map[i], activeContinent);

            mass -= map[i];
            map[i] = 0;
        }
      }
    }

    seg_data[seg_id].area = 0; // Mark segment as non-existent
    return old_mass - mass;
} catch (const exception& e){
    std::string msg = "Problem during plate::aggregateCrust: ";
    msg = msg + e.what();
    throw runtime_error(msg.c_str());
}
}

void plate::applyFriction(float deformed_mass)
{
    // Remove the energy that deformation consumed from plate's kinetic
    // energy: F - dF = ma - dF => a = dF/m.
    if (mass > 0)
    {
        float vel_dec = DEFORMATION_WEIGHT * deformed_mass / mass;
        vel_dec = vel_dec < velocity ? vel_dec : velocity;

        // Altering the source variable causes the order of calls to
        // this function to have difference when it shouldn't!
        // However, it's a hack well worth the outcome. :)
        velocity -= vel_dec;
    }
}

void plate::collide(plate& p, size_t wx, size_t wy, float coll_mass)
{
try {    
    const float coeff_rest = 0.0; // Coefficient of restitution.
                                  // 1 = fully elastic, 0 = stick together.

    // Calculate the normal to the curve/line at collision point.
    // The normal will point into plate B i.e. the "other" plate.
    //
    // Plates that wrap over world edges can mess the normal vector.
    // This could be solved by choosing the normal vector that points the
    // shortest path beween mass centers but this causes problems when
    // plates are like heavy metal balls at a long rod and one plate's ball
    // collides at the further end of other plate's rod. Sure, this is
    // nearly never occurring situation but if we can easily do better then
    // why not do it?
    //
    // Better way is to select that normal vector that points along the
    // line that passes nearest the point of collision. Because point's
    // distance from line segment is relatively cumbersome to perform, the
    // vector is constructed as the sum of vectors <massCenterA, P> and
    // <P, massCenterB>. This solution works because collisions always
    // happen in the overlapping region of the two plates.
    size_t apx = wx, apy = wy, bpx = wx, bpy = wy;
    float ap_dx, ap_dy, bp_dx, bp_dy, nx, ny;
    size_t index   =   getMapIndex(&apx, &apy);
    size_t p_index = p.getMapIndex(&bpx, &bpy);

    // out of colliding map's bounds!
    assert(index < width * height);
    assert(p_index < p.width * p.height);

    ap_dx = (int)apx - (int)cx;
    ap_dy = (int)apy - (int)cy;
    bp_dx = (int)bpx - (int)p.cx;
    bp_dy = (int)bpy - (int)p.cy;
    nx = ap_dx - bp_dx;
    ny = ap_dy - bp_dy;

    if (nx * nx + ny * ny <= 0) {
        return; // Avoid division by zero!
    }

    // Scaling is required at last when impulses are added to plates!
    float n_len = sqrt(nx * nx + ny * ny);
    nx /= n_len;
    ny /= n_len;

    // Compute relative velocity between plates at the collision point.
    // Because torque is not included, calc simplifies to v_ab = v_a - v_b.
    const float rel_vx = vx - p.vx;
    const float rel_vy = vy - p.vy;

    // Get the dot product of relative velocity vector and collision vector.
    // Then get the projection of v_ab along collision vector.
    // Note that vector n must be a unit vector!
    const float rel_dot_n = rel_vx * nx + rel_vy * ny;

    if (rel_dot_n <= 0) {
        return; // Exit if objects are moving away from each other.
    }

    // Calculate the denominator of impulse: n . n * (1 / m_1 + 1 / m_2).
    // Use the mass of the colliding crust for the "donator" plate.
    float denom = (nx * nx + ny * ny) * (1.0/mass + 1.0/coll_mass);

    // Calculate force of impulse.
    float J = -(1 + coeff_rest) * rel_dot_n / denom;

    // Compute final change of trajectory.
    // The plate that is the "giver" of the impulse should receive a
    // force according to its pre-collision mass, not the current mass!
    dx += nx * J / mass;
    dy += ny * J / mass;
    p.dx -= nx * J / (coll_mass + p.mass);
    p.dy -= ny * J / (coll_mass + p.mass);

    // In order to prove that the code above works correctly, here is an
    // example calculation with ball A (mass 10) moving right at velocity
    // 1 and ball B (mass 100) moving up at velocity 1. Collision point
    // is at rightmost point of ball A and leftmost point of ball B.
    // Radius of both balls is 2.
    // ap_dx =  2;
    // ap_dy =  0;
    // bp_dx = -2;
    // bp_dy =  0;
    // nx = 2 - -2 = 4;
    // ny = 0 -  0 = 0;
    // n_len = sqrt(4 * 4 + 0) = 4;
    // nx = 4 / 4 = 1;
    // ny = 0 / 4 = 0;
    //
    // So far so good, right? Normal points into ball B like it should.
    //
    // rel_vx = 1 -  0 = 1;
    // rel_vy = 0 - -1 = 1;
    // rel_dot_n = 1 * 1 + 1 * 0 = 1;
    // denom = (1 * 1 + 0 * 0) * (1/10 + 1/100) = 1 * 11/100 = 11/100;
    // J = -(1 + 0) * 1 / (11/100) = -100/11;
    // dx = 1 * (-100/11) / 10 = -10/11;
    // dy = 0;
    // p.dx = -1 * (-100/11) / 100 = 1/11;
    // p.dy = -0;
    //
    // So finally:
    // vx = 1 - 10/11 = 1/11
    // vy = 0
    // p.vx = 0 + 1/11 = 1/11
    // p.vy = -1
    //
    // We see that in with restitution 0, both balls continue at same
    // speed along X axis. However at the same time ball B continues its
    // path upwards like it should. Seems correct right?
} catch (const exception& e){
    std::string msg = "Problem during plate::collide: ";
    msg = msg + e.what();
    throw runtime_error(msg.c_str());
}
}

size_t plate::xMod(size_t x) const
{
    return (x + _worldDimension.getWidth()) % _worldDimension.getWidth();
}

size_t plate::yMod(size_t y) const
{
    return (y + _worldDimension.getHeight()) % _worldDimension.getHeight();
}

bool plate::contains(size_t x, size_t y) const
{
    size_t cleanX = xMod(x);
    size_t cleanY = yMod(y);

    return cleanX>=left && cleanX<(left+width) && cleanY>=top && cleanY<(top+height);
}

void plate::calculateCrust(size_t x, size_t y, size_t index, 
    float& w_crust, float& e_crust, float& n_crust, float& s_crust,
    size_t& w, size_t& e, size_t& n, size_t& s)
{
try {    
    // Build masks for accessible directions (4-way).
    // Allow wrapping around map edges if plate has world wide dimensions.
    size_t w_mask = -((x > 0)          | (width == _worldDimension.getWidth()));
    size_t e_mask = -((x < width - 1)  | (width == _worldDimension.getWidth()));
    size_t n_mask = -((y > 0)          | (height == _worldDimension.getHeight()));
    size_t s_mask = -((y < height - 1) | (height == _worldDimension.getHeight()));

    // Calculate the x and y offset of neighbour directions.
    // If neighbour is out of plate edges, set it to zero. This protects
    // map memory reads from segment faulting.
    w = w_mask==-1 ? xMod(x-1) : 0;
    e = e_mask==-1 ? xMod(x+1) : 0;
    n = n_mask==-1 ? yMod(y-1) : 0;
    s = s_mask==-1 ? yMod(y+1) : 0;

    // Calculate offsets within map memory.
    w = y * width + w;
    e = y * width + e;
    n = n * width + x;
    s = s * width + x;

    // Extract neighbours heights. Apply validity filtering: 0 is invalid.
    w_crust = map[w] * (w_mask & (map[w] < map[index]));
    e_crust = map[e] * (e_mask & (map[e] < map[index]));
    n_crust = map[n] * (n_mask & (map[n] < map[index]));
    s_crust = map[s] * (s_mask & (map[s] < map[index]));    
} catch (const exception& e){
    std::string msg = "Problem during plate::calculateCrust (width: ";
    // avoid Mingw32 bug (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52015)
    #ifndef __MINGW32__
    #ifndef _MSC_VER
    msg = msg + to_string(width)
            + ", height: " + to_string(height) 
            + ", left: " + to_string(left) 
            + ", top: " + to_string(top) 
            + ", x: " + to_string(x)
            + ", y:" + to_string(y) + ") :"
            + e.what();
    #endif
    #endif
    throw runtime_error(msg.c_str());
}
}

void plate::erode(float lower_bound)
{
try {    
  vector<size_t> sources_data;
  vector<size_t> sinks_data;
  vector<size_t>* sources = &sources_data;
  vector<size_t>* sinks = &sinks_data;

  float* tmp = new float[width*height];
  map.copy_raw_to(tmp);

  // Find all tops.
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
        const size_t index = y * width + x;

        if (map[index] < lower_bound) {
            continue;
        }

        float w_crust, e_crust, n_crust, s_crust;
        size_t w, e, n, s;
        calculateCrust(x, y, index, w_crust, e_crust, n_crust, s_crust,
            w, e, n, s);

        // This location is either at the edge of the plate or it is not the
        // tallest of its neightbours. Don't start a river from here.
        if (w_crust * e_crust * n_crust * s_crust == 0) {
            continue;
        }

        sources->push_back(index);
    }
  }

  size_t* isDone = new size_t[width*height];
  memset(isDone, 0, width*height*sizeof(size_t));

  // From each top, start flowing water along the steepest slope.
  while (!sources->empty()) {
    while (!sources->empty()) {
        const size_t index = sources->back();
        const size_t y = index / width;
        const size_t x = index - y * width;

        sources->pop_back();

        if (map[index] < lower_bound) {
            continue;
        }

        float w_crust, e_crust, n_crust, s_crust;
        size_t w, e, n, s;
        calculateCrust(x, y, index, w_crust, e_crust, n_crust, s_crust,
            w, e, n, s);

        // If this is the lowest part of its neighbourhood, stop.
        if (w_crust + e_crust + n_crust + s_crust == 0) {
            continue;
        }

        w_crust += (w_crust == 0) * map[index];
        e_crust += (e_crust == 0) * map[index];
        n_crust += (n_crust == 0) * map[index];
        s_crust += (s_crust == 0) * map[index];

        // Find lowest neighbour.
        float lowest_crust = w_crust;
        size_t dest = index - 1;

        if (e_crust < lowest_crust) {
            lowest_crust = e_crust;
            dest = index + 1;
        }

        if (n_crust < lowest_crust) {
            lowest_crust = n_crust;
            dest = index - width;
        }

        if (s_crust < lowest_crust) {
            lowest_crust = s_crust;
            dest = index + width;
        }

        // if it's not handled yet, add it as new sink.
        if (dest < width * height && !isDone[dest]) {
            sinks->push_back(dest);
            isDone[dest] = 1;
        }

        // Erode this location with the water flow.
        tmp[index] -= (tmp[index] - lower_bound) * 0.2;
    }


    vector<size_t>* v_tmp = sources;
    sources = sinks;
    sinks = v_tmp;
    sinks->clear();
  }

  delete[] isDone;

  // Add random noise (10 %) to heightmap.
  for (size_t i = 0; i < width*height; ++i)
  {
    float alpha = 0.2 * _randsource() / (float)_randsource.max();
    tmp[i] += 0.1 * tmp[i] - alpha * tmp[i];
  }

  memcpy(map.raw_data(), tmp, width*height*sizeof(float));
  memset(tmp, 0, width*height*sizeof(float));
  mass = 0;
  cx = cy = 0;

  for (size_t y = 0; y < height; ++y)
    for (size_t x = 0; x < width; ++x)
    {
    const size_t index = y * width + x;
    mass += map[index];
    tmp[index] += map[index]; // Careful not to overwrite earlier amounts.

    // Update the center coordinates weighted by mass.
    cx += x * map[index];
    cy += y * map[index];

    if (map[index] < lower_bound)
        continue;

    float w_crust, e_crust, n_crust, s_crust;
    size_t w, e, n, s;
    calculateCrust(x, y, index, w_crust, e_crust, n_crust, s_crust,
        w, e, n, s);

    // This location has no neighbours (ARTIFACT!) or it is the lowest
    // part of its area. In either case the work here is done.
    if (w_crust + e_crust + n_crust + s_crust == 0)
        continue;

    // The steeper the slope, the more water flows along it.
    // The more downhill (sources), the more water flows to here.
    // 1+1+10 = 12, avg = 4, stdev = sqrt((3*3+3*3+6*6)/3) = 4.2, var = 18,
    //  1*1+1*1+10*10 = 102, 102/4.2=24
    // 1+4+7 = 12, avg = 4, stdev = sqrt((3*3+0*0+3*3)/3) = 2.4, var = 6,
    //  1*1+4*4+7*7 = 66, 66/2.4 = 27
    // 4+4+4 = 12, avg = 4, stdev = sqrt((0*0+0*0+0*0)/3) = 0, var = 0,
    //  4*4+4*4+4*4 = 48, 48/0 = inf -> 48
    // If there's a source slope of height X then it will always cause
    // water erosion of amount Y. Then again from one spot only so much
    // water can flow.
    // Thus, the calculated non-linear flow value for this location is
    // multiplied by the "water erosion" constant.
    // The result is max(result, 1.0). New height of this location could
    // be e.g. h_lowest + (1 - 1 / result) * (h_0 - h_lowest).

    // Calculate the difference in height between this point and its
    // nbours that are lower than this point.
    float w_diff = map[index] - w_crust;
    float e_diff = map[index] - e_crust;
    float n_diff = map[index] - n_crust;
    float s_diff = map[index] - s_crust;

    float min_diff = w_diff;
    min_diff -= (min_diff - e_diff) * (e_diff < min_diff);
    min_diff -= (min_diff - n_diff) * (n_diff < min_diff);
    min_diff -= (min_diff - s_diff) * (s_diff < min_diff);

    // Calculate the sum of difference between lower neighbours and
    // the TALLEST lower neighbour.
    float diff_sum = (w_diff - min_diff) * (w_crust > 0) +
                     (e_diff - min_diff) * (e_crust > 0) +
                     (n_diff - min_diff) * (n_crust > 0) +
                     (s_diff - min_diff) * (s_crust > 0);

    // Erosion difference sum is negative!
    assert(diff_sum >= 0);

    if (diff_sum < min_diff)
    {
        // There's NOT enough room in neighbours to contain all the
        // crust from this peak so that it would be as tall as its
        // tallest lower neighbour. Thus first step is make ALL
        // lower neighbours and this point equally tall.
        tmp[w] += (w_diff - min_diff) * (w_crust > 0);
        tmp[e] += (e_diff - min_diff) * (e_crust > 0);
        tmp[n] += (n_diff - min_diff) * (n_crust > 0);
        tmp[s] += (s_diff - min_diff) * (s_crust > 0);
        tmp[index] -= min_diff;

        min_diff -= diff_sum;

        // Spread the remaining crust equally among all lower nbours.
        min_diff /= 1 + (w_crust > 0) + (e_crust > 0) +
            (n_crust > 0) + (s_crust > 0);

        tmp[w] += min_diff * (w_crust > 0);
        tmp[e] += min_diff * (e_crust > 0);
        tmp[n] += min_diff * (n_crust > 0);
        tmp[s] += min_diff * (s_crust > 0);
        tmp[index] += min_diff;
    }
    else
    {
        float unit = min_diff / diff_sum;

        // Remove all crust from this location making it as tall as
        // its tallest lower neighbour.
        tmp[index] -= min_diff;

        // Spread all removed crust among all other lower neighbours.
        tmp[w] += unit * (w_diff - min_diff) * (w_crust > 0);
        tmp[e] += unit * (e_diff - min_diff) * (e_crust > 0);
        tmp[n] += unit * (n_diff - min_diff) * (n_crust > 0);
        tmp[s] += unit * (s_diff - min_diff) * (s_crust > 0);
    }
    }

  map.from(tmp);

  if (mass > 0)
  {
    cx /= mass;
    cy /= mass;
  }
} catch (const exception& e){
    std::string msg = "Problem during plate::erode: ";
    msg = msg + e.what();
    throw runtime_error(msg.c_str());
}
}

void plate::getCollisionInfo(size_t wx, size_t wy, size_t* count, float* ratio) const
{
    ContinentId seg = getContinentAt(wx, wy);

    *count = 0;
    *ratio = 0;

    *count = seg_data[seg].coll_count;
    *ratio = (float)seg_data[seg].coll_count /
        (float)(1 + seg_data[seg].area); // +1 avoids DIV with zero.
}

size_t plate::getContinentArea(size_t wx, size_t wy) const
{
    const size_t index = getMapIndex(&wx, &wy);

    assert(segment[index] < seg_data.size());

    return seg_data[segment[index]].area;
}

float plate::getCrust(size_t x, size_t y) const
{
    const size_t index = getMapIndex(&x, &y);
    return index < (size_t)(-1) ? map[index] : 0;
}

size_t plate::getCrustTimestamp(size_t x, size_t y) const
{
    const size_t index = getMapIndex(&x, &y);
    return index < (size_t)(-1) ? age_map[index] : 0;
}

void plate::getMap(const float** c, const size_t** t) const
{
    if (c) {
        *c = map.raw_data();
    }
    if (t) {
        *t = age_map.raw_data();
    }
}

void plate::move()
{
try {    
    float len;

    // Apply any new impulses to the plate's trajectory.
    vx += dx;
    vy += dy;
    dx = 0;
    dy = 0;

    // Force direction of plate to be unit vector.
    // Update velocity so that the distance of movement doesn't change.
    len = sqrt(vx*vx+vy*vy);
    vx /= len;
    vy /= len;
    velocity += len - 1.0;
    velocity *= velocity > 0; // Round negative values to zero.

    // Apply some circular motion to the plate.
    // Force the radius of the circle to remain fixed by adjusting
    // angular velocity (which depends on plate's velocity).
    size_t world_avg_side = (_worldDimension.getWidth() + _worldDimension.getHeight()) / 2;
    float alpha = rot_dir * velocity / (world_avg_side * 0.33);
    float _cos = cos(alpha * velocity);
    float _sin = sin(alpha * velocity);
    float _vx = vx * _cos - vy * _sin;
    float _vy = vy * _cos + vx * _sin;
    vx = _vx;
    vy = _vy;

    // Location modulations into range [0..world width/height[ are a have to!
    // If left undone SOMETHING WILL BREAK DOWN SOMEWHERE in the code!

    assert(_worldDimension.contains(left, top));

    left += vx * velocity;
    left += left > 0 ? 0 : _worldDimension.getWidth();
    left -= left < _worldDimension.getWidth() ? 0 : _worldDimension.getWidth();

    top += vy * velocity;
    top += top > 0 ? 0 : _worldDimension.getHeight();
    top -= top < _worldDimension.getHeight() ? 0 : _worldDimension.getHeight();

    assert(_worldDimension.contains(left, top));
} catch (const exception& e){
    std::string msg = "Problem during plate::move: ";
    msg = msg + e.what();
    throw runtime_error(msg.c_str());
}
}

void plate::resetSegments()
{
    memset(segment, -1, sizeof(size_t) * width * height);
    seg_data.clear();
}

void plate::setCrust(size_t x, size_t y, float z, size_t t)
{
try {    
    if (z < 0) { // Do not accept negative values.
        z = 0;
    }

    size_t _x = x;
    size_t _y = y;
    size_t index = getMapIndex(&_x, &_y);

    if (index >= width*height)
    {
        // Extending plate for nothing!
        assert(z>0);

        const size_t ilft = left;
        const size_t itop = top;
        const size_t irgt = ilft + width - 1;
        const size_t ibtm = itop + height - 1;

        _worldDimension.normalize(x, y);

        // Calculate distance of new point from plate edges.
        const size_t _lft = ilft - x;
        const size_t _rgt = (_worldDimension.getWidth() & -(x < ilft)) + x - irgt;
        const size_t _top = itop - y;
        const size_t _btm = (_worldDimension.getHeight() & -(y < itop)) + y - ibtm;

        // Set larger of horizontal/vertical distance to zero.
        // A valid distance is NEVER larger than world's side's length!
        size_t d_lft = _lft & -(_lft <  _rgt) & -(_lft < _worldDimension.getWidth());
        size_t d_rgt = _rgt & -(_rgt <= _lft) & -(_rgt < _worldDimension.getWidth());
        size_t d_top = _top & -(_top <  _btm) & -(_top < _worldDimension.getHeight());
        size_t d_btm = _btm & -(_btm <= _top) & -(_btm < _worldDimension.getHeight());

        // Scale all changes to multiple of 8.
        d_lft = ((d_lft > 0) + (d_lft >> 3)) << 3;
        d_rgt = ((d_rgt > 0) + (d_rgt >> 3)) << 3;
        d_top = ((d_top > 0) + (d_top >> 3)) << 3;
        d_btm = ((d_btm > 0) + (d_btm >> 3)) << 3;

        // Make sure plate doesn't grow bigger than the system it's in!
        if (width + d_lft + d_rgt > _worldDimension.getWidth())
        {
            d_lft = 0;
            d_rgt = _worldDimension.getWidth() - width;
        }

        if (height + d_top + d_btm > _worldDimension.getHeight())
        {
            d_top = 0;
            d_btm = _worldDimension.getHeight() - height;
        }

        // Index out of bounds, but nowhere to grow!
        assert(d_lft + d_rgt + d_top + d_btm != 0);

        const size_t old_width  = width;
        const size_t old_height = height;
        
        left -= d_lft;
        left += left >= 0 ? 0 : _worldDimension.getWidth();
        width += d_lft + d_rgt;

        top -= d_top;
        top += top >= 0 ? 0 : _worldDimension.getHeight();
        height += d_top + d_btm;

        HeightMap tmph = HeightMap(width, height);
        AgeMap    tmpa = AgeMap(width, height);
        size_t* tmps = new size_t[width*height];
        tmph.set_all(0);
        tmpa.set_all(0);
        memset(tmps, 255, width*height*sizeof(size_t));

        // copy old plate into new.
        for (size_t j = 0; j < old_height; ++j)
        {
            const size_t dest_i = (d_top + j) * width + d_lft;
            const size_t src_i = j * old_width;
            memcpy(&tmph[dest_i], &map[src_i], old_width *
                sizeof(float));
            memcpy(&tmpa[dest_i], &age_map[src_i], old_width *
                sizeof(size_t));
            memcpy(&tmps[dest_i], &segment[src_i], old_width *
                sizeof(size_t));
        }

        delete[] segment;
        map     = tmph;
        age_map = tmpa;
        segment = tmps;

        // Shift all segment data to match new coordinates.
        for (size_t s = 0; s < seg_data.size(); ++s)
        {
            seg_data[s].shift(d_lft, d_top);
        }

        _x = x, _y = y;
        index = getMapIndex(&_x, &_y);

        assert(index < width * height);
    }

    // Update crust's age.
    // If old crust exists, new age is mean of original and supplied ages.
    // If no new crust is added, original time remains intact.
    const size_t old_crust = -(map[index] > 0);
    const size_t new_crust = -(z > 0);
    t = (t & ~old_crust) | ((size_t)((map[index] * age_map[index] + z * t) /
        (map[index] + z)) & old_crust);
    age_map[index] = (t & new_crust) | (age_map[index] & ~new_crust);

    mass -= map[index];
    map[index] = z;     // Set new crust height to desired location.
    mass += z;      // Update mass counter.
} catch (const exception& e){
    std::string msg = "Problem during plate::setCrust: ";
    msg = msg + e.what();
    throw runtime_error(msg.c_str());
}
}

ContinentId plate::selectCollisionSegment(size_t coll_x, size_t coll_y)
{
    size_t index = getMapIndex(&coll_x, &coll_y);
    ContinentId activeContinent = segment[index];
    return activeContinent;
}

///////////////////////////////////////////////////////////////////////////////
/// Private methods ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

size_t plate::createSegment(size_t x, size_t y) throw()
{
try {    
    const size_t origin_index = y * width + x;
    const size_t ID = seg_data.size();

    if (segment[origin_index] < ID) {
        return segment[origin_index];
    }

    size_t canGoLeft  = x > 0          && map[origin_index - 1]     >= CONT_BASE;
    size_t canGoRight = x < width - 1  && map[origin_index+1]       >= CONT_BASE;
    size_t canGoUp    = y > 0          && map[origin_index - width] >= CONT_BASE;
    size_t canGoDown  = y < height - 1 && map[origin_index + width] >= CONT_BASE;
    size_t nbour_id = ID;

    // This point belongs to no segment yet.
    // However it might be a neighbour to some segment created earlier.
    // If such neighbour is found, associate this point with it.
    if (canGoLeft && segment[origin_index - 1] < ID) {
        nbour_id = segment[origin_index - 1];
    } else if (canGoRight && segment[origin_index + 1] < ID) {
        nbour_id = segment[origin_index + 1];
    } else if (canGoUp && segment[origin_index - width] < ID) {
        nbour_id = segment[origin_index - width];
    } else if (canGoDown && segment[origin_index + width] < ID) {
        nbour_id = segment[origin_index + width];
    }

    if (nbour_id < ID)
    {
        segment[origin_index] = nbour_id;
        ++seg_data[nbour_id].area;

        seg_data[nbour_id].enlarge_to_contain(x, y);

        return nbour_id;
    }

    size_t lines_processed;
    Rectangle r = Rectangle(_worldDimension, x, x, y, y);
    segmentData data(r, 0);

    std::vector<size_t>* spans_todo = new std::vector<size_t>[height];
    std::vector<size_t>* spans_done = new std::vector<size_t>[height];

    segment[origin_index] = ID;
    spans_todo[y].push_back(x);
    spans_todo[y].push_back(x);

    do
    {
      lines_processed = 0;
      for (size_t line = 0; line < height; ++line)
      {
        size_t start, end;

        if (spans_todo[line].size() == 0)
            continue;

        do // Find an unscanned span on this line.
        {
            end = spans_todo[line].back();
            spans_todo[line].pop_back();

            start = spans_todo[line].back();
            spans_todo[line].pop_back();

            // Reduce any done spans from this span.
            for (size_t j = 0; j < spans_done[line].size();
                 j += 2)
            {
                // Saved coordinates are AT the point
                // that was included last to the span.
                // That's why equalities matter.

                if (start >= spans_done[line][j] &&
                    start <= spans_done[line][j+1])
                    start = spans_done[line][j+1] + 1;

                if (end >= spans_done[line][j] &&
                    end <= spans_done[line][j+1])
                    end = spans_done[line][j] - 1;
            }

            // Unsigned-ness hacking!
            // Required to fix the underflow of end - 1.
            start |= -(end >= width);
            end -= (end >= width);

        } while (start > end && spans_todo[line].size());

        if (start > end) // Nothing to do here anymore...
            continue;

        // Calculate line indices. Allow wrapping around map edges.
        const size_t row_above = ((line - 1) & -(line > 0)) |
            ((height - 1) & -(line == 0));
        const size_t row_below = (line + 1) & -(line < height - 1);
        const size_t line_here = line * width;
        const size_t line_above = row_above * width;
        const size_t line_below = row_below * width;

        // Extend the beginning of line.
        while (start > 0 && segment[line_here+start-1] > ID &&
            map[line_here+start-1] >= CONT_BASE)
        {
            --start;
            segment[line_here + start] = ID;

            // Count volume of pixel...
        }

        // Extend the end of line.
        while (end < width - 1 &&
            segment[line_here + end + 1] > ID &&
            map[line_here + end + 1] >= CONT_BASE)
        {
            ++end;
            segment[line_here + end] = ID;

            // Count volume of pixel...
        }

        // Check if should wrap around left edge.
        if (width == _worldDimension.getWidth() && start == 0 &&
            segment[line_here+width-1] > ID &&
            map[line_here+width-1] >= CONT_BASE)
        {
            segment[line_here + width - 1] = ID;
            spans_todo[line].push_back(width - 1);
            spans_todo[line].push_back(width - 1);

            // Count volume of pixel...
        }

        // Check if should wrap around right edge.
        if (width == _worldDimension.getWidth() && end == width - 1 &&
            segment[line_here+0] > ID &&
            map[line_here+0] >= CONT_BASE)
        {
            segment[line_here + 0] = ID;
            spans_todo[line].push_back(0);
            spans_todo[line].push_back(0);

            // Count volume of pixel...
        }

        data.area += 1 + end - start; // Update segment area counter.

        // Record any changes in extreme dimensions.
        if (line < data.getTop()) data.setTop(line);
        if (line > data.getBottom()) data.setBottom(line);
        if (start < data.getLeft()) data.setLeft(start);
        if (end > data.getRight()) data.setRight(end);

        if (line > 0 || height == _worldDimension.getHeight())
        for (size_t j = start; j <= end; ++j)
          if (segment[line_above + j] > ID &&
              map[line_above + j] >= CONT_BASE)
          {
            size_t a = j;
            segment[line_above + a] = ID;

            // Count volume of pixel...

            while (++j < width &&
                   segment[line_above + j] > ID &&
                   map[line_above + j] >= CONT_BASE)
            {
                segment[line_above + j] = ID;

                // Count volume of pixel...
            }

            size_t b = --j; // Last point is invalid.

            spans_todo[row_above].push_back(a);
            spans_todo[row_above].push_back(b);
            ++j; // Skip the last scanned point.
          }

        if (line < height - 1 || height == _worldDimension.getHeight())
        for (size_t j = start; j <= end; ++j)
          if (segment[line_below + j] > ID &&
              map[line_below + j] >= CONT_BASE)
          {
            size_t a = j;
            segment[line_below + a] = ID;

            // Count volume of pixel...

            while (++j < width &&
                   segment[line_below + j] > ID &&
                   map[line_below + j] >= CONT_BASE)
            {
                segment[line_below + j] = ID;

                // Count volume of pixel...
            }

            size_t b = --j; // Last point is invalid.

            spans_todo[row_below].push_back(a);
            spans_todo[row_below].push_back(b);
            ++j; // Skip the last scanned point.
          }

        spans_done[line].push_back(start);
        spans_done[line].push_back(end);
        ++lines_processed;
      }
    } while (lines_processed > 0);

    delete[] spans_todo;
    delete[] spans_done;
    seg_data.push_back(data);

    return ID;
} catch (const exception& e){
    std::string msg = "Problem during plate::createSegement: ";
    msg = msg + e.what();
    throw runtime_error(msg.c_str());
}
}

size_t plate::getMapIndex(size_t* px, size_t* py) const throw()
{
    const size_t ilft = (size_t)(int)left;
    const size_t itop = (size_t)(int)top;
    const size_t irgt = ilft + width;
    const size_t ibtm = itop + height;

    Rectangle rect = Rectangle(_worldDimension, ilft, irgt, itop, ibtm);
    return rect.getMapIndex(px, py);
}

ContinentId plate::getContinentAt(int x, int y) const
{
    size_t lx = x, ly = y;
    size_t index = getMapIndex(&lx, &ly);
    ContinentId seg = segment[index];

    if (seg >= seg_data.size()) {
        // in this case, we consider as const this call because we calculate
        // something that we would calculate anyway, so the segments are
        // a sort of cache
        seg = const_cast<plate*>(this)->createSegment(lx, ly);
    }

    if (seg >= seg_data.size())
    {
        throw invalid_argument("Could not create segment");
    }
    return seg;
}
