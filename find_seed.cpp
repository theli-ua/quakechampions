#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cstring>
#include <omp.h>

const static uint32_t magic = 0x06064b50;
const static uint32_t entry_magic = 0x02014b50;

#pragma pack(push, 1)
typedef struct {
    /*
    findloc OFFSET binary "PK\x06\x06" 0 "" LIMIT
    math OFFSET + 4
    goto OFFSET
    get ZERO byte
    get central_entries longlong
    get central_size longlong
    get central_offset longlong
    get DUMMY_offset longlong
    */
    uint32_t magic;
    uint8_t unk;
    uint64_t central_entries;
    uint64_t central_size;
    uint64_t central_offset;
    uint64_t dummy_offset;
} main_header_t;

typedef struct {
    /*
        idstring MEMORY_FILE "PK\x01\x02"
        get ver_made        short MEMORY_FILE
        get ver_need        short MEMORY_FILE
        get flag            short MEMORY_FILE
        get method          short MEMORY_FILE
        get modtime         short MEMORY_FILE
        get moddate         short MEMORY_FILE
        get zip_crc         long MEMORY_FILE
        get comp_size       long MEMORY_FILE
        get uncomp_size     long MEMORY_FILE
        get name_len        short MEMORY_FILE
        get extra_len       short MEMORY_FILE
        get comm_len        short MEMORY_FILE
        get disknum         short MEMORY_FILE
        get int_attr        short MEMORY_FILE
        get ext_attr        long MEMORY_FILE
        get rel_offset      long MEMORY_FILE
            */
    uint32_t magic;
    uint16_t ver_made;
    uint16_t ver_need;
    uint16_t flag;
    uint16_t method;
    uint16_t modtime;
    uint16_t moddate;
    uint32_t zip_crc;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comm_len;
    uint16_t disknum;
    uint16_t int_attr;
    uint32_t ext_attr;
    uint32_t rel_offset;
} entry_header_t;
#pragma pack(pop)

class QCDecrypt {
    private:
    uint64_t u, v, w;

    uint64_t NextUInt64()
    {
        u = u * 2862933555777941757ULL + 7046029254386353087ULL;
        v ^= v >> 17; v ^= v << 31; v ^= v >> 8;
        w = 4294957665U * (w & 0xffffffff) + (w >> 32);
        uint64_t x = u ^ (u << 21); x ^= x >> 35; x ^= x << 4;
        return (x + v) ^ w;
    }

    void NrRandom(uint64_t seed)
    {
        v = 4101842887655102017ULL;
        w = 1;

        u = v ^ seed; NextUInt64();
        v = u;        NextUInt64();
        w = v;        NextUInt64();
    }

    uint64_t    qc_seed;
    uint8_t    qc_ivec[32];
    uint16_t   qc_seed_idx;
    uint16_t   qc_ivec_idx;

    public:
    void quake_decrypt_init(uint8_t *key, uint64_t seed2) {
        memcpy(qc_ivec, key, sizeof(qc_ivec));
        qc_seed = *(uint64_t *)(key);
        qc_seed_idx = 0;
        qc_ivec_idx = 0;
        NrRandom(qc_seed ^ seed2);
    }

    QCDecrypt(uint8_t *key, uint64_t seed2) {
        quake_decrypt_init(key, seed2);
    }
    int quake_decrypt(uint8_t *data, uint8_t* output, int size) {
        for(int i = 0; i < size; i++) {
            uint8_t c = data[i];
            uint8_t old = qc_ivec[qc_ivec_idx];
            qc_ivec[qc_ivec_idx] = c;
            output[i] = (qc_seed_idx ? 0 : qc_seed) ^ c ^ old;
            qc_ivec_idx = (qc_ivec_idx + 1) & 0x1f;
            if(++qc_seed_idx == 8) {
                qc_seed = NextUInt64();
                qc_seed_idx = 0;
            }
        }
        return size;
    }
};

using namespace std;
std::string string_to_hex(const std::string& input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}
int main(int argc, char *argv[]) {
    (void)argc;
    char* fname = argv[1];
    cout << "Opening file:" << fname << endl;
    ifstream ifile(fname, ios::in | ios::binary);
    ifile.seekg(-40, ios_base::end);
    array<uint8_t, 40> key;
    ifile.read((char*)key.data(), key.size());
    cout << "key:" << string_to_hex((char*)key.data()) << endl;

    array<uint8_t, 0x4000> haystack;
    ifile.seekg(-0x4000, ios_base::end);
    ifile.read((char*)haystack.data(), haystack.size());
    array<uint8_t, 4> needle = {'P', 'K', 0x06, 0x06};

    auto it = std::search (haystack.begin(), haystack.end(), needle.begin(), needle.end());

    main_header_t header;
    memcpy((char*)&header, it, sizeof(header));

    cout << "sizeof entry header " << hex << sizeof(entry_header_t) << dec << endl;
    cout << "Header has " << header.central_entries << " entries"<<endl;

    array<uint8_t, 0x400> buffer;
    ifile.seekg(header.central_offset, ios_base::beg);
    ifile.read((char*)buffer.data(), buffer.size());



    std::atomic_uint32_t progress;
    std::atomic_bool found;

#pragma omp parallel
    {
        
        unsigned int percent = 0;
        int nthreads, tid;
        uint32_t seed;
        tid = omp_get_thread_num();
        nthreads = omp_get_num_threads();
        uint32_t per_thread = 0xFFFFFFFF / nthreads;
        uint32_t from = per_thread * tid;
        //from = 0x412e2206;
        //from = 0x631A2028;
        //from = 0x0F10C856F;
        //from = 0x0;
        //from = 8041504;
        //
        //from = 0x6F01BCCC;
        //from = 0x6F00BCCC;
        //from = 0x6EA00000;
        uint32_t to = (tid == nthreads - 1 ? 0xFFFFFFFF : per_thread * (tid+1));

#pragma omp critical
                {
                    cout << "Thread " << dec << tid;
                    cout << " From   " << hex << from;
                    cout << " To   " << hex << to << dec << endl;
                }

        QCDecrypt qc(key.data(), 0);
        array<char, 0x4000> tmp;
        for (seed = from; !found && seed < to; ++seed) {
            qc.quake_decrypt_init(key.data(), seed);

            bool all_match = true;
            unsigned int index = 0;
            for (int i = 0; i < 3; ++i) {
                entry_header_t *encrypted_entry;
                entry_header_t entry;
                encrypted_entry = (entry_header_t*)(buffer.data() + index);
                index += sizeof(entry_header_t);
                qc.quake_decrypt((uint8_t*)encrypted_entry, (uint8_t*)&entry, sizeof(entry_header_t));
                if (entry.name_len + entry.extra_len + entry.comm_len + index > buffer.size()) {
                    all_match = false;
                    break;
                }
                char *name = (char*)(buffer.data() + index);
                qc.quake_decrypt((uint8_t*)name, (uint8_t*)tmp.data(), entry.name_len);
                if (!all_of(tmp.data(), tmp.data()+entry.name_len, [] (char c) -> bool {return isprint(c);})) {
                    all_match = false;
                    break;
                }
                index += entry.name_len;
                index += entry.extra_len;
                index += entry.comm_len;
#if 0
                name = (char*)tmp.data();
#pragma omp critical
                {
                cout <<"entry:"<<  i << endl;
                cout <<"name:"<< string_to_hex(name) << endl;
                cout <<"name:"<< name << endl;
                cout <<"name_len:"<<  entry.name_len << endl;
                cout <<"magic:"<<  hex << entry.magic << dec << endl;
                }
#endif
                if (entry.magic != entry_magic) {
                    all_match = false;
                    break;
                }
            }

            if (all_match) {
                found = true;
#pragma omp critical
                {
                    cout << "++++++++++++++++++++++++++++++++" << endl;
                    cout << "Found seed: " << hex << seed<< dec << endl;
                    cout << "++++++++++++++++++++++++++++++++" << endl;
                }
                break;
            }
            ++progress;
            if ( tid == 0 && percent < progress / (0xFFFFFFFF / 100)) {
#pragma omp critical
                cout << "Progress: " << dec << progress / (0xFFFFFFFF / 100) << "%" << endl;
                percent ++;
            }
        }
#pragma omp critical
        {
            cout << "Thread " << dec << tid;
            cout << " Done   " << endl;
        }

    }

}
