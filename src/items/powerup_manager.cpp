//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2006-2015 Joerg Henrichs
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "items/powerup_manager.hpp"

#include <cinttypes>
#include <stdexcept>

#include <irrlicht.h>

#include "graphics/irr_driver.hpp"
#include "graphics/sp/sp_base.hpp"
#include "graphics/material.hpp"
#include "graphics/material_manager.hpp"
#include "io/file_manager.hpp"
#include "io/xml_node.hpp"
#include "items/bowling.hpp"
#include "items/cake.hpp"
#include "items/plunger.hpp"
#include "items/rubber_ball.hpp"
#include "modes/profile_world.hpp"
#include "utils/constants.hpp"
#include "utils/string_utils.hpp"

static constexpr unsigned EXPECTED_NUM_POWERUPS
    = 2 * static_cast<unsigned>(PowerupManager::PowerupType::POWERUP_LAST);

PowerupManager* powerup_manager=0;

//-----------------------------------------------------------------------------
/** The constructor initialises everything to zero. */
PowerupManager::PowerupManager()
{
    m_random_seed.store(0);
    for(int i=0; i<POWERUP_MAX; i++)
    {
        m_all_meshes[i] = NULL;
        m_all_icons[i]  = (Material*)NULL;
    }
}   // PowerupManager

//-----------------------------------------------------------------------------
/** Destructor, frees all meshes. */
PowerupManager::~PowerupManager()
{
    for(unsigned int i=POWERUP_FIRST; i<=POWERUP_LAST; i++)
    {
        scene::IMesh *mesh = m_all_meshes[(PowerupType)i];
        if(mesh)
        {
            mesh->drop();
            // If the ref count is 1, the only reference is in
            // irrlicht's mesh cache, from which the mesh can
            // then be deleted.
            // Note that this test is necessary, since some meshes
            // are also used in attachment_manager!!!
            if(mesh->getReferenceCount()==1)
                irr_driver->removeMeshFromCache(mesh);
        }
    }
}   // ~PowerupManager

//-----------------------------------------------------------------------------
/** Removes any textures so that they can be reloaded.
 */
void PowerupManager::unloadPowerups()
{
    for(unsigned int i=POWERUP_FIRST; i<=POWERUP_LAST; i++)
    {
        if(m_all_meshes[(PowerupType)i])
            m_all_meshes[(PowerupType)i]->drop();

        //FIXME: I'm not sure if this is OK or if I need to ->drop(),
        //       or delete them, or...
        m_all_icons[i]  = (Material*)NULL;
    }
}   // removeTextures

//-----------------------------------------------------------------------------
/** Determines the powerup type for a given name.
 *  \param name Name of the powerup to look up.
 *  \return The type, or POWERUP_NOTHING if the name is not found
 */
PowerupManager::PowerupType
    PowerupManager::getPowerupType(const std::string &name) const
{
    // Must match the order of PowerupType in powerup_manager.hpp!!
    static std::string powerup_names[] = {
        "",            /* Nothing */
        "bubblegum", "cake", "bowling", "zipper", "plunger", "switch",
        "swatter", "rubber-ball", "parachute", "anchor"
    };

    for(unsigned int i=POWERUP_FIRST; i<=POWERUP_LAST; i++)
    {
        if(powerup_names[i]==name) return(PowerupType)i;
    }
    return POWERUP_NOTHING;
}   // getPowerupType

//-----------------------------------------------------------------------------
/** Loads powerups models and icons from the powerup.xml file.
 */
void PowerupManager::loadPowerupsModels()
{
    const std::string file_name = file_manager->getAsset("powerup.xml");
    XMLNode *root               = file_manager->createXMLTree(file_name);
    for(unsigned int i=0; i<root->getNumNodes(); i++)
    {
        const XMLNode *node=root->getNode(i);
        if(node->getName()!="item") continue;
        std::string name;
        node->get("name", &name);
        PowerupType type = getPowerupType(name);
        // The weight nodes will be also included in this list, so ignore those
        if(type!=POWERUP_NOTHING)
            loadPowerup(type, *node);
        else
        {
            Log::fatal("PowerupManager",
                       "Can't find item '%s' from powerup.xml, entry %d.",
                       name.c_str(), i+1);
            exit(-1);
        }
    }
    
    loadWeights(root, "race-weight-list"    );
    // loadWeights(root, "ftl-weight-list"     );
    loadWeights(root, "battle-weight-list"  );
    loadWeights(root, "soccer-weight-list"  );
    loadWeights(root, "tutorial-weight-list");

    delete root;

    if (ProfileWorld::isNoGraphics())
    {
        for (unsigned i = POWERUP_FIRST; i <= POWERUP_LAST; i++)
        {
            scene::IMesh *mesh = m_all_meshes[(PowerupType)i];
            if (mesh)
            {
                // After minMax3D from loadPowerup mesh can free its vertex
                // buffer
                mesh->freeMeshVertexBuffer();
            }
        }
    }
}  // loadPowerupsModels

static PowerupManager::PowerupType convertToPowerupType(unsigned val) {
    if (PowerupManager::POWERUP_FIRST <= val && val <= PowerupManager::POWERUP_LAST)
        return static_cast<PowerupManager::PowerupType>(val);
    return PowerupManager::POWERUP_NOTHING;
}

template<typename F>
static void readWeightRow(const std::string items, F op)
{
    std::vector<std::string> string_values = StringUtils::split(items, ' ');

    int i = PowerupManager::PowerupType::POWERUP_FIRST;
    for (auto &&str : string_values)
    {
        if (str.empty())
            continue;

        int weight;
        StringUtils::fromString(str, weight);
        op(convertToPowerupType(i++), weight);
    }
}

static std::vector<PowerupManager::WeightedPowerup>
loadPowerupWeights(const XMLNode *node)
{
    using PowerupType = PowerupManager::PowerupType;
    using WeightedPowerup = PowerupManager::WeightedPowerup;

    std::vector<WeightedPowerup> weighted_powerups;

    std::string single_item;
    node->get("single", &single_item);
    std::string multi_item;
    node->get("multi", &multi_item);

    readWeightRow(single_item, [&] (PowerupType type, uint64_t weight) {
        weighted_powerups.emplace_back(weight, 1, type);
    });

    readWeightRow(multi_item, [&] (PowerupType type, uint64_t weight) {
        weighted_powerups.emplace_back(weight, 3, type);
    });

    // Make sure we have the right number of entries
    if (weighted_powerups.size() < EXPECTED_NUM_POWERUPS)
    {
        Log::fatal("PowerupManager",
                   "Not enough entries for '%s' in powerup.xml",
                   node->getName().c_str());

        while (weighted_powerups.size() < EXPECTED_NUM_POWERUPS)
        {
            weighted_powerups.emplace_back(0, 0, PowerupType::POWERUP_NOTHING);
        }
    }
    else if (weighted_powerups.size() > EXPECTED_NUM_POWERUPS)
    {
        Log::fatal("PowerupManager",
                   "Too many entries for '%s' in powerup.xml.",
                   node->getName().c_str());
    }

    return weighted_powerups;
}

static PowerupManager::WeightsData loadPowerupWeightNode(const XMLNode *node) {
    float distance;
    node->get("distance", &distance);

    return PowerupManager::WeightsData(distance, loadPowerupWeights(node));
}

static void
postProcessWeightData(const std::vector<PowerupManager::WeightsData> &raw_data,
                      std::vector<PowerupManager::WeightsData> &processed_data)
{
    using PowerupType = PowerupManager::PowerupType;
    using WeightedPowerup = PowerupManager::WeightedPowerup;

    for (auto &&weight_data : raw_data)
    {
        const uint64_t new_distance = weight_data.getDistance();
        std::vector<WeightedPowerup> new_weights;

        for (auto &weighted_powerup : weight_data.getPowerupWeights())
        {
            // If it's not going to be considered, don't bother adding it to the
            // set of possibilities.
            if (weighted_powerup.getWeight() < 1)
                continue;

            // If it's for an invalid item, filter it out
            if (weighted_powerup.getType() == PowerupType::POWERUP_NOTHING)
                continue;

            if (new_weights.empty())
                new_weights.push_back(weighted_powerup);
            else
                new_weights.push_back(weighted_powerup.merge(new_weights.back()));
        }

        processed_data.emplace_back(new_distance, std::move(new_weights));
    }
}

//-----------------------------------------------------------------------------
/** Loads the powerups weights for a given category (race, ft, ...). The data
 *  is stored in m_weights.
 *  \param node The top node of the powerup xml file.
 *  \param class_name The name of the attribute with the weights for the
 *         class.
 */
void PowerupManager::loadWeights(const XMLNode *powerup_node,
                                 const std::string &class_name)
{
    const XMLNode *node = powerup_node->getNode(class_name);
    if(!node)
    {
        Log::fatal("PowerupManager",
                   "Cannot find node '%s' in powerup.xml file.",
                   class_name.c_str());
    }

    std::vector<WeightsData> raw_data;
    for (unsigned int i = 0; i < node->getNumNodes(); i++)
    {
        const XMLNode *weights = node->getNode(i);
        raw_data.push_back(loadPowerupWeightNode(weights));
    }    // for i in node->getNumNodes

    postProcessWeightData(raw_data, m_all_weights[class_name]);
}  // loadWeights

// ============================================================================
// Implement of WeightsData

PowerupManager::WeightsData::WeightsData(float distance,
                                         std::vector<WeightedPowerup> &&weights)
    : m_distance(distance), m_weights(std::move(weights))
{
    auto max_res = std::max_element(m_weights.begin(), m_weights.end());
    m_cfd = max_res->getWeight();
}

//-----------------------------------------------------------------------------
/** Computes a random item dependent on the rank of the kart and a given
 *  random number.
 *
 *  \param random_number A random number used to 'randomly' select the item
 *         that was picked.
 */
const PowerupManager::WeightedPowerup &
PowerupManager::WeightsData::getRandomItem(uint64_t random_number) const
{
#undef ITEM_DISTRIBUTION_DEBUG
#ifdef ITEM_DISTRIBUTION_DEBUG
    uint64_t original_random_number = random_number;
#endif
    random_number = random_number % m_cfd;

    auto powerup_it = std::lower_bound(
        m_weights.begin(), m_weights.end(), random_number,
        [] (const WeightedPowerup &powerup, uint64_t val) {
            return powerup.getWeight() < val;
        });

    // We should always get something from this.
    assert(powerup_it != m_weights.end());
    const WeightedPowerup &powerup = *powerup_it;

    // We align with the beginning of the enum and return
    // We don't do more, because it would need to be decoded from enum later
#ifdef ITEM_DISTRIBUTION_DEBUG
    Log::verbose("Powerup", "World %d random %d %" PRIu64 " item %d",
                 World::getWorld()->getTicksSinceStart(), random_number,
                 original_random_number, powerup.getType());
#endif

    return powerup;
}   // WeightsData::getRandomItem

// ============================================================================
/** Loads the data for one particular powerup. For bowling ball, plunger, and
 *  cake static members in the appropriate classes are called to store
 *  additional information for those objects.
 *  \param type The type of the powerup.
 *  \param node The XML node with the data for this powerup.
 */
void PowerupManager::loadPowerup(PowerupType type, const XMLNode &node)
{
    std::string icon_file("");
    node.get("icon", &icon_file);

#ifdef DEBUG
    if (icon_file.size() == 0)
    {
        Log::fatal("PowerupManager",
                   "Cannot load powerup %i, no 'icon' attribute under XML node",
                   type);
    }
#endif

    m_all_icons[type] = material_manager->getMaterial(icon_file,
                                  /* full_path */     false,
                                  /*make_permanent */ true);


    assert(m_all_icons[type] != NULL);
    assert(m_all_icons[type]->getTexture() != NULL);

    std::string model("");
    node.get("model", &model);
    if(model.size()>0)
    {
        std::string full_path = file_manager->getAsset(FileManager::MODEL,model);
        m_all_meshes[type] = irr_driver->getMesh(full_path);
        if(!m_all_meshes[type])
        {
            std::ostringstream o;
            o << "Can't load model '" << model << "' for powerup type '" << type
              << "', aborting.";
            throw std::runtime_error(o.str());
        }
#ifndef SERVER_ONLY
        SP::uploadSPM(m_all_meshes[type]);
#endif
        m_all_meshes[type]->grab();
    }
    else
    {
        m_all_meshes[type] = 0;
    }
    // Load special attributes for certain powerups
    switch (type) {
        case POWERUP_BOWLING:
             Bowling::init(node, m_all_meshes[type]);    break;
        case POWERUP_PLUNGER:
             Plunger::init(node, m_all_meshes[type]);    break;
        case POWERUP_CAKE:
             Cake::init(node, m_all_meshes[type]);       break;
        case POWERUP_RUBBERBALL:
             RubberBall::init(node, m_all_meshes[type]); break;
        default: break;
    }   // switch
}   // loadPowerup

void PowerupManager::selectWeightsForCurMode()
{
    std::string class_name;
    switch (race_manager->getMinorMode())
    {
    case RaceManager::MINOR_MODE_TIME_TRIAL:       /* fall through */
    case RaceManager::MINOR_MODE_NORMAL_RACE:      class_name="race";     break;
    case RaceManager::MINOR_MODE_FOLLOW_LEADER:    class_name="ftl";      break;
    case RaceManager::MINOR_MODE_3_STRIKES:        class_name="battle";   break;
    case RaceManager::MINOR_MODE_FREE_FOR_ALL:     class_name="battle";   break;
    case RaceManager::MINOR_MODE_CAPTURE_THE_FLAG: class_name="battle";   break;
    case RaceManager::MINOR_MODE_TUTORIAL:         class_name="tutorial"; break;
    case RaceManager::MINOR_MODE_EASTER_EGG:       /* fall through */
    case RaceManager::MINOR_MODE_OVERWORLD:
    case RaceManager::MINOR_MODE_CUTSCENE:
    case RaceManager::MINOR_MODE_SOCCER:           class_name="soccer";   break;
    default:
        Log::fatal("PowerupManager", "Invalid minor mode %d - aborting.",
                    race_manager->getMinorMode());
    }
    class_name +="-weight-list";

    m_current_item_weights = m_all_weights[class_name];
}

// ----------------------------------------------------------------------------
/** Returns a random powerup for a kart at a given position. If the race mode
 *  is a battle, the position is actually not used and a randomly selected
 *  item for POSITION_BATTLE_MODE is returned. This function takes the weights
 *  specified for all items into account by using a list which contains all
 *  items depending on the weights defined. See updateWeightsForRace()
 *
 *  \param distance Distance of the kart from the player in first place.
 *  \param random_number A random number used to select the item. Important
 *         for networking to be able to reproduce item selection.
 */
const PowerupManager::WeightedPowerup &
PowerupManager::getRandomPowerup(float distance,
                                 uint64_t random_number)
{
    auto weight_data_it = std::find_if(
        m_current_item_weights.rbegin(), m_current_item_weights.rend(),
        [distance] (const WeightsData& data) {
            return distance >= data.getDistance();
        });

    assert(weight_data_it != m_current_item_weights.rend());
    return weight_data_it->getRandomItem(random_number);
}   // getRandomPowerup

// ============================================================================
/** Unit testing is based on deterministic item distributions: if all random
 *  numbers from 0 till sum_of_all_weights - 1 are used, the original weight
 *  distribution must be restored.
 */
void PowerupManager::unitTesting()
{
//     // Test 1: Test all possible random numbers for tutorial, and
//     // make sure that always three bowling balls are picked.
//     // ----------------------------------------------------------
//     race_manager->setMinorMode(RaceManager::MINOR_MODE_TUTORIAL);
//     powerup_manager->computeWeightsForRace(1);
//     WeightsData wd = powerup_manager->m_current_item_weights;
//     int num_weights = wd.m_summed_weights_for_rank[0].back();
//     for(int i=0; i<num_weights; i++)
//     {
// #ifdef DEBUG
//         unsigned int n;
//         assert( powerup_manager->getRandomPowerup(1, &n, i)==POWERUP_BOWLING );
//         assert(n==3);
// #endif
//     }

//     // Test 2: Test all possible random numbers for 5 karts and rank 5
//     // ---------------------------------------------------------------
//     race_manager->setMinorMode(RaceManager::MINOR_MODE_NORMAL_RACE);
//     int num_karts = 5;
//     powerup_manager->computeWeightsForRace(num_karts);
//     wd = powerup_manager->m_current_item_weights;

//     int position = 5;
//     int section, next;
//     float weight;
//     wd.convertRankToSection(position, &section, &next, &weight);
//     assert(weight == 1.0f);
//     assert(section == next);
//     // Get the sum of all weights, which determine the number
//     // of different random numbers we need to test.
//     num_weights = wd.m_summed_weights_for_rank[section].back();
//     std::vector<int> count(2*POWERUP_LAST);
//     for (int i = 0; i<num_weights; i++)
//     {
//         unsigned int n;
//         int powerup = powerup_manager->getRandomPowerup(position, &n, i);
//         if(n==1)
//             count[powerup-1]++;
//         else
//             count[powerup+POWERUP_LAST-POWERUP_FIRST]++;
//     }

//     // Now make sure we reproduce the original weight distribution.
//     for(unsigned int i=0; i<wd.m_weights_for_section[section].size(); i++)
//     {
//         assert(count[i] == wd.m_weights_for_section[section][i]);
//     }
}   // unitTesting
