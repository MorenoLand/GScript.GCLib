#include "TClientPacketDispatch.h"

#include "IEnums.h"
#include "TClientPlayerProps.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <string>

namespace {
using JsonFieldMap = std::unordered_map<std::string, std::string>;

static std::string to_string_lossy(const std::vector<uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

static std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x7f) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 0x0f];
            else out << c;
        }
    }
    return out.str();
}

static const char* prop_name(int prop) {
    switch (prop) {
    case TC_PLPROP_NICKNAME: return "nickname";
    case TC_PLPROP_MAXPOWER: return "max_power";
    case TC_PLPROP_CURPOWER: return "current_power";
    case TC_PLPROP_RUPEESCOUNT: return "rupees";
    case TC_PLPROP_ARROWSCOUNT: return "arrows";
    case TC_PLPROP_BOMBSCOUNT: return "bombs";
    case TC_PLPROP_GLOVEPOWER: return "glove_power";
    case TC_PLPROP_BOMBPOWER: return "bomb_power";
    case TC_PLPROP_SWORDPOWER: return "sword";
    case TC_PLPROP_SHIELDPOWER: return "shield";
    case TC_PLPROP_GANI: return "gani";
    case TC_PLPROP_HEADIMAGE: return "head_image";
    case TC_PLPROP_CURCHAT: return "chat";
    case TC_PLPROP_COLORS: return "colors";
    case TC_PLPROP_ID: return "id";
    case TC_PLPROP_X: return "x";
    case TC_PLPROP_Y: return "y";
    case TC_PLPROP_DIRECTION: return "direction";
    case TC_PLPROP_STATUS: return "status";
    case TC_PLPROP_CARRYSPRITE: return "carry_sprite";
    case TC_PLPROP_CURLEVEL: return "level";
    case TC_PLPROP_HORSEGIF: return "horse_image";
    case TC_PLPROP_HORSEBUSHES: return "horse_bushes";
    case TC_PLPROP_EFFECTCOLORS: return "effect_colors";
    case TC_PLPROP_CARRYNPC: return "carry_npc";
    case TC_PLPROP_APCOUNTER: return "ap_counter";
    case TC_PLPROP_MAGICPOINTS: return "magic_points";
    case TC_PLPROP_KILLSCOUNT: return "kills";
    case TC_PLPROP_DEATHSCOUNT: return "deaths";
    case TC_PLPROP_ONLINESECS: return "online_seconds";
    case TC_PLPROP_IPADDR: return "ip_address";
    case TC_PLPROP_UDPPORT: return "udp_port";
    case TC_PLPROP_ALIGNMENT: return "alignment";
    case TC_PLPROP_ADDITFLAGS: return "additional_flags";
    case TC_PLPROP_ACCOUNTNAME: return "account";
    case TC_PLPROP_BODYIMAGE: return "body_image";
    case TC_PLPROP_RATING: return "rating";
    case TC_PLPROP_ATTACHNPC: return "attached_npc";
    case TC_PLPROP_GMAPLEVELX: return "gmap_level_x";
    case TC_PLPROP_GMAPLEVELY: return "gmap_level_y";
    case TC_PLPROP_Z: return "z";
    case TC_PLPROP_JOINLEAVELVL: return "join_leave_level";
    case TC_PLPROP_PCONNECTED: return "connected";
    case TC_PLPROP_PLANGUAGE: return "language";
    case TC_PLPROP_PSTATUSMSG: return "status_message";
    case TC_PLPROP_OSTYPE: return "os_type";
    case TC_PLPROP_TEXTCODEPAGE: return "text_codepage";
    case TC_PLPROP_UNKNOWN77: return "unknown77";
    case TC_PLPROP_X2: return "precise_x";
    case TC_PLPROP_Y2: return "precise_y";
    case TC_PLPROP_Z2: return "precise_z";
    case TC_PLPROP_UNKNOWN81: return "unknown81";
    case TC_PLPROP_COMMUNITYNAME: return "community";
    default:
        if (prop >= TC_PLPROP_GATTRIB1 && prop <= TC_PLPROP_GATTRIB1 + 4) return "attribute";
        if (prop >= TC_PLPROP_GATTRIB6 && prop <= TC_PLPROP_GATTRIB6 + 3) return "attribute";
        if (prop >= TC_PLPROP_GATTRIB10 && prop <= TC_PLPROP_GATTRIB10 + 20) return "attribute";
        return nullptr;
    }
}

static int attribute_index(int prop) {
    if (prop >= TC_PLPROP_GATTRIB1 && prop <= TC_PLPROP_GATTRIB1 + 4) return prop - TC_PLPROP_GATTRIB1 + 1;
    if (prop >= TC_PLPROP_GATTRIB6 && prop <= TC_PLPROP_GATTRIB6 + 3) return prop - TC_PLPROP_GATTRIB6 + 6;
    if (prop >= TC_PLPROP_GATTRIB10 && prop <= TC_PLPROP_GATTRIB10 + 20) return prop - TC_PLPROP_GATTRIB10 + 10;
    return 0;
}

static bool is_string_prop(int prop) {
    if (prop == TC_PLPROP_NICKNAME || prop == TC_PLPROP_GANI || prop == TC_PLPROP_CURCHAT ||
        prop == TC_PLPROP_CURLEVEL || prop == TC_PLPROP_HORSEGIF || prop == TC_PLPROP_ACCOUNTNAME ||
        prop == TC_PLPROP_BODYIMAGE || prop == TC_PLPROP_PLANGUAGE || prop == TC_PLPROP_PSTATUSMSG ||
        prop == TC_PLPROP_OSTYPE || prop == TC_PLPROP_COMMUNITYNAME ||
        attribute_index(prop)) {
        return true;
    }
    return false;
}

static void write_prop_key(std::ostringstream& json, int prop) {
    int attr = attribute_index(prop);
    if (attr) {
        json << "\"attr" << attr << "\"";
        return;
    }
    const char* name = prop_name(prop);
    if (name) json << "\"" << name << "\"";
    else json << "\"p" << prop << "\"";
}

static std::string prop_key_string(int prop) {
    int attr = attribute_index(prop);
    if (attr) return "attr" + std::to_string(attr);
    const char* name = prop_name(prop);
    if (name) return name;
    return "p" + std::to_string(prop);
}

static std::string decode_head_image(TPacketReader& r) {
    if (!r.has()) return "";
    int value = r.gchar();
    if (value < 100) return "head" + std::to_string(value) + ".png";
    return r.str(static_cast<size_t>(value - 100));
}

static int decode_signed14(TPacketReader& r) {
    int raw = r.gshort();
    int value = raw >> 1;
    return (raw & 1) ? -value : value;
}

static std::string bytes_hex(const uint8_t* data, size_t count) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0f]);
    }
    return out;
}

static std::string read_ip_address(TPacketReader& r) {
    int a = r.has() ? r.gchar() : 0;
    int b = r.has() ? r.gchar() : 0;
    int c = r.has() ? r.gchar() : 0;
    int d = r.has() ? r.gchar() : 0;
    int e = r.has() ? r.gchar() : 0;
    uint32_t value = static_cast<uint32_t>(a) * 0x10000000U +
                     static_cast<uint32_t>(b) * 0x200000U +
                     static_cast<uint32_t>(c) * 0x4000U +
                     static_cast<uint32_t>(d) * 0x80U +
                     static_cast<uint32_t>(e);
    std::ostringstream out;
    out << ((value >> 24) & 0xff) << "." << ((value >> 16) & 0xff) << "."
        << ((value >> 8) & 0xff) << "." << (value & 0xff);
    return out.str();
}

static const char* packet_name(int packet_id) {
    switch (packet_id) {
    case PLO_LEVELBOARD: return "PLO_LEVELBOARD";
    case PLO_LEVELLINK: return "PLO_LEVELLINK";
    case PLO_BADDYPROPS: return "PLO_BADDYPROPS";
    case PLO_NPCPROPS: return "PLO_NPCPROPS";
    case PLO_LEVELCHEST: return "PLO_LEVELCHEST";
    case PLO_LEVELSIGN: return "PLO_LEVELSIGN";
    case PLO_LEVELNAME: return "PLO_LEVELNAME";
    case PLO_BOARDMODIFY: return "PLO_BOARDMODIFY";
    case PLO_OTHERPLPROPS: return "PLO_OTHERPLPROPS";
    case PLO_PLAYERPROPS: return "PLO_PLAYERPROPS";
    case PLO_ISLEADER: return "PLO_ISLEADER";
    case PLO_BOMBADD: return "PLO_BOMBADD";
    case PLO_BOMBDEL: return "PLO_BOMBDEL";
    case PLO_TOALL: return "PLO_TOALL";
    case PLO_PLAYERWARP: return "PLO_PLAYERWARP";
    case PLO_WARPFAILED: return "PLO_WARPFAILED";
    case PLO_DISCMESSAGE: return "PLO_DISCMESSAGE";
    case PLO_HORSEADD: return "PLO_HORSEADD";
    case PLO_HORSEDEL: return "PLO_HORSEDEL";
    case PLO_ARROWADD: return "PLO_ARROWADD";
    case PLO_FIRESPY: return "PLO_FIRESPY";
    case PLO_THROWCARRIED: return "PLO_THROWCARRIED";
    case PLO_ITEMADD: return "PLO_ITEMADD";
    case PLO_ITEMDEL: return "PLO_ITEMDEL";
    case PLO_NPCMOVED: return "PLO_NPCMOVED";
    case PLO_SIGNATURE: return "PLO_SIGNATURE";
    case PLO_NPCACTION: return "PLO_NPCACTION";
    case PLO_BADDYHURT: return "PLO_BADDYHURT";
    case PLO_FLAGSET: return "PLO_FLAGSET";
    case PLO_NPCDEL: return "PLO_NPCDEL";
    case PLO_FILESENDFAILED: return "PLO_FILESENDFAILED";
    case PLO_FLAGDEL: return "PLO_FLAGDEL";
    case PLO_SHOWIMG: return "PLO_SHOWIMG";
    case PLO_NPCWEAPONADD: return "PLO_NPCWEAPONADD";
    case PLO_NPCWEAPONDEL: return "PLO_NPCWEAPONDEL";
    case PLO_RC_ADMINMESSAGE: return "PLO_RC_ADMINMESSAGE";
    case PLO_EXPLOSION: return "PLO_EXPLOSION";
    case PLO_PRIVATEMESSAGE: return "PLO_PRIVATEMESSAGE";
    case PLO_PUSHAWAY: return "PLO_PUSHAWAY";
    case PLO_LEVELMODTIME: return "PLO_LEVELMODTIME";
    case PLO_HURTPLAYER: return "PLO_HURTPLAYER";
    case PLO_STARTMESSAGE: return "PLO_STARTMESSAGE";
    case PLO_NEWWORLDTIME: return "PLO_NEWWORLDTIME";
    case PLO_HASNPCSERVER: return "PLO_HASNPCSERVER";
    case PLO_DEFAULTWEAPON: return "PLO_DEFAULTWEAPON";
    case PLO_FILEUPTODATE: return "PLO_FILEUPTODATE";
    case PLO_HITOBJECTS: return "PLO_HITOBJECTS";
    case PLO_STAFFGUILDS: return "PLO_STAFFGUILDS";
    case PLO_TRIGGERACTION: return "PLO_TRIGGERACTION";
    case PLO_PLAYERWARP2: return "PLO_PLAYERWARP2";
    case PLO_RC_ACCOUNTADD: return "PLO_RC_ACCOUNTADD";
    case PLO_RC_ACCOUNTSTATUS: return "PLO_RC_ACCOUNTSTATUS";
    case PLO_RC_ACCOUNTNAME: return "PLO_RC_ACCOUNTNAME";
    case PLO_RC_ACCOUNTDEL: return "PLO_RC_ACCOUNTDEL";
    case PLO_RC_ACCOUNTPROPS: return "PLO_RC_ACCOUNTPROPS";
    case PLO_ADDPLAYER: return "PLO_ADDPLAYER";
    case PLO_DELPLAYER: return "PLO_DELPLAYER";
    case PLO_RC_ACCOUNTPROPSGET: return "PLO_RC_ACCOUNTPROPSGET";
    case PLO_RC_ACCOUNTCHANGE: return "PLO_RC_ACCOUNTCHANGE";
    case PLO_RC_PLAYERPROPSCHANGE: return "PLO_RC_PLAYERPROPSCHANGE";
    case PLO_UNKNOWN60: return "PLO_UNKNOWN60";
    case PLO_RC_SERVERFLAGSGET: return "PLO_RC_SERVERFLAGSGET";
    case PLO_RC_PLAYERRIGHTSGET: return "PLO_RC_PLAYERRIGHTSGET";
    case PLO_RC_PLAYERCOMMENTSGET: return "PLO_RC_PLAYERCOMMENTSGET";
    case PLO_RC_PLAYERBANGET: return "PLO_RC_PLAYERBANGET";
    case PLO_RC_FILEBROWSER_DIRLIST: return "PLO_RC_FILEBROWSER_DIRLIST";
    case PLO_RC_FILEBROWSER_DIR: return "PLO_RC_FILEBROWSER_DIR";
    case PLO_RC_FILEBROWSER_MESSAGE: return "PLO_RC_FILEBROWSER_MESSAGE";
    case PLO_LARGEFILESTART: return "PLO_LARGEFILESTART";
    case PLO_LARGEFILEEND: return "PLO_LARGEFILEEND";
    case PLO_RC_ACCOUNTLISTGET: return "PLO_RC_ACCOUNTLISTGET";
    case PLO_RC_PLAYERPROPS: return "PLO_RC_PLAYERPROPS";
    case PLO_RC_PLAYERPROPSGET: return "PLO_RC_PLAYERPROPSGET";
    case PLO_RC_ACCOUNTGET: return "PLO_RC_ACCOUNTGET";
    case PLO_RC_CHAT: return "PLO_RC_CHAT";
    case PLO_PROFILE: return "PLO_PROFILE";
    case PLO_RC_SERVEROPTIONSGET: return "PLO_RC_SERVEROPTIONSGET";
    case PLO_RC_FOLDERCONFIGGET: return "PLO_RC_FOLDERCONFIGGET";
    case PLO_NC_CONTROL: return "PLO_NC_CONTROL";
    case PLO_NPCSERVERADDR: return "PLO_NPCSERVERADDR";
    case PLO_NC_LEVELLIST: return "PLO_NC_LEVELLIST";
    case PLO_UNKNOWN81: return "PLO_UNKNOWN81";
    case PLO_SERVERTEXT: return "PLO_SERVERTEXT";
    case PLO_UNKNOWN83: return "PLO_UNKNOWN83";
    case PLO_LARGEFILESIZE: return "PLO_LARGEFILESIZE";
    case PLO_RAWDATA: return "PLO_RAWDATA";
    case PLO_BOARDPACKET: return "PLO_BOARDPACKET";
    case PLO_FILE: return "PLO_FILE";
    case PLO_RC_MAXUPLOADFILESIZE: return "PLO_RC_MAXUPLOADFILESIZE";
    case PLO_UNKNOWN104: return "PLO_UNKNOWN104";
    case PLO_UPDATEPACKAGESIZE: return "PLO_UPDATEPACKAGESIZE";
    case PLO_UPDATEPACKAGEDONE: return "PLO_UPDATEPACKAGEDONE";
    case PLO_BOARDLAYER: return "PLO_BOARDLAYER";
    case PLO_UNKNOWN109: return "PLO_UNKNOWN109";
    case PLO_UNKNOWN111: return "PLO_UNKNOWN111";
    case PLO_UNKNOWN124: return "PLO_UNKNOWN124";
    case PLO_NPCBYTECODE: return "PLO_NPCBYTECODE";
    case PLO_UNKNOWN132: return "PLO_UNKNOWN132";
    case PLO_UNKNOWN133: return "PLO_UNKNOWN133";
    case PLO_GANISCRIPT: return "PLO_GANISCRIPT";
    case PLO_NPCWEAPONSCRIPT: return "PLO_NPCWEAPONSCRIPT";
    case PLO_NPCDEL2: return "PLO_NPCDEL2";
    case PLO_HIDENPCS: return "PLO_HIDENPCS";
    case PLO_SAY2: return "PLO_SAY2";
    case PLO_FREEZEPLAYER2: return "PLO_FREEZEPLAYER2";
    case PLO_UNFREEZEPLAYER: return "PLO_UNFREEZEPLAYER";
    case PLO_SETACTIVELEVEL: return "PLO_SETACTIVELEVEL";
    case PLO_NC_NPCATTRIBUTES: return "PLO_NC_NPCATTRIBUTES";
    case PLO_NC_NPCADD: return "PLO_NC_NPCADD";
    case PLO_NC_NPCDELETE: return "PLO_NC_NPCDELETE";
    case PLO_NC_NPCSCRIPT: return "PLO_NC_NPCSCRIPT";
    case PLO_NC_NPCFLAGS: return "PLO_NC_NPCFLAGS";
    case PLO_NC_CLASSGET: return "PLO_NC_CLASSGET";
    case PLO_NC_CLASSADD: return "PLO_NC_CLASSADD";
    case PLO_NC_LEVELDUMP: return "PLO_NC_LEVELDUMP";
    case PLO_MOVE: return "PLO_MOVE";
    case PLO_UNKNOWN166: return "PLO_UNKNOWN166";
    case PLO_NC_WEAPONLISTGET: return "PLO_NC_WEAPONLISTGET";
    case PLO_GHOSTMODE: return "PLO_GHOSTMODE";
    case PLO_UNKNOWN169: return "PLO_UNKNOWN169";
    case PLO_BIGMAP: return "PLO_BIGMAP";
    case PLO_MINIMAP: return "PLO_MINIMAP";
    case PLO_GHOSTTEXT: return "PLO_GHOSTTEXT";
    case PLO_GHOSTICON: return "PLO_GHOSTICON";
    case PLO_SHOOT: return "PLO_SHOOT";
    case PLO_FULLSTOP: return "PLO_FULLSTOP";
    case PLO_FULLSTOP2: return "PLO_FULLSTOP2";
    case PLO_SERVERWARP: return "PLO_SERVERWARP";
    case PLO_RPGWINDOW: return "PLO_RPGWINDOW";
    case PLO_STATUSLIST: return "PLO_STATUSLIST";
    case PLO_UNKNOWN181: return "PLO_UNKNOWN181";
    case PLO_LISTPROCESSES: return "PLO_LISTPROCESSES";
    case PLO_UNKNOWN183: return "PLO_UNKNOWN183";
    case PLO_UNKNOWN184: return "PLO_UNKNOWN184";
    case PLO_UNKNOWN185: return "PLO_UNKNOWN185";
    case PLO_UNKNOWN186: return "PLO_UNKNOWN186";
    case PLO_UPDATEPACKAGEISUPDATED: return "PLO_UPDATEPACKAGEISUPDATED";
    case PLO_NC_CLASSDELETE: return "PLO_NC_CLASSDELETE";
    case PLO_UNKNOWN168: return "PLO_UNKNOWN168";
    case PLO_UNKNOWN190: return "PLO_UNKNOWN190";
    case PLO_SHOOT2: return "PLO_SHOOT2";
    case PLO_NC_WEAPONGET: return "PLO_NC_WEAPONGET";
    case PLO_UNKNOWN193: return "PLO_UNKNOWN193";
    case PLO_CLEARWEAPONS: return "PLO_CLEARWEAPONS";
    case PLO_MOVE2: return "PLO_MOVE2";
    case PLO_UNKNOWN195: return "PLO_UNKNOWN195";
    case PLO_UNKNOWN197: return "PLO_UNKNOWN197";
    case PLO_UNKNOWN198: return "PLO_UNKNOWN198";
    default: return "UNKNOWN";
    }
}

static std::string json_string_field(const char* key, const std::string& value) {
    std::ostringstream json;
    json << "{\"" << key << "\":\"" << json_escape(value) << "\"}";
    return json.str();
}

static std::string json_raw_event(const std::vector<uint8_t>& payload) {
    std::string text = to_string_lossy(payload);
    std::ostringstream json;
    json << "{\"length\":" << payload.size()
         << ",\"text\":\"" << json_escape(text)
         << "\",\"raw_hex\":\"" << bytes_hex(payload.data(), payload.size()) << "\"}";
    return json.str();
}

static std::string json_raw_event(const std::vector<uint8_t>& payload, const char* category) {
    std::string text = to_string_lossy(payload);
    std::ostringstream json;
    json << "{\"category\":\"" << category
         << "\",\"length\":" << payload.size()
         << ",\"text\":\"" << json_escape(text)
         << "\",\"raw_hex\":\"" << bytes_hex(payload.data(), payload.size()) << "\"}";
    return json.str();
}

static void emit_packet_event(const CallbackPacketEvent& cb, int packet_id, const std::string& event_json) {
    if (!cb.cb) return;
    cb.cb(packet_id, packet_name(packet_id), event_json.c_str(), cb.ud);
}

static std::string percent_encode_path_segment(const std::string& segment) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(segment.size());
    for (unsigned char c : segment) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        out.push_back('%');
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0f]);
    }
    return out.empty() ? "_" : out;
}

static std::filesystem::path safe_resource_relative_path(const std::string& name) {
    std::filesystem::path out;
    std::string segment;
    auto flush_segment = [&]() {
        if (!segment.empty() && segment != "." && segment != "..") out /= percent_encode_path_segment(segment);
        segment.clear();
    };
    for (char c : name) {
        if (c == '/' || c == '\\') flush_segment();
        else segment.push_back(c);
    }
    flush_segment();
    return out.empty() ? std::filesystem::path("unnamed") : out;
}

static std::string path_string_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.u8string();
#else
    return path.string();
#endif
}

static std::filesystem::path resource_dump_path(const std::string& root, const std::string& resource_type, const std::string& name) {
    return std::filesystem::path(root) / percent_encode_path_segment(resource_type) / safe_resource_relative_path(name);
}

static void append_resource_manifest(const std::string& root, const std::string& resource_type, const std::string& name, int packet_id, size_t length, const std::filesystem::path& target) {
    if (root.empty()) return;
    try {
        std::filesystem::path manifest = std::filesystem::path(root) / "_manifest.jsonl";
        std::ofstream out(manifest, std::ios::binary | std::ios::app);
        if (!out) return;
        out << "{\"packet_id\":" << packet_id
            << ",\"packet_name\":\"" << packet_name(packet_id)
            << "\",\"type\":\"" << json_escape(resource_type)
            << "\",\"name\":\"" << json_escape(name)
            << "\",\"length\":" << length
            << ",\"path\":\"" << json_escape(path_string_utf8(target))
            << "\"}\n";
    } catch (...) {
    }
}

static void dump_resource_file(const std::string& root, const std::string& resource_type, const std::string& name, const uint8_t* data, size_t length, int packet_id) {
    if (root.empty() || (!data && length > 0)) return;
    try {
        std::filesystem::path target = resource_dump_path(root, resource_type, name);
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary);
        if (out) {
            if (length > 0) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
            append_resource_manifest(root, resource_type, name, packet_id, length, target);
        }
    } catch (...) {
    }
}

static bool should_dump_resource_type(const std::unordered_set<std::string>& types, const std::string& resource_type) {
    return types.empty() || types.find(resource_type) != types.end();
}

static void emit_resource(const CallbackResource& cb, const std::string& dump_dir, const std::unordered_set<std::string>& dump_types, int packet_id, const std::string& resource_type, const std::string& name, const std::vector<uint8_t>& data) {
    const uint8_t* bytes = data.empty() ? nullptr : data.data();
    if (should_dump_resource_type(dump_types, resource_type)) {
        dump_resource_file(dump_dir, resource_type, name, bytes, data.size(), packet_id);
    }
    if (cb.cb) cb.cb(resource_type.c_str(), name.c_str(), bytes, static_cast<int>(data.size()), packet_id, cb.ud);
}

static bool is_level_resource_name(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string legacy_level_ext = std::string() + 'g' + 'r' + 'a' + 'a' + 'l';
    return ext == "nw" || ext == legacy_level_ext || ext == "gmap";
}

static std::string parse_props_json(const std::vector<uint8_t>& data, size_t start_pos = 0, JsonFieldMap* fields = nullptr);

static std::string parse_gstring_pair_plus_rest_json(const std::vector<uint8_t>& payload, const char* first_name, const char* second_name, const char* rest_name) {
    TPacketReader r(payload);
    std::string first = r.gstring();
    std::string second = r.gstring();
    std::vector<uint8_t> rest = r.remaining();
    std::ostringstream json;
    json << "{\"" << first_name << "\":\"" << json_escape(first)
         << "\",\"" << second_name << "\":\"" << json_escape(second)
         << "\",\"" << rest_name << "\":\"" << json_escape(to_string_lossy(rest))
         << "\",\"data_length\":" << rest.size() << "}";
    return json.str();
}

static std::vector<std::string> parse_comma_text(const std::string& text) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                current.push_back(c);
            }
        } else if (c == ',') {
            fields.push_back(current);
            current.clear();
        } else if (c == '"') {
            quoted = true;
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

static std::string parse_npc_weapon_add_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    std::string name = r.gstring();
    int flags = r.has() ? r.gchar() : 0;
    std::string image = r.has() ? r.gstring() : "";
    int unknown = r.has() ? r.gchar() : 0;
    std::string classes = r.has() ? r.gstring() : "";
    std::ostringstream json;
    json << "{\"name\":\"" << json_escape(name)
         << "\",\"flags\":" << flags
         << ",\"image\":\"" << json_escape(image)
         << "\",\"unknown\":" << unknown
         << ",\"classes\":\"" << json_escape(classes)
         << "\"}";
    return json.str();
}

static std::string parse_cache_metadata_json(const std::vector<uint8_t>& payload) {
    std::vector<std::string> fields = parse_comma_text(to_string_lossy(payload));
    std::ostringstream json;
    json << "{\"field_count\":" << fields.size();
    const char* names[] = {"type", "name", "version", "checksum", "key"};
    for (size_t i = 0; i < fields.size(); ++i) {
        json << ",\"";
        if (i < 5) json << names[i];
        else json << "field" << i;
        json << "\":\"" << json_escape(fields[i]) << "\"";
    }
    json << "}";
    return json.str();
}

static double half_byte(TPacketReader& r) {
    return r.has() ? r.gchar() / 2.0 : 0.0;
}

static std::string parse_level_chest_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    bool open = r.has() ? r.byte() > 0x20 : false;
    int x = r.has() ? r.gchar() : 0;
    int y = r.has() ? r.gchar() : 0;
    int item = r.has() ? r.gchar() : -1;
    int sign = r.has() ? r.gchar() : -1;
    std::ostringstream json;
    json << "{\"open\":" << (open ? "true" : "false")
         << ",\"x\":" << x << ",\"y\":" << y
         << ",\"item_index\":" << item << ",\"sign_index\":" << sign << "}";
    return json.str();
}

static std::string parse_board_modify_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int layer = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    size_t tile_start = 4;
    if (!payload.empty()) {
        int first = static_cast<int>(payload[0]) - 0x20;
        if (first < 0x40) {
            x = first;
            y = payload.size() > 1 ? static_cast<int>(payload[1]) - 0x20 : 0;
            width = payload.size() > 2 ? static_cast<int>(payload[2]) - 0x20 : 0;
            height = payload.size() > 3 ? static_cast<int>(payload[3]) - 0x20 : 0;
        } else {
            layer = first - 0x40;
            x = payload.size() > 1 ? static_cast<int>(payload[1]) - 0x20 : 0;
            y = payload.size() > 2 ? static_cast<int>(payload[2]) - 0x20 : 0;
            width = payload.size() > 3 ? static_cast<int>(payload[3]) - 0x20 : 0;
            height = payload.size() > 4 ? static_cast<int>(payload[4]) - 0x20 : 0;
            tile_start = 5;
        }
    }
    int tile_count = width > 0 && height > 0 ? width * height : 0;
    bool complete = tile_start + static_cast<size_t>(tile_count * 2) <= payload.size();
    std::ostringstream json;
    json << "{\"layer\":" << layer << ",\"x\":" << x << ",\"y\":" << y
         << ",\"width\":" << width << ",\"height\":" << height
         << ",\"tile_count\":" << tile_count << ",\"complete\":" << (complete ? "true" : "false") << "}";
    return json.str();
}

static std::string parse_board_summary_json(const std::vector<uint8_t>& payload, int layer) {
    size_t tile_pairs = payload.size() / 2;
    if (tile_pairs > 4096) tile_pairs = 4096;
    std::ostringstream json;
    json << "{\"layer\":" << layer
         << ",\"tile_count\":" << tile_pairs
         << ",\"raw_length\":" << payload.size()
         << ",\"complete\":" << (tile_pairs >= 4096 ? "true" : "false") << "}";
    return json.str();
}

static std::string parse_resource_summary_json(const std::vector<uint8_t>& payload, const char* resource_type) {
    std::ostringstream json;
    json << "{\"resource_type\":\"" << resource_type
         << "\",\"length\":" << payload.size() << "}";
    return json.str();
}

static std::string parse_arrow_add_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int player = r.gshort();
    double x = half_byte(r);
    double y = half_byte(r);
    int angle = r.has() ? r.gchar() : 0;
    int power = r.has() ? r.gchar() : 0;
    int type = r.has() ? static_cast<int>(r.byte()) - 0x21 : 0;
    std::ostringstream json;
    json << "{\"player_id\":" << player << ",\"x\":" << x << ",\"y\":" << y
         << ",\"angle\":" << angle << ",\"power\":" << power << ",\"type\":" << type << "}";
    return json.str();
}

static std::string parse_bomb_add_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int player = r.gshort();
    double x = half_byte(r);
    double y = half_byte(r);
    int flags = r.has() ? r.gchar() : 0;
    int power = r.has() ? r.gchar() : 0;
    std::string custom;
    if (r.has()) custom = to_string_lossy(r.remaining());
    std::ostringstream json;
    json << "{\"player_id\":" << player << ",\"x\":" << x << ",\"y\":" << y
         << ",\"flags\":" << flags << ",\"power\":" << power
         << ",\"custom\":\"" << json_escape(custom) << "\"}";
    return json.str();
}

static std::string parse_showimg_json(const std::vector<uint8_t>& payload) {
    std::ostringstream json;
    json << "{\"length\":" << payload.size()
         << ",\"text\":\"" << json_escape(to_string_lossy(payload))
         << "\",\"raw_hex\":\"" << bytes_hex(payload.data(), payload.size()) << "\"}";
    return json.str();
}

static std::string parse_xy_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    std::ostringstream json;
    json << "{\"x\":" << half_byte(r) << ",\"y\":" << half_byte(r) << "}";
    return json.str();
}

static std::string parse_extra_add_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    double x = half_byte(r);
    double y = half_byte(r);
    int item = r.has() ? r.gchar() : 0;
    int player = r.has(2) ? r.gshort() : -1;
    std::ostringstream json;
    json << "{\"x\":" << x << ",\"y\":" << y << ",\"item_index\":" << item << ",\"player_id\":" << player << "}";
    return json.str();
}

static std::string parse_player_id_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    std::ostringstream json;
    json << "{\"player_id\":" << (r.has(2) ? r.gshort() : -1) << "}";
    return json.str();
}

static std::string parse_fire_spy_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int player = r.has(2) ? r.gshort() : -1;
    int power = r.has() ? r.gchar() : 0;
    std::ostringstream json;
    json << "{\"player_id\":" << player << ",\"power\":" << power << "}";
    return json.str();
}

static std::string parse_pushaway_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    double x = r.has() ? r.gchar() / 2.0 - 1.5 : 0.0;
    double y = r.has() ? r.gchar() / 2.0 - 1.5 : 0.0;
    std::ostringstream json;
    json << "{\"x\":" << x << ",\"y\":" << y << "}";
    return json.str();
}

static std::string parse_hurt_player_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int player = r.has(2) ? r.gshort() : -1;
    double dx = r.has() ? (static_cast<int>(r.byte()) - 0x60) / 16.0 : 0.0;
    double dy = r.has() ? (static_cast<int>(r.byte()) - 0x60) / 16.0 : 0.0;
    double damage = r.has() ? r.gchar() / 2.0 : 0.0;
    int npc = r.has(3) ? r.gint3() : -1;
    std::ostringstream json;
    json << "{\"player_id\":" << player << ",\"dx\":" << dx << ",\"dy\":" << dy
         << ",\"damage\":" << damage << ",\"npc_id\":" << npc << "}";
    return json.str();
}

static std::string parse_local_npc_delete_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    std::string level = r.gstring();
    int npc = r.has(3) ? r.gint3() : -1;
    std::ostringstream json;
    json << "{\"level\":\"" << json_escape(level) << "\",\"npc_id\":" << npc << "}";
    return json.str();
}

static std::string parse_npc_removed_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int npc = r.has(3) ? r.gint3() : -1;
    double x = r.has() ? r.gchar() / 2.0 : 0.0;
    double y = r.has() ? r.gchar() / 2.0 : 0.0;
    std::string level = r.has() ? to_string_lossy(r.remaining()) : "";
    std::ostringstream json;
    json << "{\"npc_id\":" << npc << ",\"x\":" << x << ",\"y\":" << y
         << ",\"level\":\"" << json_escape(level) << "\"}";
    return json.str();
}

static std::string parse_name_data_json(const std::vector<uint8_t>& payload, const char* name_field, const char* data_field) {
    TPacketReader r(payload);
    std::string name = r.gstring();
    std::vector<uint8_t> data = r.remaining();
    std::ostringstream json;
    json << "{\"" << name_field << "\":\"" << json_escape(name)
         << "\",\"" << data_field << "\":\"" << json_escape(to_string_lossy(data))
         << "\",\"data_length\":" << data.size() << "}";
    return json.str();
}

static std::string parse_gstring_list_json(const std::vector<uint8_t>& payload, const char* list_name) {
    TPacketReader r(payload);
    std::vector<std::string> values;
    while (r.has()) values.push_back(r.gstring());
    std::ostringstream json;
    json << "{\"" << list_name << "\":[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) json << ",";
        json << "\"" << json_escape(values[i]) << "\"";
    }
    json << "],\"count\":" << values.size() << "}";
    return json.str();
}

static std::string parse_comma_fields_json(const std::vector<uint8_t>& payload, const char* text_key, const char* list_key) {
    std::string text = to_string_lossy(payload);
    std::vector<std::string> fields = parse_comma_text(text);
    std::ostringstream json;
    json << "{\"" << text_key << "\":\"" << json_escape(text) << "\",\"" << list_key << "\":[";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) json << ",";
        json << "\"" << json_escape(fields[i]) << "\"";
    }
    json << "],\"count\":" << fields.size() << "}";
    return json.str();
}

static std::string parse_gint5_json(const std::vector<uint8_t>& payload, const char* key) {
    TPacketReader r(payload);
    std::ostringstream json;
    json << "{\"" << key << "\":" << (r.has(5) ? r.gint5() : 0) << "}";
    return json.str();
}

static std::string parse_npc_server_addr_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int first = r.has() ? r.gchar() : 0;
    int second = r.has() ? r.gchar() : 0;
    std::string address = to_string_lossy(r.remaining());
    std::ostringstream json;
    json << "{\"field0\":" << first << ",\"field1\":" << second
         << ",\"address\":\"" << json_escape(address) << "\"}";
    return json.str();
}

static std::string parse_id_gstring_json(const std::vector<uint8_t>& payload, const char* data_key) {
    TPacketReader r(payload);
    int id = r.has(3) ? r.gint3() : -1;
    std::string data = r.has() ? r.gstring() : "";
    std::ostringstream json;
    json << "{\"id\":" << id << ",\"" << data_key << "\":\"" << json_escape(data) << "\"}";
    return json.str();
}

static std::string parse_nc_npc_add_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int id = r.has(3) ? r.gint3() : -1;
    int name_marker = r.has() ? r.byte() : 0;
    std::string name = r.has() ? r.gstring() : "";
    int type_marker = r.has() ? r.byte() : 0;
    std::string type = r.has() ? r.gstring() : "";
    int level_marker = r.has() ? r.byte() : 0;
    std::string level = r.has() ? r.gstring() : "";
    std::ostringstream json;
    json << "{\"id\":" << id
         << ",\"name_marker\":" << name_marker << ",\"name\":\"" << json_escape(name)
         << "\",\"type_marker\":" << type_marker << ",\"type\":\"" << json_escape(type)
         << "\",\"level_marker\":" << level_marker << ",\"level\":\"" << json_escape(level) << "\"}";
    return json.str();
}

static std::string parse_add_player_json(const std::vector<uint8_t>& payload) {
    TPacketReader r(payload);
    int id = r.has(2) ? r.gshort() : -1;
    std::string account = r.has() ? r.gstring() : "";
    std::string props = r.has() ? parse_props_json(payload, r.pos) : "{}";
    std::ostringstream json;
    json << "{\"player_id\":" << id << ",\"account\":\"" << json_escape(account)
         << "\",\"props\":" << props << "}";
    return json.str();
}

static std::string parse_props_json(const std::vector<uint8_t>& data, size_t start_pos, JsonFieldMap* fields) {
    TPacketReader r(data.data(), data.size());
    r.pos = start_pos;
    std::ostringstream json;
    json << "{";
    bool first = true;
    while (r.has()) {
        int prop = r.gchar();
        std::string key = prop_key_string(prop);
        std::ostringstream value;
        if (!first) json << ",";
        first = false;
        if (is_string_prop(prop)) {
            std::string s = r.gstring();
            value << "\"" << json_escape(s) << "\"";
        } else if (prop == TC_PLPROP_HEADIMAGE) {
            value << "\"" << json_escape(decode_head_image(r)) << "\"";
        } else if (prop == TC_PLPROP_SWORDPOWER) {
            int raw = r.has() ? r.byte() : 0;
            std::string image = r.has() ? r.gstring() : "";
            value << "{\"raw\":" << raw << ",\"image\":\"" << json_escape(image) << "\"}";
        } else if (prop == TC_PLPROP_SHIELDPOWER) {
            int raw = r.has() ? r.byte() : 0;
            std::string image = r.has() ? r.gstring() : "";
            value << "{\"raw\":" << raw << ",\"image\":\"" << json_escape(image) << "\"}";
        } else if (prop == TC_PLPROP_X2 || prop == TC_PLPROP_Y2 || prop == TC_PLPROP_Z2) {
            value << (decode_signed14(r) / 16.0);
        } else if (prop == TC_PLPROP_X || prop == TC_PLPROP_Y) {
            value << (r.gchar() / 2.0);
        } else if (prop == TC_PLPROP_Z) {
            value << (r.has() ? static_cast<int>(r.byte()) - 0x52 : 0);
        } else if (prop == TC_PLPROP_COLORS) {
            std::vector<int> colors;
            for (int i = 0; i < 5 && r.has(); ++i) colors.push_back(r.gchar());
            value << "[";
            for (size_t i = 0; i < colors.size(); ++i) {
                if (i) value << ",";
                value << colors[i];
            }
            value << "]";
        } else if (prop == TC_PLPROP_ID) {
            value << r.gshort();
        } else if (prop == TC_PLPROP_RUPEESCOUNT || prop == TC_PLPROP_CARRYNPC ||
                   prop == TC_PLPROP_UDPPORT || prop == TC_PLPROP_TEXTCODEPAGE) {
            value << r.gint3();
        } else if (prop == TC_PLPROP_ATTACHNPC) {
            int kind = r.has() ? r.gchar() : 0;
            int id = r.gint3();
            value << "{\"kind\":" << kind << ",\"id\":" << id << "}";
        } else if (prop == TC_PLPROP_RATING) {
            int start = static_cast<int>(r.pos);
            int high = r.has() ? r.gchar() : 0;
            int low = r.has() ? r.gchar() : 0;
            int frac = r.has() ? r.gchar() : 0;
            value << "{\"value\":" << ((high * 0x20) + ((low & 0x7f) >> 2))
                 << ",\"detail\":" << (((low & 3) * 0x80) + frac)
                 << ",\"raw_offset\":" << start << "}";
        } else if (prop == TC_PLPROP_IPADDR) {
            value << "\"" << read_ip_address(r) << "\"";
        } else if (prop == TC_PLPROP_PCONNECTED) {
            value << "true";
        } else if (prop == TC_PLPROP_UNKNOWN81) {
            value << (r.has() ? r.gchar() : 0);
        } else if (prop == TC_PLPROP_HORSEBUSHES || prop == TC_PLPROP_EFFECTCOLORS ||
                   prop == TC_PLPROP_APCOUNTER || prop == TC_PLPROP_MAGICPOINTS ||
                   prop == TC_PLPROP_KILLSCOUNT || prop == TC_PLPROP_DEATHSCOUNT ||
                   prop == TC_PLPROP_ONLINESECS || prop == TC_PLPROP_ALIGNMENT ||
                   prop == TC_PLPROP_ADDITFLAGS || prop == TC_PLPROP_JOINLEAVELVL ||
                   prop == TC_PLPROP_UNKNOWN77 || prop == TC_PLPROP_MAXPOWER ||
                   prop == TC_PLPROP_CURPOWER || prop == TC_PLPROP_ARROWSCOUNT ||
                   prop == TC_PLPROP_BOMBSCOUNT || prop == TC_PLPROP_GLOVEPOWER ||
                   prop == TC_PLPROP_BOMBPOWER || prop == TC_PLPROP_DIRECTION ||
                   prop == TC_PLPROP_STATUS || prop == TC_PLPROP_CARRYSPRITE ||
                   prop == TC_PLPROP_GMAPLEVELX || prop == TC_PLPROP_GMAPLEVELY) {
            value << (r.has() ? r.gchar() : 0);
        } else {
            size_t remaining = r.len - r.pos;
            value << "{\"unhandled\":true,\"remaining_hex\":\""
                 << bytes_hex(r.data + r.pos, remaining) << "\"}";
            r.pos = r.len;
        }
        std::string value_json = value.str();
        json << "\"" << key << "\":" << value_json;
        if (fields) (*fields)[key] = value_json;
    }
    json << "}";
    return json.str();
}

static int json_int_value(const std::string& value, int fallback = -1) {
    try {
        size_t used = 0;
        int parsed = std::stoi(value, &used);
        return used > 0 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

static std::string json_string_value(const std::string& value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return "";
    std::string out;
    out.reserve(value.size() - 2);
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (c != '\\' || i + 1 >= value.size() - 1) {
            out.push_back(c);
            continue;
        }
        char e = value[++i];
        switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u':
            if (i + 4 < value.size() - 1) {
                std::string hex = value.substr(i + 1, 4);
                i += 4;
                try {
                    int code = std::stoi(hex, nullptr, 16);
                    out.push_back(code >= 0 && code <= 0xff ? static_cast<char>(code) : '?');
                } catch (...) {
                    out.push_back('?');
                }
            }
            break;
        default: out.push_back(e); break;
        }
    }
    return out;
}

static std::string player_field_string(const TClientPlayerState& player, const char* key) {
    auto it = player.fields.find(key);
    return it == player.fields.end() ? "" : json_string_value(it->second);
}

static void merge_player_fields(TClient* client, int id, const JsonFieldMap& fields, bool connected = true) {
    std::lock_guard<std::mutex> lock(client->players_mutex);
    TClientPlayerState& player = client->players[id];
    player.id = id;
    player.connected = connected;
    for (const auto& kv : fields) player.fields[kv.first] = kv.second;
}

static int merge_self_player_fields(TClient* client, const JsonFieldMap& fields) {
    int id = client->self_player_id;
    auto it = fields.find("id");
    if (it != fields.end()) id = json_int_value(it->second, id);
    if (id < 0) return id;
    {
        std::lock_guard<std::mutex> lock(client->players_mutex);
        client->self_player_id = id;
        TClientPlayerState& player = client->players[id];
        player.id = id;
        player.connected = true;
        for (const auto& kv : fields) player.fields[kv.first] = kv.second;
    }
    return id;
}

static TClientPlayerState snapshot_player(TClient* client, int id) {
    std::lock_guard<std::mutex> lock(client->players_mutex);
    auto it = client->players.find(id);
    if (it != client->players.end()) return it->second;
    TClientPlayerState player;
    player.id = id;
    player.connected = false;
    return player;
}

static std::vector<unsigned short> parse_tiles(const std::vector<uint8_t>& data) {
    std::vector<unsigned short> tiles;
    tiles.reserve(4096);
    for (size_t i = 0; i + 1 < data.size() && tiles.size() < 4096; i += 2) {
        unsigned short tile = static_cast<unsigned short>(data[i] | (data[i + 1] << 8));
        tiles.push_back(static_cast<unsigned short>(tile & 0x0fff));
    }
    while (tiles.size() < 4096) tiles.push_back(0);
    return tiles;
}

static void set_current_level(TClient* client, const std::string& level) {
    if (level.empty()) return;
    std::lock_guard<std::mutex> lock(client->players_mutex);
    client->current_level = level;
}

static std::string snapshot_current_level(TClient* client) {
    std::lock_guard<std::mutex> lock(client->players_mutex);
    return client->current_level.empty() ? "current-level" : client->current_level;
}

static std::string level_name_for_nw_dump(const std::string& level) {
    if (level.empty() || level == "current-level") return "current-level.nw";
    size_t slash = level.find_last_of("/\\");
    size_t dot = level.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        std::string ext = level.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".nw") return level;
    }
    return level + ".nw";
}

static std::vector<uint8_t> tiles_to_nw_bytes(const std::vector<unsigned short>& tiles, int layer) {
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::ostringstream out;
    out << "GLEVNW01\n";
    for (int y = 0; y < 64; ++y) {
        out << "BOARD 0 " << y << " 64 " << layer << " ";
        for (int x = 0; x < 64; ++x) {
            unsigned short tile = tiles[static_cast<size_t>(x + y * 64)];
            out << base64_chars[(tile >> 6) & 0x3f] << base64_chars[tile & 0x3f];
        }
        out << "\n";
    }
    std::string text = out.str();
    return std::vector<uint8_t>(text.begin(), text.end());
}

static std::string bytes_to_base64(const std::vector<uint8_t>& bytes) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t value = static_cast<uint32_t>(bytes[i]) << 16;
        bool has_b = i + 1 < bytes.size();
        bool has_c = i + 2 < bytes.size();
        if (has_b) value |= static_cast<uint32_t>(bytes[i + 1]) << 8;
        if (has_c) value |= static_cast<uint32_t>(bytes[i + 2]);
        out.push_back(chars[(value >> 18) & 0x3f]);
        out.push_back(chars[(value >> 12) & 0x3f]);
        out.push_back(has_b ? chars[(value >> 6) & 0x3f] : '=');
        out.push_back(has_c ? chars[value & 0x3f] : '=');
    }
    return out;
}

static void append_nw_board(std::ostringstream& out, const std::vector<unsigned short>& tiles, int layer) {
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int y = 0; y < 64; ++y) {
        out << "BOARD 0 " << y << " 64 " << layer << " ";
        for (int x = 0; x < 64; ++x) {
            unsigned short tile = tiles[static_cast<size_t>(x + y * 64)];
            out << base64_chars[(tile >> 6) & 0x3f] << base64_chars[tile & 0x3f];
        }
        out << "\n";
    }
}

static std::string format_decimal(double value) {
    if (std::abs(value - std::round(value)) < 0.0001) return std::to_string(static_cast<int>(std::round(value)));
    std::ostringstream out;
    out << value;
    return out.str();
}

static std::vector<uint8_t> level_state_to_nw_bytes(const TClientLevelState& level) {
    std::ostringstream out;
    out << "GLEVNW01\n";
    if (level.has_tiles && level.tiles.size() >= 4096) {
        append_nw_board(out, level.tiles, 0);
    }
    for (const std::string& link : level.links) {
        out << "LINK " << link << "\n";
    }
    std::vector<int> ids;
    ids.reserve(level.npcs.size());
    for (const auto& kv : level.npcs) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        auto it = level.npcs.find(id);
        if (it == level.npcs.end()) continue;
        const TClientLevelNpcState& npc = it->second;
        std::string image = npc.image.empty() ? "-" : npc.image;
        if (image == "-" || image == "#c#") continue;
        out << "NPC " << image << " " << format_decimal(npc.x) << " " << format_decimal(npc.y) << "\n";
        if (!npc.bytecode.empty()) {
            out << "//#GCLIB_BYTECODE_BASE64 " << bytes_to_base64(npc.bytecode) << "\n";
        }
        out << "NPCEND\n";
    }
    std::string text = out.str();
    return std::vector<uint8_t>(text.begin(), text.end());
}

static void emit_level_board_resource(TClient* client, const CallbackResource& cb, const std::string& dump_dir, const std::unordered_set<std::string>& dump_types, int packet_id, const char* type, const std::vector<unsigned short>& tiles, int layer) {
    std::string level = snapshot_current_level(client);
    std::vector<uint8_t> decoded = tiles_to_nw_bytes(tiles, layer);
    emit_resource(cb, dump_dir, dump_types, packet_id, type, level_name_for_nw_dump(level), decoded);
}

static bool is_level_capture_packet(int packet_id) {
    return packet_id == PLO_LEVELNAME ||
           packet_id == PLO_LEVELBOARD ||
           packet_id == PLO_LEVELLINK ||
           packet_id == PLO_BOARDPACKET ||
           packet_id == PLO_NPCPROPS ||
           packet_id == PLO_NPCBYTECODE ||
           packet_id == PLO_BOARDLAYER;
}

static bool is_level_flush_boundary_packet(int packet_id) {
    return packet_id == PLO_NPCWEAPONADD ||
           packet_id == PLO_PLAYERWARP ||
           packet_id == PLO_PLAYERWARP2 ||
           packet_id == PLO_DISCMESSAGE;
}

static bool level_capture_has_npcs(TClient* client) {
    std::lock_guard<std::mutex> lock(client->players_mutex);
    return !client->level_state.npcs.empty();
}

static void begin_level_capture(TClient* client, const std::string& level) {
    if (level.empty()) return;
    std::lock_guard<std::mutex> lock(client->players_mutex);
    client->current_level = level;
    client->level_state = TClientLevelState{};
    client->level_state.active = true;
    client->level_state.name = level;
}

static void record_level_tiles(TClient* client, const std::vector<unsigned short>& tiles) {
    std::lock_guard<std::mutex> lock(client->players_mutex);
    if (!client->level_state.active) {
        client->level_state.active = true;
        client->level_state.name = client->current_level.empty() ? "current-level" : client->current_level;
    }
    client->level_state.tiles = tiles;
    client->level_state.has_tiles = true;
}

static void record_level_link(TClient* client, const std::string& link) {
    if (link.empty()) return;
    std::lock_guard<std::mutex> lock(client->players_mutex);
    if (!client->level_state.active) {
        client->level_state.active = true;
        client->level_state.name = client->current_level.empty() ? "current-level" : client->current_level;
    }
    client->level_state.links.push_back(link);
}

static double decode_position16(int raw) {
    int shifted = raw >> 1;
    int value = (raw & 1) ? -shifted : shifted;
    return static_cast<double>(value) / 16.0;
}

static void record_level_npc_props(TClient* client, const std::vector<uint8_t>& payload) {
    if (payload.size() < 3) return;
    TPacketReader r(payload);
    TClientLevelNpcState npc;
    npc.id = r.gint3();
    while (r.has()) {
        int prop = r.gchar();
        switch (prop) {
        case 0:
            npc.image = r.gstring();
            break;
        case 1: {
            int size = r.gshort();
            r.str(static_cast<size_t>(size));
            break;
        }
        case 2:
            npc.x = r.gchar() / 2.0;
            break;
        case 3:
            npc.y = r.gchar() / 2.0;
            break;
        case 10: {
            int power = r.gchar();
            if (power > 4) r.gstring();
            break;
        }
        case 11: {
            int power = r.gchar();
            if (power > 3) r.gstring();
            break;
        }
        case 12:
        case 15:
        case 20:
        case 21:
        case 35:
        case 49:
        case 50:
        case 51:
        case 52:
            r.gstring();
            break;
        case 13:
        case 14:
        case 18:
        case 33:
        case 41:
        case 42:
            r.gchar();
            break;
        case 17:
            npc.id = r.gint3();
            break;
        case 19:
            r.str(5);
            break;
        case 22: {
            int len = r.gchar();
            if (len >= 100) r.str(static_cast<size_t>(len - 100));
            break;
        }
        case 34:
            r.str(6);
            break;
        case 74: {
            int size = r.gshort();
            r.str(static_cast<size_t>(size));
            break;
        }
        case 75:
            npc.x = decode_position16(r.gshort());
            break;
        case 76:
            npc.y = decode_position16(r.gshort());
            break;
        default:
            if ((prop >= 23 && prop <= 32) || (prop >= 36 && prop <= 40) || (prop >= 44 && prop <= 47) || (prop >= 53 && prop <= 73)) {
                r.gstring();
            } else {
                r.pos = r.len;
            }
            break;
        }
    }
    if (npc.image.empty()) npc.image = "-";
    std::lock_guard<std::mutex> lock(client->players_mutex);
    if (!client->level_state.active) {
        client->level_state.active = true;
        client->level_state.name = client->current_level.empty() ? "current-level" : client->current_level;
    }
    auto existing = client->level_state.npcs.find(npc.id);
    if (existing != client->level_state.npcs.end() && !existing->second.bytecode.empty()) {
        npc.bytecode = existing->second.bytecode;
    }
    client->level_state.npcs[npc.id] = std::move(npc);
}

static void record_level_npc_bytecode(TClient* client, int id, const std::vector<uint8_t>& bytecode) {
    std::lock_guard<std::mutex> lock(client->players_mutex);
    if (!client->level_state.active) {
        client->level_state.active = true;
        client->level_state.name = client->current_level.empty() ? "current-level" : client->current_level;
    }
    TClientLevelNpcState& npc = client->level_state.npcs[id];
    npc.id = id;
    if (npc.image.empty()) npc.image = "-";
    npc.bytecode = bytecode;
}

static void flush_level_capture(TClient* client, const CallbackResource& cb, const std::string& dump_dir, const std::unordered_set<std::string>& dump_types, int packet_id) {
    TClientLevelState snapshot;
    {
        std::lock_guard<std::mutex> lock(client->players_mutex);
        if (!client->level_state.active || (!client->level_state.has_tiles && client->level_state.npcs.empty())) return;
        snapshot = client->level_state;
        client->level_state.active = false;
    }
    std::string name = snapshot.name.empty() ? snapshot_current_level(client) : snapshot.name;
    std::vector<uint8_t> nw = level_state_to_nw_bytes(snapshot);
    emit_resource(cb, dump_dir, dump_types, packet_id, "level", level_name_for_nw_dump(name), nw);
}

static void split_flag(const std::string& raw, std::string& name, std::string& value) {
    size_t eq = raw.find('=');
    if (eq == std::string::npos) {
        name = raw;
        value.clear();
        return;
    }
    name = raw.substr(0, eq);
    value = raw.substr(eq + 1);
}

} // namespace

void tclient_flush_pending_level_capture(TClient* client, int packet_id) {
    if (!client) return;
    CallbackResource resource_cb;
    std::string resource_dump_directory;
    std::unordered_set<std::string> resource_dump_types;
    {
        std::lock_guard<std::mutex> lock(client->cb_mutex);
        resource_cb = client->on_resource;
        resource_dump_directory = client->resource_dump_directory;
        resource_dump_types = client->resource_dump_types;
    }
    flush_level_capture(client, resource_cb, resource_dump_directory, resource_dump_types, packet_id);
}

void tclient_dispatch_packet(TClient* client, int packet_id, const std::vector<uint8_t>& payload) {
    CallbackRawPacket raw_cb;
    CallbackChat chat_cb;
    CallbackChatEx chat_ex_cb;
    CallbackPrivateMessage pm_cb;
    CallbackLevelName level_cb;
    CallbackPlayerWarp warp_cb;
    CallbackPlayerWarp2 warp2_cb;
    CallbackPlayerProps props_cb;
    CallbackOtherPlayerProps other_props_cb;
    CallbackPlayerLeft left_cb;
    CallbackBoardPacket board_cb;
    CallbackFile file_cb;
    CallbackFileFailed file_failed_cb;
    CallbackWorldTime time_cb;
    CallbackNpcProps npc_cb;
    CallbackNpcDeleted npc_deleted_cb;
    CallbackSign sign_cb;
    CallbackExplosion explosion_cb;
    CallbackHitObjects hit_cb;
    CallbackServerText server_text_cb;
    CallbackFlagSet flag_set_cb;
    CallbackFlagDel flag_del_cb;
    CallbackWeaponScript weapon_script_cb;
    CallbackResource resource_cb;
    CallbackAuthenticated auth_cb;
    CallbackDisconnected disconnected_cb;
    CallbackPacketEvent packet_event_cb;
    std::string resource_dump_directory;
    std::unordered_set<std::string> resource_dump_types;
    {
        std::lock_guard<std::mutex> lock(client->cb_mutex);
        raw_cb = client->on_raw_packet;
        chat_cb = client->on_chat;
        chat_ex_cb = client->on_chat_ex;
        pm_cb = client->on_private_message;
        level_cb = client->on_level_name;
        warp_cb = client->on_player_warp;
        warp2_cb = client->on_player_warp2;
        props_cb = client->on_player_props;
        other_props_cb = client->on_other_player_props;
        left_cb = client->on_player_left;
        board_cb = client->on_board_packet;
        file_cb = client->on_file;
        file_failed_cb = client->on_file_failed;
        time_cb = client->on_world_time;
        npc_cb = client->on_npc_props;
        npc_deleted_cb = client->on_npc_deleted;
        sign_cb = client->on_sign;
        explosion_cb = client->on_explosion;
        hit_cb = client->on_hit_objects;
        server_text_cb = client->on_server_text;
        flag_set_cb = client->on_flag_set;
        flag_del_cb = client->on_flag_del;
        weapon_script_cb = client->on_weapon_script;
        resource_cb = client->on_resource;
        auth_cb = client->on_authenticated;
        disconnected_cb = client->on_disconnected;
        packet_event_cb = client->on_packet_event;
        resource_dump_directory = client->resource_dump_directory;
        resource_dump_types = client->resource_dump_types;
    }

    if (raw_cb.cb) raw_cb.cb(packet_id, payload.data(), static_cast<int>(payload.size()), raw_cb.ud);

    switch (packet_id) {
    case PLO_LEVELBOARD: {
        std::vector<unsigned short> tiles = parse_tiles(payload);
        if (payload.size() >= 8192) record_level_tiles(client, tiles);
        if (board_cb.cb) board_cb.cb(tiles.data(), static_cast<int>(tiles.size()), payload.data(), static_cast<int>(payload.size()), board_cb.ud);
        emit_packet_event(packet_event_cb, packet_id, parse_board_summary_json(payload, 0));
        break;
    }
    case PLO_LEVELLINK: {
        std::string link = to_string_lossy(payload);
        record_level_link(client, link);
        emit_packet_event(packet_event_cb, packet_id, json_string_field("link", link));
        break;
    }
    case PLO_LEVELCHEST: {
        emit_packet_event(packet_event_cb, packet_id, parse_level_chest_json(payload));
        break;
    }
    case PLO_BOARDMODIFY: {
        emit_packet_event(packet_event_cb, packet_id, parse_board_modify_json(payload));
        break;
    }
    case PLO_BOARDLAYER: {
        std::vector<unsigned short> tiles = parse_tiles(payload);
        if (payload.size() >= 8192) {
            emit_level_board_resource(client, resource_cb, resource_dump_directory, resource_dump_types, packet_id, "level-board-layer", tiles, 1);
        }
        if (board_cb.cb) board_cb.cb(tiles.data(), static_cast<int>(tiles.size()), payload.data(), static_cast<int>(payload.size()), board_cb.ud);
        emit_packet_event(packet_event_cb, packet_id, parse_board_summary_json(payload, 1));
        break;
    }
    case PLO_SIGNATURE: {
        if (!client->authenticated.exchange(true) && auth_cb.cb) auth_cb.cb(auth_cb.ud);
        break;
    }
    case PLO_DISCMESSAGE: {
        flush_level_capture(client, resource_cb, resource_dump_directory, resource_dump_types, packet_id);
        std::string reason = to_string_lossy(payload);
        client->connected = false;
        if (disconnected_cb.cb) disconnected_cb.cb(reason.c_str(), disconnected_cb.ud);
        break;
    }
    case PLO_WARPFAILED: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("reason", to_string_lossy(payload)));
        break;
    }
    case PLO_PLAYERPROPS: {
        JsonFieldMap fields;
        std::string props = parse_props_json(payload, 0, &fields);
        int self_id = merge_self_player_fields(client, fields);
        auto level_it = fields.find("level");
        if (level_it != fields.end()) set_current_level(client, json_string_value(level_it->second));
        auto chat_it = fields.find("chat");
        if (self_id >= 0 && chat_it != fields.end()) {
            std::string message = json_string_value(chat_it->second);
            if (!message.empty()) {
                if (chat_cb.cb) chat_cb.cb(self_id, message.c_str(), chat_cb.ud);
                if (chat_ex_cb.cb) {
                    TClientPlayerState player = snapshot_player(client, self_id);
                    std::string account = player_field_string(player, "account");
                    std::string nickname = player_field_string(player, "nickname");
                    std::string community = player_field_string(player, "community");
                    std::string level = player_field_string(player, "level");
                    chat_ex_cb.cb(self_id, account.c_str(), nickname.c_str(), community.c_str(), level.c_str(), message.c_str(), chat_ex_cb.ud);
                }
            }
        }
        if (!client->authenticated.exchange(true) && auth_cb.cb) auth_cb.cb(auth_cb.ud);
        if (props_cb.cb) props_cb.cb(props.c_str(), props_cb.ud);
        break;
    }
    case PLO_OTHERPLPROPS: {
        if (payload.size() < 2) break;
        TPacketReader r(payload);
        int id = r.gshort();
        JsonFieldMap fields;
        std::string props = parse_props_json(payload, r.pos, &fields);
        merge_player_fields(client, id, fields);
        auto chat_it = fields.find("chat");
        if (chat_it != fields.end()) {
            std::string message = json_string_value(chat_it->second);
            if (!message.empty()) {
                if (chat_cb.cb) chat_cb.cb(id, message.c_str(), chat_cb.ud);
                if (chat_ex_cb.cb) {
                    TClientPlayerState player = snapshot_player(client, id);
                    std::string account = player_field_string(player, "account");
                    std::string nickname = player_field_string(player, "nickname");
                    std::string community = player_field_string(player, "community");
                    std::string level = player_field_string(player, "level");
                    chat_ex_cb.cb(id, account.c_str(), nickname.c_str(), community.c_str(), level.c_str(), message.c_str(), chat_ex_cb.ud);
                }
            }
        }
        if (other_props_cb.cb) other_props_cb.cb(id, props.c_str(), other_props_cb.ud);
        break;
    }
    case PLO_TOALL: {
        if (payload.size() < 3) break;
        TPacketReader r(payload);
        int id = r.gshort();
        int len = r.gchar();
        std::string message = r.str(static_cast<size_t>(len));
        if (chat_cb.cb) chat_cb.cb(id, message.c_str(), chat_cb.ud);
        if (chat_ex_cb.cb) {
            TClientPlayerState player = snapshot_player(client, id);
            std::string account = player_field_string(player, "account");
            std::string nickname = player_field_string(player, "nickname");
            std::string community = player_field_string(player, "community");
            std::string level = player_field_string(player, "level");
            chat_ex_cb.cb(id, account.c_str(), nickname.c_str(), community.c_str(), level.c_str(), message.c_str(), chat_ex_cb.ud);
        }
        break;
    }
    case PLO_SHOWIMG: {
        emit_packet_event(packet_event_cb, packet_id, parse_showimg_json(payload));
        break;
    }
    case PLO_PRIVATEMESSAGE: {
        if (payload.size() < 2) break;
        TPacketReader r(payload);
        int id = r.gshort();
        std::string text = to_string_lossy(r.remaining());
        std::string type;
        std::string message = text;
        size_t comma = text.find(',');
        if (comma != std::string::npos) {
            type = text.substr(0, comma);
            message = text.substr(comma + 1);
        }
        if (pm_cb.cb) pm_cb.cb(id, type.c_str(), message.c_str(), pm_cb.ud);
        break;
    }
    case PLO_LEVELNAME: {
        std::string level = to_string_lossy(payload);
        flush_level_capture(client, resource_cb, resource_dump_directory, resource_dump_types, packet_id);
        begin_level_capture(client, level);
        if (level_cb.cb) level_cb.cb(level.c_str(), level_cb.ud);
        break;
    }
    case PLO_PLAYERWARP: {
        if (payload.size() < 2) break;
        TPacketReader r(payload);
        float x = static_cast<float>(r.gchar()) / 2.0f;
        float y = static_cast<float>(r.gchar()) / 2.0f;
        std::string level = to_string_lossy(r.remaining());
        set_current_level(client, level);
        if (warp_cb.cb) warp_cb.cb(x, y, level.c_str(), warp_cb.ud);
        break;
    }
    case PLO_PLAYERWARP2: {
        if (payload.size() < 5) break;
        TPacketReader r(payload);
        float x = static_cast<float>(r.gchar()) / 2.0f;
        float y = static_cast<float>(r.gchar()) / 2.0f;
        int z = r.gchar();
        int gx = r.gchar();
        int gy = r.gchar();
        std::string level = to_string_lossy(r.remaining());
        set_current_level(client, level);
        if (warp2_cb.cb) warp2_cb.cb(x, y, z, gx, gy, level.c_str(), warp2_cb.ud);
        break;
    }
    case PLO_DELPLAYER: {
        if (payload.size() < 2) break;
        TPacketReader r(payload);
        int id = r.gshort();
        {
            std::lock_guard<std::mutex> lock(client->players_mutex);
            auto it = client->players.find(id);
            if (it != client->players.end()) it->second.connected = false;
        }
        if (left_cb.cb) left_cb.cb(id, left_cb.ud);
        break;
    }
    case PLO_ADDPLAYER: {
        TPacketReader r(payload);
        int id = r.has(2) ? r.gshort() : -1;
        std::string account = r.has() ? r.gstring() : "";
        JsonFieldMap fields;
        fields["account"] = "\"" + json_escape(account) + "\"";
        if (r.has()) parse_props_json(payload, r.pos, &fields);
        if (id >= 0) merge_player_fields(client, id, fields);
        emit_packet_event(packet_event_cb, packet_id, parse_add_player_json(payload));
        break;
    }
    case PLO_BOARDPACKET: {
        std::vector<unsigned short> tiles = parse_tiles(payload);
        if (payload.size() >= 8192) record_level_tiles(client, tiles);
        if (board_cb.cb) board_cb.cb(tiles.data(), static_cast<int>(tiles.size()), payload.data(), static_cast<int>(payload.size()), board_cb.ud);
        emit_packet_event(packet_event_cb, packet_id, parse_board_summary_json(payload, 0));
        break;
    }
    case PLO_FILE: {
        if (payload.size() < 7) break;
        TPacketReader r(payload);
        int mod_time = r.gint5();
        std::string filename = r.gstring();
        std::vector<uint8_t> content = r.remaining();
        if (!content.empty() && content.back() == '\n') content.pop_back();
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, is_level_resource_name(filename) ? "level" : "file", filename, content);
        if (file_cb.cb) file_cb.cb(filename.c_str(), content.data(), static_cast<int>(content.size()), mod_time, file_cb.ud);
        break;
    }
    case PLO_FILESENDFAILED:
    case 104: {
        std::string filename = to_string_lossy(payload);
        if (file_failed_cb.cb) file_failed_cb.cb(filename.c_str(), file_failed_cb.ud);
        break;
    }
    case PLO_NEWWORLDTIME: {
        int value = 0;
        for (size_t i = 0; i < payload.size() && i < 4; ++i) value |= static_cast<int>(payload[i]) << (i * 8);
        if (time_cb.cb) time_cb.cb(value, time_cb.ud);
        break;
    }
    case PLO_STARTMESSAGE:
    case PLO_RC_ADMINMESSAGE: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("message", to_string_lossy(payload)));
        break;
    }
    case PLO_ISLEADER:
    case PLO_HASNPCSERVER:
    case PLO_FULLSTOP:
    case PLO_FULLSTOP2: {
        emit_packet_event(packet_event_cb, packet_id, "{\"enabled\":true}");
        break;
    }
    case PLO_FLAGSET: {
        std::string raw = to_string_lossy(payload);
        std::string name;
        std::string value;
        split_flag(raw, name, value);
        if (flag_set_cb.cb) flag_set_cb.cb(name.c_str(), value.c_str(), flag_set_cb.ud);
        break;
    }
    case PLO_FLAGDEL: {
        std::string name = to_string_lossy(payload);
        if (flag_del_cb.cb) flag_del_cb.cb(name.c_str(), flag_del_cb.ud);
        break;
    }
    case PLO_NPCPROPS: {
        if (payload.size() < 3) break;
        TPacketReader r(payload);
        int id = r.gint3();
        std::string props = parse_props_json(payload, r.pos);
        record_level_npc_props(client, payload);
        if (npc_cb.cb) npc_cb.cb(id, props.c_str(), npc_cb.ud);
        break;
    }
    case PLO_NPCDEL: {
        if (payload.size() < 3) break;
        TPacketReader r(payload);
        int id = r.gint3();
        if (npc_deleted_cb.cb) npc_deleted_cb.cb(id, npc_deleted_cb.ud);
        break;
    }
    case PLO_NPCDEL2: {
        emit_packet_event(packet_event_cb, packet_id, parse_local_npc_delete_json(payload));
        break;
    }
    case PLO_NPCMOVED: {
        emit_packet_event(packet_event_cb, packet_id, parse_npc_removed_json(payload));
        break;
    }
    case PLO_LEVELSIGN: {
        if (payload.size() < 2) break;
        TPacketReader r(payload);
        float x = static_cast<float>(r.gchar()) / 2.0f;
        float y = static_cast<float>(r.gchar()) / 2.0f;
        std::string text = to_string_lossy(r.remaining());
        if (sign_cb.cb) sign_cb.cb(x, y, text.c_str(), sign_cb.ud);
        break;
    }
    case PLO_SAY2: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("text", to_string_lossy(payload)));
        break;
    }
    case PLO_EXPLOSION: {
        if (payload.size() < 3) break;
        TPacketReader r(payload);
        float x = static_cast<float>(r.gchar()) / 2.0f;
        float y = static_cast<float>(r.gchar()) / 2.0f;
        int radius = r.gchar();
        int power = r.has() ? r.gchar() : 1;
        if (explosion_cb.cb) explosion_cb.cb(x, y, radius, power, explosion_cb.ud);
        break;
    }
    case PLO_HITOBJECTS: {
        if (payload.size() < 3) break;
        TPacketReader r(payload);
        float x = static_cast<float>(r.gchar()) / 2.0f;
        float y = static_cast<float>(r.gchar()) / 2.0f;
        int power = r.gchar();
        int id = r.has(2) ? r.gshort() : 0;
        if (hit_cb.cb) hit_cb.cb(x, y, power, id, hit_cb.ud);
        break;
    }
    case PLO_HURTPLAYER: {
        emit_packet_event(packet_event_cb, packet_id, parse_hurt_player_json(payload));
        break;
    }
    case PLO_PUSHAWAY: {
        emit_packet_event(packet_event_cb, packet_id, parse_pushaway_json(payload));
        break;
    }
    case PLO_ARROWADD: {
        emit_packet_event(packet_event_cb, packet_id, parse_arrow_add_json(payload));
        break;
    }
    case PLO_BOMBADD: {
        emit_packet_event(packet_event_cb, packet_id, parse_bomb_add_json(payload));
        break;
    }
    case PLO_BOMBDEL: {
        emit_packet_event(packet_event_cb, packet_id, parse_xy_json(payload));
        break;
    }
    case PLO_ITEMADD: {
        emit_packet_event(packet_event_cb, packet_id, parse_extra_add_json(payload));
        break;
    }
    case PLO_ITEMDEL: {
        emit_packet_event(packet_event_cb, packet_id, parse_xy_json(payload));
        break;
    }
    case PLO_FIRESPY: {
        emit_packet_event(packet_event_cb, packet_id, parse_fire_spy_json(payload));
        break;
    }
    case PLO_THROWCARRIED: {
        emit_packet_event(packet_event_cb, packet_id, parse_player_id_json(payload));
        break;
    }
    case PLO_HORSEADD:
    case PLO_HORSEDEL:
    case PLO_NPCACTION:
    case PLO_BADDYPROPS:
    case PLO_BADDYHURT: {
        emit_packet_event(packet_event_cb, packet_id, json_raw_event(payload, "world-object"));
        break;
    }
    case PLO_SERVERTEXT: {
        std::string text = to_string_lossy(payload);
        if (server_text_cb.cb) server_text_cb.cb(text.c_str(), server_text_cb.ud);
        break;
    }
    case PLO_NPCWEAPONSCRIPT: {
        if (payload.size() < 2) break;
        TPacketReader r(payload);
        int header_len = r.gshort();
        if (header_len < 0 || !r.has(static_cast<size_t>(header_len))) break;
        std::string header = r.str(static_cast<size_t>(header_len));
        std::string script_type;
        std::string script_name;
        size_t comma = header.find(',');
        if (comma == std::string::npos) {
            script_name = header;
        } else {
            script_type = header.substr(0, comma);
            script_name = header.substr(comma + 1);
        }
        std::vector<uint8_t> bytecode = r.remaining();
        std::string resource_type = script_type.empty() ? "weapon-bytecode" : script_type + "-bytecode";
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, resource_type, script_name, bytecode);
        if (weapon_script_cb.cb) {
            weapon_script_cb.cb(script_type.c_str(), script_name.c_str(), bytecode.data(), static_cast<int>(bytecode.size()), weapon_script_cb.ud);
        }
        break;
    }
    case PLO_NPCWEAPONADD: {
        emit_packet_event(packet_event_cb, packet_id, parse_npc_weapon_add_json(payload));
        break;
    }
    case PLO_NPCWEAPONDEL: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("name", to_string_lossy(payload)));
        break;
    }
    case PLO_LEVELMODTIME: {
        TPacketReader r(payload);
        std::ostringstream json;
        json << "{\"mod_time\":" << (payload.size() >= 5 ? r.gint5() : 0) << "}";
        emit_packet_event(packet_event_cb, packet_id, json.str());
        break;
    }
    case PLO_DEFAULTWEAPON: {
        TPacketReader r(payload);
        std::ostringstream json;
        json << "{\"weapon_id\":" << (r.has() ? r.gchar() : -1) << "}";
        emit_packet_event(packet_event_cb, packet_id, json.str());
        break;
    }
    case PLO_FILEUPTODATE: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("filename", to_string_lossy(payload)));
        break;
    }
    case PLO_LARGEFILESTART:
    case PLO_LARGEFILEEND: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("filename", to_string_lossy(payload)));
        break;
    }
    case PLO_RC_FILEBROWSER_MESSAGE:
    case PLO_RC_CHAT:
    case PLO_PROFILE:
    case PLO_NC_CONTROL:
    case PLO_NC_LEVELLIST:
    case PLO_UNKNOWN81:
    case PLO_UNKNOWN83:
    case PLO_UNKNOWN109:
    case PLO_UNKNOWN111:
    case PLO_UNKNOWN124:
    case PLO_UNKNOWN132:
    case PLO_UNKNOWN133:
    case PLO_UNKNOWN166:
    case PLO_UNKNOWN169:
    case PLO_UNKNOWN181:
    case PLO_UNKNOWN183:
    case PLO_UNKNOWN184:
    case PLO_UNKNOWN185:
    case PLO_UNKNOWN186:
    case PLO_UNKNOWN193:
    case PLO_UNKNOWN195:
    case PLO_UNKNOWN198: {
        emit_packet_event(packet_event_cb, packet_id, json_raw_event(payload, "unknown"));
        break;
    }
    case PLO_NPCSERVERADDR: {
        emit_packet_event(packet_event_cb, packet_id, parse_npc_server_addr_json(payload));
        break;
    }
    case PLO_LARGEFILESIZE: {
        emit_packet_event(packet_event_cb, packet_id, parse_gint5_json(payload, "size"));
        break;
    }
    case PLO_RC_MAXUPLOADFILESIZE: {
        emit_packet_event(packet_event_cb, packet_id, parse_gint5_json(payload, "max_upload_size"));
        break;
    }
    case PLO_UPDATEPACKAGESIZE: {
        emit_packet_event(packet_event_cb, packet_id, parse_gint5_json(payload, "package_size"));
        break;
    }
    case PLO_UPDATEPACKAGEDONE: {
        emit_packet_event(packet_event_cb, packet_id, "{\"done\":true}");
        break;
    }
    case PLO_STAFFGUILDS: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("guilds", to_string_lossy(payload)));
        break;
    }
    case PLO_TRIGGERACTION: {
        emit_packet_event(packet_event_cb, packet_id, json_raw_event(payload, "trigger-action"));
        break;
    }
    case PLO_NPCBYTECODE: {
        if (payload.size() < 3) break;
        TPacketReader r(payload);
        int id = r.gint3();
        std::vector<uint8_t> bytecode = r.remaining();
        record_level_npc_bytecode(client, id, bytecode);
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, "npc-bytecode", std::to_string(id), bytecode);
        std::ostringstream json;
        json << "{\"npc_id\":" << id << ",\"bytecode_length\":" << bytecode.size()
             << ",\"bytecode_hex\":\"" << bytes_hex(bytecode.data(), bytecode.size()) << "\"}";
        emit_packet_event(packet_event_cb, packet_id, json.str());
        break;
    }
    case PLO_GANISCRIPT: {
        TPacketReader r(payload);
        std::string name = r.has() ? r.gstring() : "";
        std::vector<uint8_t> script = r.remaining();
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, "gani-script", name, script);
        emit_packet_event(packet_event_cb, packet_id, parse_name_data_json(payload, "name", "script"));
        break;
    }
    case PLO_HIDENPCS: {
        emit_packet_event(packet_event_cb, packet_id, "{\"hidden\":true}");
        break;
    }
    case PLO_FREEZEPLAYER2: {
        emit_packet_event(packet_event_cb, packet_id, "{\"frozen\":true}");
        break;
    }
    case PLO_UNFREEZEPLAYER: {
        emit_packet_event(packet_event_cb, packet_id, "{\"frozen\":false}");
        break;
    }
    case PLO_SETACTIVELEVEL: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("level", to_string_lossy(payload)));
        break;
    }
    case PLO_NC_NPCATTRIBUTES: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("attributes", to_string_lossy(payload)));
        break;
    }
    case PLO_NC_NPCADD: {
        emit_packet_event(packet_event_cb, packet_id, parse_nc_npc_add_json(payload));
        break;
    }
    case PLO_NC_NPCDELETE: {
        TPacketReader r(payload);
        std::ostringstream json;
        json << "{\"id\":" << (r.has(3) ? r.gint3() : -1) << "}";
        emit_packet_event(packet_event_cb, packet_id, json.str());
        break;
    }
    case PLO_NC_NPCSCRIPT: {
        TPacketReader r(payload);
        int id = r.has(3) ? r.gint3() : -1;
        std::string script = r.has() ? r.gstring() : "";
        std::vector<uint8_t> bytes(script.begin(), script.end());
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, "npc-script", std::to_string(id), bytes);
        emit_packet_event(packet_event_cb, packet_id, parse_id_gstring_json(payload, "script"));
        break;
    }
    case PLO_NC_NPCFLAGS: {
        emit_packet_event(packet_event_cb, packet_id, parse_id_gstring_json(payload, "flags"));
        break;
    }
    case PLO_NC_CLASSGET: {
        TPacketReader r(payload);
        std::string name = r.has() ? r.gstring() : "";
        std::vector<uint8_t> script = r.remaining();
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, "class-script", name, script);
        emit_packet_event(packet_event_cb, packet_id, parse_name_data_json(payload, "class", "script"));
        break;
    }
    case PLO_NC_CLASSADD:
    case PLO_NC_CLASSDELETE: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("class", to_string_lossy(payload)));
        break;
    }
    case PLO_NC_WEAPONLISTGET: {
        emit_packet_event(packet_event_cb, packet_id, parse_gstring_list_json(payload, "weapons"));
        break;
    }
    case PLO_NC_LEVELDUMP:
        emit_resource(resource_cb, resource_dump_directory, resource_dump_types, packet_id, "level", "leveldump", payload);
        emit_packet_event(packet_event_cb, packet_id, parse_resource_summary_json(payload, "level"));
        break;
    case PLO_MOVE:
    case PLO_MOVE2:
    case PLO_SHOOT:
    case PLO_SHOOT2:
    case PLO_LISTPROCESSES:
    case PLO_UPDATEPACKAGEISUPDATED: {
        emit_packet_event(packet_event_cb, packet_id, json_raw_event(payload, "motion-or-action"));
        break;
    }
    case PLO_GHOSTMODE: {
        TPacketReader r(payload);
        std::ostringstream json;
        json << "{\"enabled\":" << ((r.has() ? r.gchar() : 0) ? "true" : "false") << "}";
        emit_packet_event(packet_event_cb, packet_id, json.str());
        break;
    }
    case PLO_BIGMAP:
    case PLO_MINIMAP: {
        emit_packet_event(packet_event_cb, packet_id, parse_comma_fields_json(payload, "config", "fields"));
        break;
    }
    case PLO_GHOSTTEXT: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("text", to_string_lossy(payload)));
        break;
    }
    case PLO_SERVERWARP: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("server", to_string_lossy(payload)));
        break;
    }
    case PLO_STATUSLIST: {
        emit_packet_event(packet_event_cb, packet_id, json_string_field("status", to_string_lossy(payload)));
        break;
    }
    case PLO_RPGWINDOW: {
        emit_packet_event(packet_event_cb, packet_id, json_raw_event(payload, "ui"));
        break;
    }
    case PLO_GHOSTICON: {
        TPacketReader r(payload);
        std::ostringstream json;
        json << "{\"enabled\":" << ((r.has() ? r.gchar() : 0) ? "true" : "false") << "}";
        emit_packet_event(packet_event_cb, packet_id, json.str());
        break;
    }
    case PLO_UNKNOWN190: {
        emit_packet_event(packet_event_cb, packet_id, "{\"blank\":true}");
        break;
    }
    case PLO_CLEARWEAPONS: {
        emit_packet_event(packet_event_cb, packet_id, "{\"clear\":true}");
        break;
    }
    case PLO_NC_WEAPONGET: {
        emit_packet_event(packet_event_cb, packet_id, parse_gstring_pair_plus_rest_json(payload, "name", "image", "script"));
        break;
    }
    case PLO_UNKNOWN197: {
        emit_packet_event(packet_event_cb, packet_id, parse_cache_metadata_json(payload));
        break;
    }
    default:
        emit_packet_event(packet_event_cb, packet_id, json_raw_event(payload, "unhandled"));
        break;
    }
    if (!is_level_capture_packet(packet_id) && is_level_flush_boundary_packet(packet_id) &&
        (packet_id != PLO_NPCWEAPONADD || level_capture_has_npcs(client))) {
        flush_level_capture(client, resource_cb, resource_dump_directory, resource_dump_types, packet_id);
    }
}

void tclient_process_decrypted(TClient* client, const std::vector<uint8_t>& decrypted) {
    size_t pos = 0;
    while (pos < decrypted.size()) {
        if (client->raw_expected > 0) {
            int needed = client->raw_expected - static_cast<int>(client->raw_buffer.size());
            int available = static_cast<int>(std::min<size_t>(needed, decrypted.size() - pos));
            client->raw_buffer.insert(client->raw_buffer.end(), decrypted.begin() + pos, decrypted.begin() + pos + available);
            pos += static_cast<size_t>(available);
            if (static_cast<int>(client->raw_buffer.size()) >= client->raw_expected) {
                std::vector<uint8_t> raw = client->raw_buffer;
                client->raw_buffer.clear();
                client->raw_expected = 0;
                if (!raw.empty()) {
                    int id = static_cast<int>(raw[0]) - 32;
                    std::vector<uint8_t> body(raw.begin() + 1, raw.end());
                    if (!body.empty() && body.back() == '\n') body.pop_back();
                    tclient_dispatch_packet(client, id, body);
                }
            }
            continue;
        }

        auto it = std::find(decrypted.begin() + static_cast<std::ptrdiff_t>(pos), decrypted.end(), static_cast<uint8_t>('\n'));
        if (it == decrypted.end()) break;
        if (it == decrypted.begin() + static_cast<std::ptrdiff_t>(pos)) {
            pos++;
            continue;
        }

        std::vector<uint8_t> packet(decrypted.begin() + static_cast<std::ptrdiff_t>(pos), it);
        pos = static_cast<size_t>((it - decrypted.begin()) + 1);
        int packet_id = static_cast<int>(packet[0]) - 32;
        std::vector<uint8_t> body(packet.begin() + 1, packet.end());
        if (packet_id == PLO_RAWDATA && body.size() >= 3) {
            TPacketReader r(body);
            client->raw_expected = r.gint3();
        }
        tclient_dispatch_packet(client, packet_id, body);
    }
}
