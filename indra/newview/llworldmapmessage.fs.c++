// potential workaround for hop://grid:port/Partial/x/y/z resolution
// 2024.04.30 humbletim

// notes:
//   - exact MapNameRequests are sent flagless here (not using LAYER_FLAG)
//     - this is to avoid triggering OpenSim code paths that modify result names
//     - only affects LLWorldMapMessage->sendNamedRegionRequest(name, callback, ...)
//     - in particular where a grid hosts overlapping names, hop Region matching may work better


#include "llworldmapmessage.fs.h"

#include <regex>
#include <string>

#include "llagent.h"
#include "llcommon.h"
#include "llsingleton.h"
#include "llworldmap.h" // grid_to_region_handle
#include "llworldmapmessage.h"
#include "message.h"

#include "llnotificationsutil.h"

#define htxhop_log(format, ...)                                                                                  \
    {                                                                                                            \
        fprintf(stderr, format "\n", __VA_ARGS__);                                                               \
        fflush(stderr);                                                                                          \
        LLNotificationsUtil::add("ChatSystemMessageTip", LLSD().with("MESSAGE", llformat(format, __VA_ARGS__))); \
        LL_WARNS("GridManager") << llformat(format, __VA_ARGS__) << LL_ENDL;                                     \
    }

#include "llviewercontrol.h"

inline std::string extract_region(std::string const& s)
{
    static auto const& patterns = {
        std::regex { R"(/ ([^/:=]+)$)" }, // TODO: figure out where the spec lives for hop "slash space" embedding...
        std::regex { R"(([^/:=]+)$)" }, // TODO: figure out where the spec lives for hop "grid:port:region" embedding...
    };
    std::smatch match_results;
    std::string ls { s };
    LLStringUtil::toLower(ls);
    for (auto const& pattern : patterns) {
        if (std::regex_search(ls, match_results, pattern)) {
            return match_results[1].str();
        }
    }
    return {};
}
// int main() {
// 	for (const auto& s : {
// 		"http://hg.osgrid.org:80/ Vue North",
// 		"hop://hg.osgrid.org:80/ Vue North",
// 		"hg.osgrid.org:80/ Vue North",
// 		"hg.osgrid.org:80:Vue North",
// 		"hg.osgrid.org:80/Vue North",
// 	}) fprintf(stderr, "'%s' = '%s'\n", s, extract_region(s).c_str());fflush(stderr);
// 	return 0;
// }

static LLCachedControl<S32> htxhop_flags(gSavedSettings, "htxhop_flags", 0, "default: 0\nLAYER_FLAG: 2\n");
#define htxhop_debug_setting_disable (+htxhop_flags == 2)

// helper to encapsulate Region Map Block responses
struct _MapBlock {
    S32 index {};
    U16 x_regions {}, y_regions {}, x_size { REGION_WIDTH_UNITS }, y_size { REGION_WIDTH_UNITS };
    std::string name {};
    U8 accesscode {};
    U32 region_flags {};
    LLUUID image_id {};

    inline U32 x_world() const { return (U32)(x_regions)*REGION_WIDTH_UNITS; }
    inline U32 y_world() const { return (U32)(y_regions)*REGION_WIDTH_UNITS; }
    inline U64 region_handle() const { return grid_to_region_handle(x_regions, y_regions); }

    // see: LLWorldMapMessage::processMapBlockReply
    _MapBlock(LLMessageSystem* msg, S32 block)
        : index(block)
    {
        msg->getU16Fast(_PREHASH_Data, _PREHASH_X, x_regions, block);
        msg->getU16Fast(_PREHASH_Data, _PREHASH_Y, y_regions, block);
        msg->getStringFast(_PREHASH_Data, _PREHASH_Name, name, block);
        msg->getU8Fast(_PREHASH_Data, _PREHASH_Access, accesscode, block);
        msg->getU32Fast(_PREHASH_Data, _PREHASH_RegionFlags, region_flags, block);
        //		msg->getU8Fast(_PREHASH_Data, _PREHASH_WaterHeight, water_height, block);
        //		msg->getU8Fast(_PREHASH_Data, _PREHASH_Agents, agents, block);
        msg->getUUIDFast(_PREHASH_Data, _PREHASH_MapImageID, image_id, block);
        // <FS:CR> Aurora Sim
        if (msg->getNumberOfBlocksFast(_PREHASH_Size) > 0) {
            msg->getU16Fast(_PREHASH_Size, _PREHASH_SizeX, x_size, block);
            msg->getU16Fast(_PREHASH_Size, _PREHASH_SizeY, y_size, block);
        }
        if (x_size == 0 || (x_size % 16) != 0 || (y_size % 16) != 0) {
            x_size = 256;
            y_size = 256;
        }
        // </FS:CR> Aurora Sim
    }
};

#define EXACT_FLAG 0x00000000

// see: LLWorldMapMessage::sendNamedRegionRequest
void _hypergrid_sendMapNameRequest(std::string const& region_name, U32 flags)
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MapNameRequest);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addU32Fast(_PREHASH_Flags, flags);
    msg->addU32Fast(_PREHASH_EstateID, 0); // Filled in on sim
    msg->addBOOLFast(_PREHASH_Godlike, FALSE); // Filled in on sim
    msg->nextBlockFast(_PREHASH_NameData);
    msg->addStringFast(_PREHASH_Name, region_name);
    gAgent.sendReliableMessage();
}

using url_callback_t = std::function<void(uint64_t region_handle, const std::string& url, const LLUUID& snapshot_id, bool teleport)>;
struct _AdoptedRegionNameQuery {
    std::string key;
    std::string region_name;
    url_callback_t arbitrary_callback;
    std::string arbitrary_slurl;
    bool arbitrary_teleport;
};
// map extracted region names => pending query entries
static std::map<std::string, _AdoptedRegionNameQuery> _region_name_queries;

bool hypergrid_sendExactNamedRegionRequest(std::string const& region_name, url_callback_t const& callback, std::string const& callback_url,
    bool teleport)
{
    if (htxhop_debug_setting_disable || !callback)
        return false;
    auto key = extract_region(region_name);
    if (key.empty())
        return false;
    _region_name_queries[key] = { key, region_name, callback, callback_url, teleport };
    htxhop_log("[xxHTxx] Send Region Name '%s' (key: %s)", region_name.c_str(), key.c_str());
    _hypergrid_sendMapNameRequest(region_name, EXACT_FLAG);
    return true;
}

bool hypergrid_processExactNamedRegionResponse(LLMessageSystem* msg, U32 agent_flags)
{
    if (htxhop_debug_setting_disable || !msg)
        return false;
    // NOTE: we assume only agent_flags have been read from msg so far
    S32 num_blocks = msg->getNumberOfBlocksFast(_PREHASH_Data);

    std::vector<_MapBlock> blocks;
    blocks.reserve(num_blocks);
    for (int b = 0; b < num_blocks; b++) {
        blocks.emplace_back(msg, b);
    }
    /* EXPIRE:>=2024-05-10 */ for (auto const& _block : blocks)
        htxhop_log("#%02d key='%s' block.name='%s' block.region_handle=%llu", _block.index, extract_region(_block.name).c_str(),
            _block.name.c_str(), _block.region_handle());

    // special case: handle singular result w/empty name tho valid region handle AND singular pending query as a match
    // (might be that a landing area / redirect hop URL is coming back: "^hop://grid:port/$", which extract_region's into "")
    bool solo_result = blocks.size() == 2 && blocks[0].region_handle() && extract_region(blocks[0].name).empty() && !blocks[1].region_handle();
    if (solo_result && _region_name_queries.size() == 1) {
        htxhop_log("applying first block as redirect; region_handle: %llu", blocks[0].region_handle());
        blocks[0].name = _region_name_queries.begin()->second.region_name;
    }

    for (auto const& _block : blocks) {
        auto key = extract_region(_block.name);
        if (key.empty())
            continue;
        auto idx = _region_name_queries.find(key);
        if (idx == _region_name_queries.end())
            continue;
        auto pending = idx->second;
        htxhop_log("[xxHTxx] Recv Region Name '%s' (key: %s) block.name='%s' block.region_handle=%llu)", pending.region_name.c_str(),
            pending.key.c_str(), _block.name.c_str(), _block.region_handle());
        _region_name_queries.erase(idx);
        pending.arbitrary_callback(_block.region_handle(), pending.arbitrary_slurl, _block.image_id, pending.arbitrary_teleport);
        return true;
    }
    return false;
}
