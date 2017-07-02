#include <stdint.h>
#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

#include <getopt.h>

#include "vbyte.h"

#include "simdbinarypacking.h"
#include "streamvariablebyte.h"
#include "varintgb.h"
#include "timestamp.hpp"

#define DBG(x) {}

extern "C" {
// missing prototype in SIMDCompressionLib
uint64_t svb_decode(uint32_t *out, uint8_t *in, int delta, int type);

}

static void usage(char const *progname)
{
    fprintf (stderr, "Usage: %s [-stC] \n", progname);
    fprintf (stderr, "\t-t|--test: perform codecs testing before benchmarking.\n");
    fprintf (stderr, "\t-s|--shift: perform benchamarking with range shifting.\n");
    fprintf (stderr, "\t-C|--cvs: print result in cvs-like format.\n");
    fprintf (stderr, "\t-i|--iters: iterations count[default in 10000].\n");
    fprintf (stderr, "\t-h|--help: show this usage.\n");
    fprintf (stderr, "\n");
}


using namespace SIMDCompressionLib;

    
static int svb_find_d1(uint8_t *in, uint32_t prev, uint32_t key, uint32_t *presult)
{
    uint32_t count = *(uint32_t *)in; // first 4 bytes is number of ints
    if (count == 0)
        return 0;
    
    uint8_t *keyPtr = in + 4;            // full list of keys is next
    uint32_t keyLen = ((count + 3) / 4); // 2-bits per key (rounded up)
    uint8_t *dataPtr = keyPtr + keyLen;  // data starts at end of keys
    
    return svb_find_avx_d1_init(keyPtr, dataPtr, count, prev, key, presult);
}

static std::vector<uint32_t> generate_random_list_grow( size_t count )
{
    std::vector<uint32_t> ret(count);
    uint32_t pn = 961799479U;
    for( size_t i = 0; i < count; ++i )
    {
        pn = (pn * 101513) + 198491317;
        ret[i] = pn;
        //pn *= 353868019;
    }
    
    std::sort(ret.begin(), ret.end());
    return ret;
}

static void shift_left( std::vector<uint32_t> &vec, uint32_t by )
{
    if( by )
    {
        for( uint32_t &v : vec )
            v = v >> by;
    }
}

static void compare( std::vector<uint32_t> const &lhs, std::vector<uint32_t> const &rhs )
{
    if( lhs.size() != rhs.size() )
        throw std::runtime_error("[compare] vector different sizes!");
    
    DBG( std::cout << "LIST: " );
    for( size_t i = 0, sz = lhs.size(); i < sz; ++i )
    {
        if( lhs[i] != rhs[i] )
            throw std::runtime_error("[compare] nth=" + std::to_string(i) +" elements not match!");
        
        DBG( std::cout << lhs[i] << " " );
    }
    
    DBG( std::cout << std::endl );
}

static double calc_ratio( size_t raw_size, size_t compr_size )
{
    return compr_size / double(raw_size);
}


static std::vector<uint8_t> encode_svb( std::vector<uint32_t> const& src, int delta )
{
    std::vector<uint8_t> encode_buffer(src.size() * sizeof(uint32_t));
    uint64_t compr_sz_bytes = svb_encode(&encode_buffer[0], &src[0], src.size(), delta, 1);
    // double copy for check!
    std::vector<uint8_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + compr_sz_bytes);
    
    return encoded_cpy;
}

static void test_svb( std::vector<uint32_t> const &src )
{
    int const delta = 1;
    
    std::vector<uint8_t> encode_buffer(src.size() * sizeof(uint32_t));
    
    uint64_t compr_sz_bytes = svb_encode(&encode_buffer[0], &src[0], src.size(), delta, 1);
    
    std::cout << "Stream VByte input bytes=" << encode_buffer.size() << " compressed bytes=" << compr_sz_bytes << " ratio=" << calc_ratio(encode_buffer.size(), compr_sz_bytes) << std::endl;
    
    std::vector<uint8_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + compr_sz_bytes);
    
    std::vector<uint32_t> decoded(src.size());
    
    uint64_t decoded_sz = svb_decode(&decoded[0], &encoded_cpy[0], delta, 1);
    
    std::cout << "decoded bytes=" << decoded_sz << std::endl;
    
    compare(src, decoded);
}

static void test_varintgb( std::vector<uint32_t> const &src )
{
    SIMDCompressionLib::VarIntGB<true> compressor;
    
    std::vector<uint8_t> encode_buffer(src.size() * sizeof(uint32_t));
    
    size_t encoded_sz = 0;
    
    compressor.encodeToByteArray((uint32_t *)&src[0], src.size(), &encode_buffer[0], encoded_sz);
    
    std::cout << "VarIntGB input bytes=" << encode_buffer.size() << " compressed bytes=" << encoded_sz << " ratio=" << calc_ratio(encode_buffer.size(), encoded_sz) << std::endl;
    
    
    std::vector<uint8_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + encoded_sz);
    
    std::vector<uint32_t> decoded(src.size());
    
    size_t decoded_sz = 0;
    
    compressor.decodeFromByteArray(&encoded_cpy[0], encoded_sz, &decoded[0], decoded_sz);
    
    
    std::cout << "decoded bytes=" << decoded_sz << std::endl;
    
    compare(src, decoded);
    
}

static void test_msk_vbyte( std::vector<uint32_t> const &src )
{
    std::vector<uint8_t> encode_buffer(src.size() * sizeof(uint32_t));
    
    size_t encoded_sz = vbyte_compress_sorted32(&src[0], &encode_buffer[0], 0, src.size());
    
    std::cout << "Masked VByte input bytes=" << encode_buffer.size() << " compressed bytes=" << encoded_sz << " ratio=" << calc_ratio(encode_buffer.size(), encoded_sz) << std::endl;
    
    std::vector<uint8_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + encoded_sz);
    
    std::vector<uint32_t> decoded(src.size());
    
    size_t decoded_sz = vbyte_uncompress_sorted32(&encoded_cpy[0], &decoded[0], 0, src.size());
    
    
    std::cout << "decoded bytes=" << decoded_sz << std::endl;
    
    compare(src, decoded);
}


static size_t find_lower_bound_bp128( uint32_t const *in, size_t const count, uint32_t bit, uint32_t target, uint32_t *presult )
{
    __m128i init = _mm_set1_epi32(0);
    
    
    size_t index = 0;
    
    uint32_t const block_sz = (bit * 128) / 32;
    
    for( uint32_t i = 0; i < count / 128; ++i, in += block_sz )
    {
        size_t idx = simdsearchd1(&init, (const __m128i *)in, bit, target, presult);
        
        index += idx;
        
        
        if( idx < 128 )
        {
            break;
        }
    }
    
    return index;
}

static void test_bp128_raw( std::vector<uint32_t> const &src )
{
    std::vector<uint32_t> encode_buffer(src.size() * 4); // 4 * N should be enough for all  schemes
    typedef SIMDBitPackingHelpers<RegularDeltaSIMD> packer_t;
    
    // detect bit max bit count
    uint32_t const bit = maxbits(src.begin(), src.end());
    
    packer_t::ipack(src.data(), src.size(), &encode_buffer[0], bit);
    
    // copy to another array
    size_t const packed_sz = (bit * src.size()) / 8;
    
    std::cout << "BP128R[" << bit << "] input bytes=" << src.size() * sizeof(uint32_t) << " compressed bytes=" << packed_sz << " ratio=" << calc_ratio(src.size() * sizeof(uint32_t), packed_sz) << std::endl;
    
    std::vector<uint32_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + packed_sz / 4);
    
    std::vector<uint32_t> decoded(src.size());
    
    packer_t::iunpack(encoded_cpy.data(), src.size(), decoded.data(), bit);
    
    
    compare(src, decoded);
    
    // testing search procedure
    
    DBG
    (
        __m128i init = _mm_set1_epi32(0);
        uint32_t result = 0;
        size_t index = find_lower_bound_bp128(&encoded_cpy[0], src.size(), bit, src[129], &result);
        std::cout << "found value=" << result << " at pos=" << index << " orig=" << src[129] << std::endl;
     );
}

static void test_bp128( std::vector<uint32_t> const &src )
{
    SIMDBinaryPacking<SIMDBlockPacker<RegularDeltaSIMD, true>> packer;
 
    std::vector<uint32_t> encode_buffer(src.size() * 4); // 4 * N should be enough for all  schemes
    
    size_t encoded_sz = 0;
    packer.encodeArray((uint32_t*)src.data(), src.size(), &encode_buffer[0], encoded_sz);
    
    std::cout << "BP128 input bytes=" << src.size() * sizeof(uint32_t) << " compressed bytes=" << encoded_sz * sizeof(uint32_t) << " ratio=" << calc_ratio(src.size(), encoded_sz) << std::endl;
    
    std::vector<uint32_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + encoded_sz);
    
    std::vector<uint32_t> decoded(src.size());
    
    size_t decoded_sz = 0;
    
    packer.decodeArray(encoded_cpy.data(), encoded_sz, &decoded[0], decoded_sz);
    
    compare(src, decoded);
}

static uint32_t bench_linear_scan
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *
)
{
    uint32_t checksum = 0;
    for( uint32_t target : targets )
    {
        auto it = std::find(src.begin(), src.end(), target);
        
        if( it == src.end() )
            throw std::runtime_error("[bench_linear_scan] value not found!");
        
        checksum += *it;
        
    }
    
    return checksum;
}

static uint32_t bench_binary_scan
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *
)
{
    uint32_t checksum = 0;
    for( uint32_t target : targets )
    {
        auto it = std::lower_bound(src.begin(), src.end(), target);
        
        if( it == src.end() || *it != target )
            throw std::runtime_error("[bench_binary_scan] value not found!");
        
        checksum += *it;
        
    }
    
    return checksum;
}

static uint32_t bench_svb_scan
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *svb_enc
)
{
    uint32_t checksum = 0;
    
    uint8_t *in = &*svb_enc->begin();
    
    uint32_t count = *(uint32_t *)in; // first 4 bytes is number of ints
    
    uint8_t *keyPtr = in + 4;            // full list of keys is next
    uint32_t keyLen = ((count + 3) / 4); // 2-bits per key (rounded up)
    uint8_t *dataPtr = keyPtr + keyLen;  // data starts at end of keys
    
    for( uint32_t target : targets )
    {
        uint32_t result = 0;
        int idx = svb_find_avx_d1_init(keyPtr, dataPtr, count, 0, target, &result);
        
        if( result != target )
            throw std::runtime_error("[bench_svb_scan] value not found!");
        
        checksum += result;
        
    }
    
    return checksum;
}

static uint32_t bench_svb_scan2
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *svb_enc
)
{
    // perform unpack and binary search
    
    uint32_t checksum = 0;
    
    for( uint32_t target : targets )
    {
    
        std::vector<uint32_t> decoded(src.size());
    
        uint64_t decoded_sz = svb_decode(&decoded[0], &*svb_enc->begin(), 1, 1);
        
        
        
        auto it = std::lower_bound(src.begin(), src.end(), target);
        
        if( it == src.end() || *it != target )
            throw std::runtime_error("[bench_svb_scan2] value not found!");
        
        checksum += *it;
        
    }
    
    return checksum;
}


static uint32_t bench_varintgb_scan
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *svb_enc
)
{
    SIMDCompressionLib::VarIntGB<true> compressor; // belive in harmless ctor()
    
    uint32_t const *addr = (uint32_t const*)&*svb_enc->begin();
    
    size_t const count = svb_enc->size();
    
    uint32_t checksum = 0;
    
    for( uint32_t target : targets )
    {
        uint32_t result = 0;
        size_t idx = compressor.findLowerBound(addr, count, target, &result);
        
        if( result != target )
            throw std::runtime_error("[bench_svb_scan] value not found!");
        
        checksum += result;
        
    }
    
    return checksum;
}

static uint32_t bench_msk_vbyte_scan
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *svb_enc
)
{
    uint8_t const *addr = &*svb_enc->begin();
    
    size_t const count = svb_enc->size();
    
    uint32_t checksum = 0;
    
    for( uint32_t target : targets )
    {
        uint32_t result = 0;
        size_t idx = vbyte_search_lower_bound_sorted32(addr, count, target, 0, &result);
        
        if( result != target )
            throw std::runtime_error("[bench_msk_vbyte_scan] value not found!");
        
        checksum += result;
        
    }
    
    return checksum;
}


static uint32_t bits_for_bench_bp128r_scan = 0;

static uint32_t bench_bp128r_scan
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::vector<uint8_t>        *svb_enc
)
{
    uint32_t const *addr = (uint32_t const *)&*svb_enc->begin();
    
    size_t const count = src.size();
    
    uint32_t checksum = 0;
    
    
    for( uint32_t target : targets )
    {
        uint32_t result = 0;
        
        find_lower_bound_bp128(addr, count, bits_for_bench_bp128r_scan, target, &result);
        
        if( result != target )
            throw std::runtime_error("[bench_bp128r_scan] value not found!");
        
        checksum += result;
        
    }
    
    return checksum;
}


static bool g_cvs_fmt = false;
static uint32_t iterations_cnt = 10000;

static void bench
(
    std::vector<uint32_t> const &src,
    std::vector<uint32_t> const &targets,
    std::function< uint32_t ( std::vector<uint32_t> const &, std::vector<uint32_t> const &, std::vector<uint8_t> * ) > f,
    char const *name,
    std::vector<uint8_t> *svb_enc = nullptr
)
{
    uint32_t checksum = 0;
    Timestamp ts;
    
    for( uint32_t i = 0; i < iterations_cnt; ++i )
    {
        checksum += f(src, targets, svb_enc);
    }
    
    auto elapsed = ts.elapsed_millis();
    
    if( !g_cvs_fmt )
        std::cout << "+++ bench done for " << name << " elapsed = " << elapsed << " cs = " << checksum;
    else
        std::cout << src.size() << ", " << name << ", " << elapsed;
    
    if( !g_cvs_fmt && svb_enc )
        std::cout << " packed = " << svb_enc->size();
    
    std::cout << std::endl;
}

static void bench_all(size_t count, uint32_t bits_shift )
{
    if( !g_cvs_fmt )
        std::cout   << ">>>>>>>> start benchmarking for bucket size: "
                    << count << " bitshift: " << bits_shift << std::endl;
    
    auto src( generate_random_list_grow(count) );
    
    shift_left(src, 5);
    
    // and generate targets
    
    auto targets(src);
    for( uint32_t i = 0; i < 10; ++i )
        std::random_shuffle(targets.begin(), targets.end());
    
    bench(src, targets, bench_linear_scan, "linear_scan");
    bench(src, targets, bench_binary_scan, "binary_scan");
    
    // encode src for svb
    auto svb_buffer( encode_svb(src, 1) );
    
    bench(src, targets, bench_svb_scan, "svb_scan", &svb_buffer);
    
    //bench(src, targets, bench_svb_scan2, "bench_svb_scan2", &svb_buffer);
    
    {
        SIMDCompressionLib::VarIntGB<true> compressor;
        
        std::vector<uint8_t> encode_buffer(src.size() * sizeof(uint32_t));
    
        size_t encoded_sz = 0;
    
        compressor.encodeToByteArray(&src[0], src.size(), &encode_buffer[0], encoded_sz);
        
        encode_buffer.resize(encoded_sz);
        
        bench(src, targets, bench_varintgb_scan, "varintgb_scan", &encode_buffer);
    }
    
    {
        std::vector<uint8_t> encode_buffer(src.size() * sizeof(uint32_t));
        
        size_t encoded_sz = vbyte_compress_sorted32(&src[0], &encode_buffer[0], 0, src.size());
        
        encode_buffer.resize(encoded_sz);
        
        bench(src, targets, bench_msk_vbyte_scan, "libvbyte_scan", &encode_buffer);
    }
    
    if( 0 == (count % 128) )
    {
        std::vector<uint32_t> encode_buffer(src.size() * 4); // 4 * N should be enough for all  schemes
        typedef SIMDBitPackingHelpers<RegularDeltaSIMD> packer_t;
        
        // detect bit max bit count
        uint32_t const bit = maxbits(src.begin(), src.end());
        
        packer_t::ipack(src.data(), src.size(), &encode_buffer[0], bit);
        
        // copy to another array
        size_t const packed_sz = (bit * src.size()) / 8;
        
        // HAACK!
        bits_for_bench_bp128r_scan = bit;
        
        //std::cout << "BP128R[" << bit << "] input bytes=" << src.size() * sizeof(uint32_t) << " compressed bytes=" << packed_sz << " ratio=" << calc_ratio(src.size() * sizeof(uint32_t), packed_sz) << std::endl;
        
        std::vector<uint32_t> encoded_cpy(encode_buffer.begin(), encode_buffer.begin() + packed_sz / 4);
        
        // copy to byte buffer due to iface limits
        
        std::vector<uint8_t> encoded_cpy_bytes(encoded_cpy.size() * sizeof(uint32_t));
        
        memcpy(&encoded_cpy_bytes[0], &encoded_cpy[0], encoded_cpy_bytes.size());
        
        bench(src, targets, bench_bp128r_scan, "bp128r_scan", &encoded_cpy_bytes);
    }
    
}

int main(int argc, char *argv[])
{
    char const *progname = argv[0];
    
    bool basic_tests = false;
    bool shifts = false;
    
    while (1)
    {
        static struct option long_options[] =
        {
            { "help",             0,                 NULL, 'h' },
            { "test",             0,                 NULL, 't' },
            { "shift",            0,                 NULL, 's' },
            { "cvs",              0,                 NULL, 'C' },
            { "iters",            required_argument, NULL, 'i' },
            { NULL,               0,                 NULL,  0  }
        };
        
        char c = getopt_long (argc, argv, "htsCi:", long_options, NULL);
        if (c == -1)
            break;
     
        switch (c)
        {
            case 't':
                basic_tests = true;
                break;
            case 's':
                shifts = true;
                break;
            case 'C':
                g_cvs_fmt = true;
                break;
            case 'i':
                iterations_cnt = atoi(optarg);
                break;
                
            case 'h':
            default:
                usage(progname);
                return 1;
        }
    }
    
    if( basic_tests )
    {
        auto src( generate_random_list_grow(512) );
        
        shift_left(src, 19);
        
        for( auto f : { test_svb, test_varintgb, test_msk_vbyte, test_bp128_raw } )
            f(src);
    }

    
    for( auto cnt : {128, 256, 512, 1024, 2048} )
    {
        if( shifts )
        {
            for( auto shift : {0, 8, 16, 24} )
                bench_all(cnt, shift);
        }
        else
            bench_all(cnt, 0);
    }
    
    
    return 0;
}
