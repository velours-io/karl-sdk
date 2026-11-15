#pragma once

#include <osg/Geode>
#include <osg/StateSet>

namespace fleet
{
    // The three kinds of enemy ship, smallest first.
    enum class ShipKind
    {
        GALLEY,
        CARAVEL,
        GALLEON,
    };

    namespace models
    {
        // How much bigger ships are drawn than the sizes they are modelled at.
        constexpr float SHIP_SCALE = 1.4f;

        // The shader every model draws with: flat low-poly shading under the
        // same sun as the water, height lifted up the screen (see View.h), and
        // anything below the waterline tinted then cut, so a sinking ship goes
        // under rather than through. Put it on a group above the models.
        osg::ref_ptr<osg::StateSet> modelState();

        // Each model faces +x, sits on the water at z = 0, centred on the
        // origin. Built once and shared between instances.
        osg::ref_ptr<osg::Geode> ship(ShipKind kind);
    }
}
