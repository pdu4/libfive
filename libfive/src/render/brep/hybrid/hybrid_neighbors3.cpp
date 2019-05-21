/*
libfive: a CAD kernel for modeling with implicit functions

Copyright (C) 2019  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "../neighbors.cpp"
#include "libfive/render/brep/hybrid/hybrid_neighbors.hpp"

namespace Kernel {
template class Neighbors<3, HybridTree<3>, HybridNeighbors<3>>;
template class HybridNeighbors<3>;
}   // namespace Kernel
