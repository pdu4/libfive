/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2019  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>

#include <Eigen/Eigen>

#include "libfive/render/axes.hpp"
#include "libfive/eval/tape.hpp"
#include "libfive/tree/tree.hpp"

namespace Kernel {

// Forward declarations
template <unsigned N> class HybridTree;
template <unsigned N> class PerThreadBRep;
class XTreeEvaluator;
class Mesh;

class HybridMesher
{
public:
    using Output = Mesh;
    using Input = HybridTree<3>;

    /*
     *  Constructs a mesher that owns an evaluator,
     *  which is built from the given tree.
     */
    HybridMesher(PerThreadBRep<3>& m, Tree t);

    /*
     *  Constructs a mesher that has borrowed an evaluator,
     *  which is useful in cases where constructing evaluators
     *  is expensive and they should be re-used.
     */
    HybridMesher(PerThreadBRep<3>& m, XTreeEvaluator* es);

    ~HybridMesher();

    /*
     *  Called by Dual::walk to construct the triangle mesh
     */
    template <Axis::Axis A>
    void load(const std::array<const HybridTree<3>*, 4>& ts);

    /*
     *  Hybrid meshing needs to walk the top edges of the tree,
     *  because those include tets that we have to run MT on.
     */
    static bool needsTopEdges() { return true; }

protected:
    /*
     *  Performs a binary search along a particular edge using the
     *  provided tape.  Stores the resulting vertex into the Mesh m,
     *  and returns its index.
     */
    uint64_t searchEdge(Eigen::Vector3d inside, Eigen::Vector3d outside,
                        std::shared_ptr<Tape> tape);

    PerThreadBRep<3>& m;
    XTreeEvaluator* eval;
    bool owned;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace Kernel

