/*
libfive: a CAD kernel for modeling with implicit functions

Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <chrono>

#include "catch.hpp"

#include "libfive/render/brep/dc/dc_mesher.hpp"
#include "libfive/render/brep/dc/dc_pool.hpp"
#include "libfive/render/brep/dual.hpp"
#include "libfive/render/brep/region.hpp"
#include "libfive/render/brep/mesh.hpp"

#if LIBFIVE_TRIANGLE_FAN_MESHING
#include "libfive/render/brep/dc/intersection_aligner.hpp"
#endif

#include "util/shapes.hpp"
#include "util/mesh_checks.hpp"

using namespace Kernel;

TEST_CASE("Mesh::render (sphere normals)")
{
    Tree s = sphere(0.5);
    Region<3> r({-1, -1, -1}, {1, 1, 1});

    auto mesh = Mesh::render(s, r);

    float dot = 2;
    int pos = 0;
    int neg = 0;
    for (auto t : mesh->branes)
    {
        auto norm = (mesh->verts[t(1)] - mesh->verts[t(0)])
            .cross(mesh->verts[t(2)] - mesh->verts[t(0)])
            .normalized();
        auto center = ((mesh->verts[t(0)] +
                        mesh->verts[t(1)] +
                        mesh->verts[t(2)])).normalized();
        auto dot_ = norm.dot(center);
        neg += (dot_ < 0);
        pos += (dot_ > 0);
        dot = fmin(dot, dot_);
    }
    CAPTURE(neg);
    CAPTURE(pos);
    REQUIRE(dot > 0.9);
}

TEST_CASE("Mesh::render (cube)")
{
    auto cube = max(max(
                    max(-(Tree::X() + 1.5),
                          Tree::X() - 1.5),
                    max(-(Tree::Y() + 1.5),
                          Tree::Y() - 1.5)),
                    max(-(Tree::Z() + 1.5),
                          Tree::Z() - 1.5));
    Region<3> r({-2.5, -2.5, -2.5}, {2.5, 2.5, 2.5});

    auto mesh = Mesh::render(cube, r);
}

TEST_CASE("Mesh::render (cube face count)")
{
    auto cube = max(max(
        max(-(Tree::X() + 1.5),
            Tree::X() - 1.5),
        max(-(Tree::Y() + 1.5),
            Tree::Y() - 1.5)),
        max(-(Tree::Z() + 1.5),
            Tree::Z() - 1.5));

    //  The region is set so we hit where the interesting stuff happens.
    Region<3> r({ -3., -3., -3. }, { 3., 3., 3. });

    auto m = Mesh::render(cube, r, 0.15, 1e-8, false);
    REQUIRE(m->branes.size() == 12);
    REQUIRE(m->verts.size() == 9);
}

TEST_CASE("Mesh::render (face count in rectangular prism)")
{
    auto t = max(max(max(-Tree::X(), Tree::X() - 4),
                     max(-Tree::Y(), Tree::Y() - 1)),
                     max(-Tree::Z(), Tree::Z() - 0.25));
    auto m = Mesh::render(t, Region<3>({-1, -1, -1}, {5, 2, 1.25}), 0.125);
    REQUIRE(m->verts.size() == 9); // index 0 is unused
    REQUIRE(m->branes.size() == 12);
}

TEST_CASE("Mesh::render (different algorithms)")
{
    auto s = sphere(1);
    auto a = Mesh::render(s, Region<3>({-1.6, -1, -8}, {1.6, 1, 1}),
                          1/32.0f, pow(10, -3), true,
                          DUAL_CONTOURING);
    auto b = Mesh::render(s, Region<3>({-1.6, -1, -8}, {1.6, 1, 1}),
                          1/32.0f, pow(10, -3), true,
                          ISO_SIMPLEX);
    REQUIRE(b->branes.size() > a->branes.size());
    REQUIRE(b->verts.size() >  a->verts.size());
}

TEST_CASE("Mesh::render (cone)")
{
    auto z = Tree::Z();
    auto s = 1 / (-z);
    auto r = sqrt(square(Tree::X() * s) + square(Tree::Y() * s));
    auto cone = max(r - 1, max(Tree::Z(), -1 - z));
    auto m = Mesh::render(cone, Region<3>({-10, -10, -10}, {10, 10, 10}), 0.1);
    REQUIRE(true);
}

TEST_CASE("Mesh::render (checking for triangles that are lines)")
{
    auto b = min(sphere(0.7, {0, 0, 0.1}), box({-1, -1, -1}, {1, 1, 0.1}));
    auto mesh = Mesh::render(b, Region<3>({-10, -10, -10}, {10, 10, 10}), 0.25);

    for (const auto& t : mesh->branes)
    {
        // Skip triangles that are actually collapsed into lines
        REQUIRE(t(0) != t(1));
        REQUIRE(t(0) != t(2));
        REQUIRE(t(1) != t(2));
    }
}

TEST_CASE("Mesh::render (checking for flipped triangles)")
{
    auto b = min(sphere(0.7, {0, 0, 0.1}), box({-1, -1, -1}, {1, 1, 0.1}));
    auto mesh = Mesh::render(b, Region<3>({-10, -10, -10}, {10, 10, 10}), 0.25);

    for (const auto& t : mesh->branes)
    {
        // We're only looking at the top face triangles, since that's where
        // flipped triangles are induced.
        bool on_top_face = true;
        for (unsigned i=0; i < 3; ++i)
        {
            on_top_face &= fabs(mesh->verts[t(i)].z() - 0.1) < 1e-3;
        }
        if (on_top_face)
        {
            auto norm = (mesh->verts[t(1)] - mesh->verts[t(0)])
                .cross(mesh->verts[t(2)] - mesh->verts[t(0)])
                .normalized();

            CAPTURE(mesh->verts[t(0)]);
            CAPTURE(mesh->verts[t(1)]);
            CAPTURE(mesh->verts[t(2)]);
            CAPTURE(norm);

            REQUIRE(norm.x() == Approx(0.0f).margin(0.01));
            REQUIRE(norm.y() == Approx(0.0f).margin(0.01));
            REQUIRE(norm.z() == Approx(1.0f).margin(0.01));
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Mesh::render (performance)", "[!benchmark]")
{
    BENCHMARK("Menger sponge")
    {
        Tree sponge = max(menger(2), -sphere(1, {1.5, 1.5, 1.5}));
        Region<3> r({-2.5, -2.5, -2.5}, {2.5, 2.5, 2.5});
        auto mesh = Mesh::render(sponge, r, 0.02);
    }

    BENCHMARK("Gradient blended-round spheres")
    {
        float blendAmt = 0.125f;

        auto boxB = box({ -2,-2,0 }, { 2,2,1 });
        auto sphereB = sphere(2.f, {2.f,2.f,0.f});
        auto blendObj = blend(boxB, sphereB, blendAmt);

        Region<3> r({ -5, -5, -5 }, { 5, 5, 5 });

        auto mesh = Mesh::render(blendObj, r, 0.025);
    }

    BENCHMARK("Sphere / gyroid intersection")
    {
        Region<3> r({ -5, -5, -5 }, { 5, 5, 5 });
        auto mesh = Mesh::render(sphereGyroid(), r, 0.025);
    }
}

TEST_CASE("Mesh::render (gyroid performance breakdown)", "[!benchmark]")
{
    Region<3> r({ -5, -5, -5 }, { 5, 5, 5 });

    Root<DCTree<3>> t;
    unsigned workers = 8;
    std::atomic_bool cancel(false);

    BENCHMARK("DCTree construction")
    {
        t = DCPool<3>::build(sphereGyroid(), r, 0.025, 1e-8, workers);
    }

#if LIBFIVE_TRIANGLE_FAN_MESHING
    BENCHMARK("Intersection alignment")
    {
        workers = 1;
        Dual<3>::walk<IntersectionAligner>(t, workers, cancel, EMPTY_PROGRESS_CALLBACK);
    }
#endif

    std::unique_ptr<Mesh> m;
    BENCHMARK("Mesh building")
    {
        m = Dual<3>::walk<DCMesher>(t, workers, cancel, EMPTY_PROGRESS_CALLBACK, nullptr);
    }

    BENCHMARK("DCTree deletion")
    {
        t.reset();
    }

    BENCHMARK("Mesh deletion")
    {
        m.reset();
    }
}

TEST_CASE("Mesh::render (gyroid with progress callback)", "[!benchmark]")
{
    std::vector<float> progress;
    auto progress_callback = [&](float f)
    {
        progress.push_back(f);
    };

    Region<3> r({ -5, -5, -5 }, { 5, 5, 5 });

    Root<DCTree<3>> t;
    BENCHMARK("DCTree construction")
    {
        t = DCPool<3>::build(sphereGyroid(), r, 0.025, 1e-8, 8,
                             progress_callback);
    }

    std::unique_ptr<Mesh> m;
    std::atomic_bool cancel(false);
    BENCHMARK("Mesh building")
    {
        m = Dual<3>::walk<DCMesher>(t, 8, cancel, EMPTY_PROGRESS_CALLBACK, nullptr);
    }

    BENCHMARK("DCTree deletion")
    {
        t.reset(8, progress_callback);
    }

    // Confirm that the progress counter is monotonically increasing
    CAPTURE(progress);
    float prev = -1;
    for (auto& p : progress)
    {
        REQUIRE(p > prev);
        prev = p;
    }
    REQUIRE(progress[0] == 0.0f);
    REQUIRE(prev == 3.0f);
}

TEST_CASE("Mesh::render (edge pairing)")
{
    auto c = sphere(0.5);
    auto r = Region<3>({-1, -1, -1}, {1, 1, 1});

    auto m = Mesh::render(c, r, 1.1, 1e-8, 1);
    CHECK_EDGE_PAIRS(*m);
}
