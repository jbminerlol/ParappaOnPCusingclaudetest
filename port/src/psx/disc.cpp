#include "psx/disc.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "psx/log.h"

namespace psx {

Disc g_disc;

static uint8_t to_bcd(uint32_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }
static uint32_t from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

void lba_to_msf(uint32_t lba, uint8_t *minute, uint8_t *second, uint8_t *sector) {
    lba += 150;
    *minute = to_bcd(lba / (60 * 75));
    *second = to_bcd((lba / 75) % 60);
    *sector = to_bcd(lba % 75);
}

uint32_t msf_to_lba(uint8_t minute, uint8_t second, uint8_t sector) {
    return (from_bcd(minute) * 60 + from_bcd(second)) * 75 + from_bcd(sector) - 150;
}

// The image file named by the first FILE line of a cue sheet (relative to the sheet).
static std::string cue_image(const std::string &cue_path) {
    FILE *f = std::fopen(cue_path.c_str(), "rb");
    if (!f) return {};
    char line[1024];
    std::string image;
    while (image.empty() && std::fgets(line, sizeof(line), f)) {
        std::string l = line;
        size_t k = l.find("FILE");
        if (k == std::string::npos) continue;
        size_t q1 = l.find('"', k), q2 = q1 == std::string::npos ? q1 : l.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) image = l.substr(q1 + 1, q2 - q1 - 1);
    }
    std::fclose(f);
    if (image.empty()) return {};
    size_t slash = cue_path.find_last_of("\\/");
    bool absolute = image.size() > 1 && (image[1] == ':' || image[0] == '\\' || image[0] == '/');
    std::string path = (absolute || slash == std::string::npos) ? image : cue_path.substr(0, slash + 1) + image;
    // Renamed dumps often keep the old name in the sheet: then use the .bin named like the sheet.
    if (FILE *probe = std::fopen(path.c_str(), "rb")) {
        std::fclose(probe);
        return path;
    }
    std::string same_name = cue_path.substr(0, cue_path.size() - 4) + ".bin";
    if (FILE *probe = std::fopen(same_name.c_str(), "rb")) {
        std::fclose(probe);
        LOGI("Cue sheet names %s, which is missing; using %s", path.c_str(), same_name.c_str());
        return same_name;
    }
    return path;
}

bool Disc::open(const std::string &path) {
    close();
    std::string image = path;
    std::string ext = path.size() > 4 ? path.substr(path.size() - 4) : "";
    for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".cue") {
        image = cue_image(path);
        if (image.empty()) {
            LOGE("No FILE entry in cue sheet %s", path.c_str());
            return false;
        }
    }
    file_ = std::fopen(image.c_str(), "rb");
    if (!file_) return false;
    _fseeki64(file_, 0, SEEK_END);
    int64_t size = _ftelli64(file_);
    sector_size_ = (size % RAW_SECTOR == 0) ? RAW_SECTOR : (size % DATA_SECTOR == 0 ? DATA_SECTOR : RAW_SECTOR);
    sectors_ = uint32_t(size / sector_size_);
    LOGI("Disc image %s: %u sectors of %d bytes", image.c_str(), sectors_, sector_size_);
    if (cooked()) LOGW("2048-byte image: it has no CD-XA audio, so the music will be missing");
    return true;
}

void Disc::close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    sectors_ = 0;
}

bool Disc::is_parappa() {
    DiscFile cnf;
    if (!find("\\SYSTEM.CNF;1", &cnf) || cnf.size > 4096) return false;
    std::vector<uint8_t> buf((cnf.size + DATA_SECTOR - 1) / DATA_SECTOR * DATA_SECTOR);
    for (uint32_t i = 0; i < buf.size() / DATA_SECTOR; i++)
        if (!read_data(cnf.lba + i, &buf[i * DATA_SECTOR])) return false;
    std::string text(reinterpret_cast<char *>(buf.data()), cnf.size);
    for (char &c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text.find("SCUS_941.83") != std::string::npos;
}

bool Disc::read_raw(uint32_t lba, uint8_t *out) {
    if (!file_ || lba >= sectors_) return false;
    _fseeki64(file_, int64_t(lba) * sector_size_, SEEK_SET);
    if (sector_size_ == RAW_SECTOR) return std::fread(out, RAW_SECTOR, 1, file_) == 1;
    // Cooked image: synthesize a Mode 2 Form 1 data sector around the 2048 bytes.
    std::memset(out, 0, RAW_SECTOR);
    out[18] = out[22] = 0x08;  // subheader submode: data
    return std::fread(out + 24, DATA_SECTOR, 1, file_) == 1;
}

bool Disc::read_data(uint32_t lba, uint8_t *out) {
    uint8_t raw[RAW_SECTOR];
    if (!read_raw(lba, raw)) return false;
    std::memcpy(out, raw + 24, DATA_SECTOR);  // 12 sync + 4 header + 8 subheader
    return true;
}

static std::string normalize(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    size_t semi = s.find(';');
    if (semi != std::string::npos) s.resize(semi);
    return s;
}

bool Disc::find_in_dir(uint32_t lba, uint32_t size, const std::string &name, DiscFile *out) {
    std::vector<uint8_t> dir((size + DATA_SECTOR - 1) / DATA_SECTOR * DATA_SECTOR);
    for (uint32_t i = 0; i < dir.size() / DATA_SECTOR; i++)
        if (!read_data(lba + i, &dir[i * DATA_SECTOR])) return false;
    for (uint32_t off = 0; off < size;) {
        uint8_t len = dir[off];
        if (len == 0) {  // records never cross sectors
            off = (off / DATA_SECTOR + 1) * DATA_SECTOR;
            continue;
        }
        std::string entry(reinterpret_cast<char *>(&dir[off + 33]), dir[off + 32]);
        if (normalize(entry) == name) {
            std::memcpy(&out->lba, &dir[off + 2], 4);
            std::memcpy(&out->size, &dir[off + 10], 4);
            return true;
        }
        off += len;
    }
    return false;
}

bool Disc::find(const std::string &path, DiscFile *out) {
    uint8_t pvd[DATA_SECTOR];
    if (!read_data(16, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0) return false;
    DiscFile cur;
    std::memcpy(&cur.lba, pvd + 156 + 2, 4);
    std::memcpy(&cur.size, pvd + 156 + 10, 4);
    std::string p = normalize(path);
    size_t pos = 0;
    while (pos < p.size()) {
        if (p[pos] == '\\' || p[pos] == '/') {
            pos++;
            continue;
        }
        size_t end = p.find_first_of("\\/", pos);
        std::string part = p.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!find_in_dir(cur.lba, cur.size, part, &cur)) return false;
        if (end == std::string::npos) break;
        pos = end;
    }
    *out = cur;
    return true;
}

}  // namespace psx
