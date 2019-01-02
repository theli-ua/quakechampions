typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

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

typedef struct {
    uint64_t u, v, w;
    uint64_t    qc_seed;
    uint8_t    qc_ivec[32];
    uint16_t   qc_seed_idx;
    uint16_t   qc_ivec_idx;
} qc_state_t;


uint64_t NextUInt64(qc_state_t *qc_state)
{
    qc_state->u = qc_state->u * 2862933555777941757UL + 7046029254386353087UL;
    qc_state->v ^= qc_state->v >> 17; qc_state->v ^= qc_state->v << 31; qc_state->v ^= qc_state->v >> 8;
    qc_state->w = 4294957665U * (qc_state->w & 0xffffffff) + (qc_state->w >> 32);
    uint64_t x = qc_state->u ^ (qc_state->u << 21); x ^= x >> 35; x ^= x << 4;
    return (x + qc_state->v) ^ qc_state->w;
}

void NrRandom(qc_state_t *qc_state, uint64_t seed)
{
    qc_state->v = 4101842887655102017UL;
    qc_state->w = 1;

    qc_state->u = qc_state->v ^ seed; NextUInt64(qc_state);
    qc_state->v = qc_state->u;        NextUInt64(qc_state);
    qc_state->w = qc_state->v;        NextUInt64(qc_state);
}

void quake_decrypt_init(qc_state_t *qc_state, const uint8_t key[32], uint64_t seed2) {
    for (int i=0; i < sizeof(qc_state->qc_ivec); ++i) {
        qc_state->qc_ivec[i] = key[i];
    }
    /*qc_state = key;*/
    qc_state->qc_seed = *(const uint64_t *)(key);
    qc_state->qc_seed_idx = 0;
    qc_state->qc_ivec_idx = 0;
    NrRandom(qc_state, qc_state->qc_seed ^ seed2);
}

int quake_decrypt(qc_state_t *qc_state, __const uint8_t *data, uint8_t* output, int size) {
    for(int i = 0; i < size; i++) {
        uint8_t c = data[i];
        uint8_t old = qc_state->qc_ivec[qc_state->qc_ivec_idx];
        qc_state->qc_ivec[qc_state->qc_ivec_idx] = c;
        if(output) 
            output[i] = (qc_state->qc_seed_idx ? 0 : qc_state->qc_seed) ^ c ^ old;
        qc_state->qc_ivec_idx = (qc_state->qc_ivec_idx + 1) & 0x1f;
        if(++qc_state->qc_seed_idx == 8) {
            qc_state->qc_seed = NextUInt64(qc_state);
            qc_state->qc_seed_idx = 0;
        }
    }
    return size;
}

#define ENTRIES_TO_CHECK 8
#define BUFFER_SIZE 4000
#define ENTRY_MAGIC 0x02014b50

#define SEEDS_PER_ITERATION 128
__kernel void find_seed(__global const uint8_t _key[32], __global const uint8_t _input[BUFFER_SIZE], __global uint8_t *output_seeds) {
 
    int global_id = get_global_id(0);
    int local_id = get_local_id(0);
    uint64_t seed = global_id * SEEDS_PER_ITERATION;
    uint8_t key[32];
    for (int x=0; x < sizeof(key); x++) {
        key[x] = _key[x];
    }
    uint8_t input[BUFFER_SIZE];
    for (int x=0; x < sizeof(input); x++) {
        input[x] = _input[x];
    }
 
    for (int i = 0; i < SEEDS_PER_ITERATION; ++i) {
        qc_state_t qc_state;
        unsigned int pos = 0;
        int matched = 0;
#if 1
        quake_decrypt_init(&qc_state, key, seed + i);
        for (matched = 0; matched < ENTRIES_TO_CHECK; ++matched) {
            const entry_header_t *entry = (const entry_header_t*)(input + pos);
            pos += sizeof(entry_header_t);
            entry_header_t plain_entry;
            quake_decrypt(&qc_state, entry, &plain_entry, sizeof(entry_header_t));
            if (plain_entry.magic != ENTRY_MAGIC) {
                /*matched = 2;*/
                break;
            }
            if (plain_entry.name_len + pos + plain_entry.extra_len + plain_entry.comm_len > BUFFER_SIZE)
                break;
            quake_decrypt(&qc_state, input + pos, NULL, plain_entry.name_len);
            pos += plain_entry.name_len;
            pos += plain_entry.extra_len;
            pos += plain_entry.comm_len;
        }
#endif
        /*output_seeds[seed / 8] = 1;*/
        /*output_seeds[seed / 8] = 0xFF;*/
        /*if (seed == 0x6F01BCCC) {*/
        /*output_seeds[seed / 8] = (1 << (seed % 8));*/
        /*}*/
        /*output_seeds[seed / 8] = (1 << (seed % 8));*/
        if (matched == ENTRIES_TO_CHECK) {
            output_seeds[(seed + i) / 8] = (1 << ((seed + i) % 8));
        }
    }
}
