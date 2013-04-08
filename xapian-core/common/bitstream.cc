/** @file bitstream.cc
 * @brief Classes to encode/decode a bitstream.
 */
/* Copyright (C) 2004,2005,2006,2008 Olly Betts
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <config.h>

#include "bitstream.h"

#include <xapian/types.h>

#include "omassert.h"

#include <cmath>
#include <vector>

using namespace std;

static const unsigned char flstab[256] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

// Highly optimised fls() implementation.
inline int my_fls(unsigned mask)
{
    int result = 0;
    if (mask >= 0x10000u) {
	mask >>= 16;
	result = 16;
    }
    if (mask >= 0x100u) {
	mask >>= 8;
	result += 8;
    }
    return result + flstab[mask];
}

static const unsigned char msb_lut[256] =
{
	0, 0, 1, 1, 2, 2, 2, 2, // 0000_0000 - 0000_0111
	3, 3, 3, 3, 3, 3, 3, 3, // 0000_1000 - 0000_1111
	4, 4, 4, 4, 4, 4, 4, 4, // 0001_0000 - 0001_0111
	4, 4, 4, 4, 4, 4, 4, 4, // 0001_1000 - 0001_1111
	5, 5, 5, 5, 5, 5, 5, 5, // 0010_0000 - 0010_0111
	5, 5, 5, 5, 5, 5, 5, 5, // 0010_1000 - 0010_1111
	5, 5, 5, 5, 5, 5, 5, 5, // 0011_0000 - 0011_0111
	5, 5, 5, 5, 5, 5, 5, 5, // 0011_1000 - 0011_1111

	6, 6, 6, 6, 6, 6, 6, 6, // 0100_0000 - 0100_0111
	6, 6, 6, 6, 6, 6, 6, 6, // 0100_1000 - 0100_1111
	6, 6, 6, 6, 6, 6, 6, 6, // 0101_0000 - 0101_0111
	6, 6, 6, 6, 6, 6, 6, 6, // 0101_1000 - 0101_1111
	6, 6, 6, 6, 6, 6, 6, 6, // 0110_0000 - 0110_0111
	6, 6, 6, 6, 6, 6, 6, 6, // 0110_1000 - 0110_1111
	6, 6, 6, 6, 6, 6, 6, 6, // 0111_0000 - 0111_0111
	6, 6, 6, 6, 6, 6, 6, 6, // 0111_1000 - 0111_1111

	7, 7, 7, 7, 7, 7, 7, 7, // 1000_0000 - 1000_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1000_1000 - 1000_1111
	7, 7, 7, 7, 7, 7, 7, 7, // 1001_0000 - 1001_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1001_1000 - 1001_1111
	7, 7, 7, 7, 7, 7, 7, 7, // 1010_0000 - 1010_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1010_1000 - 1010_1111
	7, 7, 7, 7, 7, 7, 7, 7, // 1011_0000 - 1011_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1011_1000 - 1011_1111

	7, 7, 7, 7, 7, 7, 7, 7, // 1100_0000 - 1100_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1100_1000 - 1100_1111
	7, 7, 7, 7, 7, 7, 7, 7, // 1101_0000 - 1101_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1101_1000 - 1101_1111
	7, 7, 7, 7, 7, 7, 7, 7, // 1110_0000 - 1110_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1110_1000 - 1110_1111
	7, 7, 7, 7, 7, 7, 7, 7, // 1111_0000 - 1111_0111
	7, 7, 7, 7, 7, 7, 7, 7, // 1111_1000 - 1111_1111
};

// Highly optimised highest_order_bit() implementation.
inline int highest_order_bit(int x)
{
    int byte;
    int byte_cnt;
    for (byte_cnt = sizeof(int) - 1; byte_cnt >= 0; byte_cnt--)
    {
        byte = (x >> (byte_cnt * 8)) & 0xff;
        if (byte != 0)
        {
            return msb_lut[byte] + (byte_cnt * 8);
        }
    }
    return 0;
}

namespace Xapian {

void
BitWriter::encode(size_t value, size_t outof)
{
    Assert(value < outof);
    size_t bits = my_fls(outof - 1);
    const size_t spare = (1 << bits) - outof;
    if (spare) {
	const size_t mid_start = (outof - spare) / 2;
	if (value >= mid_start + spare) {
	    value = (value - (mid_start + spare)) | (1 << (bits - 1));
	} else if (value >= mid_start) {
	    --bits;
	}
    }

    if (bits + n_bits > 32) {
	// We need to write more bits than there's empty room for in
	// the accumulator.  So we arrange to shift out 8 bits, then
	// adjust things so we're adding 8 fewer bits.
	Assert(bits <= 32);
	acc |= (value << n_bits);
	buf += char(acc);
	acc >>= 8;
	value >>= 8;
	bits -= 8;
    }
    acc |= (value << n_bits);
    n_bits += bits;
    while (n_bits >= 8) {
	buf += char(acc);
	acc >>= 8;
	n_bits -= 8;
    }
}

void
BitWriter::encode_interpolative(const vector<Xapian::termpos> &pos, int j, int k)
{
    while (j + 1 < k) {
	const size_t mid = (j + k) / 2;
	// Encode one out of (pos[k] - pos[j] + 1) values
	// (less some at either end because we must be able to fit
	// all the intervening pos in)
	const size_t outof = pos[k] - pos[j] + j - k + 1;
	const size_t lowest = pos[j] + mid - j;
	encode(pos[mid] - lowest, outof);
	encode_interpolative(pos, j, mid);
	j = mid;
    }
}

Xapian::termpos
BitReader::decode(Xapian::termpos outof, bool force)
{
    (void)force;
    Assert(!force && !di_current.is_initialized());
    size_t bits = my_fls(outof - 1);
    const size_t spare = (1 << bits) - outof;
    const size_t mid_start = (outof - spare) / 2;
    Xapian::termpos p;
    if (spare) {
	p = read_bits(bits - 1);
	if (p < mid_start) {
	    if (read_bits(1)) p += mid_start + spare;
	}
    } else {
	p = read_bits(bits);
    }
    Assert(p < outof);
    return p;
}

unsigned int
BitReader::read_bits(int count)
{
    unsigned int result;
    if (count > 25) {
	// If we need more than 25 bits, read in two goes to ensure that we
	// don't overflow acc.  This is a little more conservative than it
	// needs to be, but such large values will inevitably be rare (because
	// you can't fit very many of them into 2^32!)
	Assert(count <= 32);
	result = read_bits(16);
	return result | (read_bits(count - 16) << 16);
    }
    while (n_bits < count) {
	Assert(idx < buf.size());
	acc |= static_cast<unsigned char>(buf[idx++]) << n_bits;
	n_bits += 8;
    }
    result = acc & ((1u << count) - 1);
    acc >>= count;
    n_bits -= count;
    return result;
}

void
BitReader::decode_interpolative(vector<Xapian::termpos> & pos, int j, int k)
{
    Assert(!di_current.is_initialized());
    while (j + 1 < k) {
	const size_t mid = (j + k) / 2;
	// Decode one out of (pos[k] - pos[j] + 1) values
	// (less some at either end because we must be able to fit
	// all the intervening pos in)
	const size_t outof = pos[k] - pos[j] + j - k + 1;
	pos[mid] = decode(outof) + (pos[j] + mid - j);
	decode_interpolative(pos, j, mid);
	j = mid;
    }
}

void
BitReader::decode_interpolative(int j, int k, Xapian::termpos pos_j, Xapian::termpos pos_k)
{
    Assert(!di_current.is_initialized());
	di_stack.reserve(highest_order_bit(pos_k - pos_j) + 1);
    di_current.set(j, k, pos_j, pos_k);
}

Xapian::termpos
BitReader::decode_interpolative_next()
{
    Assert(di_current.is_initialized());
    while (!di_stack.empty() || di_current.is_next()) {
        if (di_current.is_next()) {
            di_stack.push_back(di_current);
            int mid = (di_current.j + di_current.k) / 2;
            int pos_mid = decode(di_current.pos_k - di_current.pos_j + di_current.j - di_current.k + 1, true) + (di_current.pos_j + mid - di_current.j);
            di_current.set(di_current.j, mid, di_current.pos_j, pos_mid);
        } else {
            Xapian::termpos pos_ret = di_current.pos_k;
            Assert(di_stack.empty() && !di_current.is_next()); //decode_interpolative_next called too many times
            di_current = di_stack[di_stack.size() - 1];
            di_stack.pop_back();
            int mid = (di_current.j + di_current.k) / 2;
            int pos_mid = pos_ret;
            di_current.set(mid, di_current.k, pos_mid, di_current.pos_k);
            if (di_stack.empty() && !di_current.is_next())
              di_current.set(0, 0, 0, 0);
            return pos_ret;
        }
    }
    Assert(false);
    return 0;
}

}
