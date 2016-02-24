/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include <assert.h>
#include <cstring>
#define RBITS( c, n )		( c & ( 0xFF >> (8 - n) ) )
#define LBITS( c, n )		( c >> (8 - n) )
#define MBITS( c, l, r )	( RBITS( c,l ) >> r )
#define RBITS16( c, n )		( c & ( 0xFFFFFFFF >> (16 - n) ) )
#define LBITS16( c, n )		( c >> (16 - n) )
#define MBITS16( c, l, r )	( RBITS16( c,l ) >> r )
#define RBITS32( c, n )		( c & ( 0xFFFFFFFF >> (32 - n) ) )
#define LBITS32( c, n )		( c >> (32 - n) )
#define MBITS32( c, l, r )	( RBITS32( c,l ) >> r )

#define RBITS64( c, n )		(n == 0 ? 0ULL : ( c & ( 0xFFFFFFFFFFFFFFFFULL >> (64 - n) ) ))
#define LBITS64( c, n )		( c >> (64 - n) )
#define MBITS64( c, l, r )	( RBITS64( c,l ) >> r )

#define BITN( c, n )		( (c >> n) & 0x1 )
#define FDIV2( v, p )		( ( v < 0 ) ? -( (-v) >> p ) : ( v >> p ) )

#define BTST_BUFF			1024 * 1024

#include <stdio.h>
#include <functional>
#include "../io/Reader.hh"
#include "../io/ioutil.hh"
#include "../vp8/util/vpx_config.hh"

void compute_md5(const char * filename, unsigned char *result);

/* -----------------------------------------------
	class to write arrays bitwise
	----------------------------------------------- */

class abitwriter
{
    unsigned char* data2;
public:
    uint64_t buf;
    int dsize;
    int adds;
    int cbyte2;
    int cbit2;
    bool fmem;
    int size_bound;
public:
    void debug() const;

    
    abitwriter( int size, int size_bound=0 );
	~abitwriter( void );
    
    unsigned char* partial_bytewise_flush() {
        if (__builtin_expect(bound_reached(), 0)) {
            return data2;
        }
        int partial_byte_bits = (64 - cbit2) & 7;
        uint64_t xbuf = htobe64(buf);
        uint32_t bytes_to_write = (64 - (cbit2 + partial_byte_bits)) / 8;
        uint32_t bits_to_write = (bytes_to_write << 3);
        memcpy(data2 + cbyte2, &xbuf, bytes_to_write);
        cbyte2 += bytes_to_write;
        if (bits_to_write > 63) {
            buf = 0;
        } else {
            buf <<= bits_to_write;
        }
        cbit2 += bits_to_write;
        return data2;
    }
    void flush_no_pad() {
        if (__builtin_expect(bound_reached(), 0)) {
            return;
        }
        assert(((64 - cbit2) & 7) == 0);
        buf = htobe64(buf);
        uint32_t bytes_to_write = (64 - cbit2) / 8;
        memcpy(data2 + cbyte2, &buf, bytes_to_write);
        cbyte2 += bytes_to_write;
        buf = 0;
        //assert(cbyte +1 == cbyte2 || cbyte == cbyte2 || cbyte == cbyte2 + 1 || cbyte == cbyte2 + 2 || cbyte == cbyte2 + 3);
        //assert(memcmp(data2, data, cbyte2) == 0);
        
        cbit2 = 64;
    }
    /* -----------------------------------------------
     writes n bits to abitwriter
     ----------------------------------------------- */
    
    void write( unsigned int val, int nbits )
    {

        int nbits2 = nbits;
        unsigned int val2 = val;
        assert(nbits <= 64);
        if ( __builtin_expect(cbyte2 > ( dsize - 16 ), false) ) {
            if (bound_reached()) {
                return;
            }
            if (adds < 4096 * 1024) {
                adds <<= 1;
            }
            int new_size = dsize + adds;
            unsigned char * tmp = (unsigned char*)custom_malloc(new_size);
            if ( tmp == NULL ) {
                error = true;
                custom_exit(ExitCode::MALLOCED_NULL);
                return;
            }
            memset(tmp + dsize, 0, adds);
            memcpy(tmp, data2, dsize);
            custom_free(data2);
            data2 = tmp;
            dsize = new_size;
        }

        // write data
        if ( nbits2 >= cbit2 ) {

            buf |= MBITS64(val2, nbits2, (nbits2-cbit2));
            nbits2 -= cbit2;
            cbit2 = 0;
            flush_no_pad();
        }
        if ( nbits2 > 0 ) {
            uint64_t tmp = (RBITS64(val2, nbits2));
            tmp <<= cbit2 - nbits2;
            buf |= tmp;
            cbit2 -= nbits2;
        }



    }
    void pad ( unsigned char fillbit ) {
        int offset = 1;
        while ((cbit2 & 7) && cbyte2 < size_bound) {
            write( (fillbit & offset) ? 1 : 0, 1 );
            offset <<= 1;
        }
        flush_no_pad();
    }
    unsigned char* getptr( void ) {
        // data is padded here
        pad( fillbit );
        flush_no_pad();
        // forbid freeing memory
        fmem = false;
        // realloc data
        return data2;
    }
    const unsigned char* peekptr( void ) {
        flush_no_pad();
        return data2;
    }
    uint8_t get_num_overhang_bits() {
        return 64 - cbit2;
    }
    bool bound_reached() const {
        return cbyte2 >= size_bound && size_bound;
    }
    uint8_t get_overhang_byte() const {
        assert(cbit2 > 56);
        uint64_t retval = buf;
        retval >>= 56;
        return (uint8_t) retval;
    }
    void reset_from_overhang_byte_and_num_bits(uint8_t overhang_byte,
                                               uint8_t num_bits) {
        memset(data2, 0, cbyte2);
        if (size_bound) {
            size_bound -=cbyte2;
        }
        cbyte2 = 0;
        buf = 0;
        buf = overhang_byte;
        buf <<= 56;
        cbit2 = 64 - num_bits;
    }
    void reset() {
        assert(no_remainder());
        reset_crystallized_bytes();
    }
    void reset_crystallized_bytes() {
        memset(data2, 0, cbyte2);
        if (size_bound) {
            size_bound -=cbyte2;
        }
        cbyte2 = 0;
    }
    int getpos( void ) const {
        return cbyte2;
    }
    bool no_remainder() const {
        return cbit2 == 64 || bound_reached();
    }
	bool error;	
	unsigned char fillbit;
	
};

/* -----------------------------------------------
	class to read arrays bitwise
	----------------------------------------------- */

class abitreader
{
public:
	abitreader( unsigned char* array, int size );
	~abitreader( void );
    unsigned int read( int nbits )
    {
        const unsigned int retval = read_internal( nbits );
        debug_writer.write( retval, nbits );
        return retval;
    }
    std::pair<uint8_t, uint8_t> overhang()
    {
        debug_writer.partial_bytewise_flush();
        debug_writer.reset_crystallized_bytes();
        return { debug_writer.get_num_overhang_bits(), debug_writer.get_overhang_byte() };
    }
private:
	unsigned int read_internal( int nbits ) {
        if (__builtin_expect(eof || !nbits, 0)) {
            return 0;
        }
        unsigned int bits_read = 0;
        unsigned int retval2 = 0;
        if (__builtin_expect(nbits >= cbit2, 0)) {
            bits_read = cbit2;
            retval2 = (RBITS64(buf, cbit2) << (nbits - bits_read)) & ((1 << nbits) - 1);
            int cur_nbits = nbits - bits_read;
            buf >>= bits_read;
            cbit2 -= bits_read;
            if (cbyte2 == lbyte && cbit2 == 0) {
                eof = true;
                return retval2;
            }
            if (__builtin_expect(lbyte - cbyte2 < (int)sizeof(buf), 0)) {
                int new_bytes = std::min((int)sizeof(buf), lbyte - cbyte2);
                memcpy(&buf, &data2[cbyte2], new_bytes);
                buf = htobe64(buf);
                buf >>= (sizeof(buf) - new_bytes) * 8;
                cbyte2 += new_bytes;
                cbit2 += new_bytes * 8;
            } else {
                memcpy(&buf, &data2[cbyte2], sizeof(buf));
                buf = htobe64(buf);
                cbyte2 += sizeof(buf);
                cbit2 += sizeof(buf) * 8;
            }
            if (cbyte2 == lbyte && cbit2 == 0) {
                eof = true;
            }
            if (cur_nbits) {
                if (cur_nbits <= cbit2) {
                    retval2 |= MBITS64(buf, cbit2, (cbit2-cur_nbits));
                    cbit2 -= cur_nbits;
                } else {
                    retval2 |= buf;
                    buf = 0;
                    cbit2 = 0;
                }
            }
        } else {
            retval2 = MBITS64(buf, cbit2, (cbit2-nbits));
            cbit2 -= nbits;
        }
        return retval2;
    }
public:
    bool remainder() {
        if (cbit2 & 7) {
            return 8 - (cbit2 &7);
        } return 0;
    }
	unsigned char unpad( unsigned char fillbit ) {
        if ((cbit2 & 7) == 0 || eof) return fillbit;
        else {
            char last_bit = read( 1 );
            fillbit = last_bit;
            int offset = 1;
            while (cbit2 & 7) {
                last_bit = read( 1 );
                fillbit |= (last_bit << offset);
                ++offset;
            }
            while(offset < 7) {
                fillbit |= (last_bit << offset);
                ++offset;
            }
        }
        return fillbit;
    }
	int getpos( void ) {
        return cbyte2 - 7 + ((64 - cbit2) >> 3);
    }
    uint64_t debug_peek(void) {
        uint64_t retval = 0;
        abitreader tmp(*this);
        bool had_remainder = false;
        while (tmp.remainder()) {
            had_remainder = true;
            retval = tmp.read(tmp.remainder());
        }
        for (int i = 0 ;i < (had_remainder ? 7 : 8);++i) {
            uint8_t a = tmp.read(8);
            retval |= a;
            retval <<= 8;
        }
        return retval;
    }
    bool eof;
private:
    unsigned char* data2;
    int cbyte2;
    int cbit2;
    uint64_t buf;
	int lbyte;
    abitwriter debug_writer;
};

/* -----------------------------------------------
	class to write arrays bytewise
	----------------------------------------------- */
extern void aligned_dealloc(unsigned char*);
extern unsigned char * aligned_alloc(size_t);

class abytewriter
{
public:
	abytewriter( int size );
	~abytewriter( void );	
	void write( unsigned char byte );
	void write_n( unsigned char* byte, int n );
	unsigned char* getptr_aligned( void );
	unsigned char* peekptr_aligned( void );
	int getpos( void );
	void reset( void );
	bool error;	
	
private:
	unsigned char* data;
	int dsize;
	int adds;
	int cbyte;
	bool fmem;
};


/* -----------------------------------------------
	class to read arrays bytewise
	----------------------------------------------- */

class abytereader
{
public:
	abytereader( unsigned char* array, int size );
	~abytereader( void );	
	int read( unsigned char* byte );
	int read_n( unsigned char* byte, int n );
	void seek( int pos );
	int getsize( void );
	int getpos( void );
	bool eof;	
	
private:
	unsigned char* data;
	int lbyte;
	int cbyte;
};


/* -----------------------------------------------
	class for input and output from file or memory
	----------------------------------------------- */

class ibytestream {
    Sirikata::DecoderReader* parent;
    unsigned int bytes_read;
public:
	unsigned char get_last_read() const {
        return last_read[1];
    }
	unsigned char get_penultimate_read() const {
        return last_read[0];
    }
    ibytestream(Sirikata::DecoderReader *p,
                unsigned int starting_byte_offset,
                const Sirikata::JpegAllocator<uint8_t> &alloc);
    unsigned int getsize() const {
        return bytes_read;
    }
    bool read_byte(unsigned char *output);
    unsigned int read(unsigned char *output, unsigned int size);
    // the biggest allowed huffman code (that may get damaged by truncation)
    unsigned char last_read[2];
};
class ibytestreamcopier : ibytestream{ // since we don't use virtual methods... must reimplement
    std::vector<uint8_t, Sirikata::JpegAllocator<uint8_t> > side_channel;
public:
    ibytestreamcopier(Sirikata::DecoderReader *p,
                      unsigned int starting_byte_offset,
                      unsigned int maximum_file_size,
                      const Sirikata::JpegAllocator<uint8_t> &alloc);
    unsigned int getsize() const {
        return ibytestream::getsize();
    }
    unsigned int get_last_read() const {
        return ibytestream::get_last_read();
    }
    unsigned int get_penultimate_read() const {
        return ibytestream::get_penultimate_read();
    }

    bool read_byte(unsigned char *output);
    unsigned int read(unsigned char *output, unsigned int size);

    const std::vector<uint8_t,
                      Sirikata::JpegAllocator<uint8_t> >&get_read_data() const {
        return side_channel;
    }
    std::vector<uint8_t, Sirikata::JpegAllocator<uint8_t> >&mutate_read_data() {
        return side_channel;
    }
};

class bounded_iostream
{
    enum {
        buffer_size = 65536
    };
    uint8_t buffer[buffer_size];
    uint32_t buffer_position;
    Sirikata::DecoderWriter *parent;
    unsigned int byte_bound;
    unsigned int byte_position;
    Sirikata::JpegError err;
    std::function<void(Sirikata::DecoderWriter*, size_t)> size_callback;
public:
	bounded_iostream( Sirikata::DecoderWriter * parent,
                      const std::function<void(Sirikata::DecoderWriter*, size_t)> &size_callback,
                      const Sirikata::JpegAllocator<uint8_t> &alloc);
	~bounded_iostream( void );
    void call_size_callback(size_t size);
    bool chkerr();
    unsigned int getsize();
    unsigned int bytes_written()const {
        return byte_position;
    }
    void set_bound(size_t bound); // bound of zero = fine
    size_t get_bound() const {
        return byte_bound;
    }
    bool has_reached_bound() const {
        return byte_bound && byte_position == byte_bound;
    }
    unsigned int write_no_buffer( const void* from, size_t bytes_to_write );
    unsigned int write_byte(uint8_t byte) {
        assert(buffer_position < buffer_size && "Full buffer wasn't flushed");
        buffer[buffer_position++] = byte;
        if (__builtin_expect(buffer_position == buffer_size, 0)) {
            buffer_position = 0;
            write_no_buffer(buffer, buffer_size);
        }
        return 1;
    }
    unsigned int write(const void *from, unsigned int nbytes) {
        size_t bytes_to_write = nbytes;
        if (__builtin_expect(nbytes + buffer_position > buffer_size, 0)) {
            if (buffer_position) {
                write_no_buffer(buffer, buffer_position);
                buffer_position = 0;
            }
            if (bytes_to_write < 64) {
                memcpy(buffer + buffer_position, from, bytes_to_write);
                buffer_position += bytes_to_write;
            } else {
                return write_no_buffer(from, bytes_to_write);
            }
        } else {
            memcpy(buffer + buffer_position, from, bytes_to_write);
            buffer_position += bytes_to_write;
            if (__builtin_expect(buffer_position == buffer_size, 0)) {
                 buffer_position = 0;
                write_no_buffer(buffer, buffer_size);
            }
        }
        return bytes_to_write;
    }
    void flush();
    void close();
};
