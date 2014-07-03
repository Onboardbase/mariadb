/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
/*
COPYING CONDITIONS NOTICE:

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation, and provided that the
  following conditions are met:

      * Redistributions of source code must retain this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below).

      * Redistributions in binary form must reproduce this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below) in the documentation and/or other materials
        provided with the distribution.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.

COPYRIGHT NOTICE:

  TokuDB, Tokutek Fractal Tree Indexing Library.
  Copyright (C) 2007-2013 Tokutek, Inc.

DISCLAIMER:

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

UNIVERSITY PATENT NOTICE:

  The technology is licensed by the Massachusetts Institute of
  Technology, Rutgers State University of New Jersey, and the Research
  Foundation of State University of New York at Stony Brook under
  United States of America Serial No. 11/760379 and to the patents
  and/or patent applications resulting from it.

PATENT MARKING NOTICE:

  This software is covered by US Patent No. 8,185,551.
  This software is covered by US Patent No. 8,489,638.

PATENT RIGHTS GRANT:

  "THIS IMPLEMENTATION" means the copyrightable works distributed by
  Tokutek as part of the Fractal Tree project.

  "PATENT CLAIMS" means the claims of patents that are owned or
  licensable by Tokutek, both currently or in the future; and that in
  the absence of this license would be infringed by THIS
  IMPLEMENTATION or by using or running THIS IMPLEMENTATION.

  "PATENT CHALLENGE" shall mean a challenge to the validity,
  patentability, enforceability and/or non-infringement of any of the
  PATENT CLAIMS or otherwise opposing any of the PATENT CLAIMS.

  Tokutek hereby grants to you, for the term and geographical scope of
  the PATENT CLAIMS, a non-exclusive, no-charge, royalty-free,
  irrevocable (except as stated in this section) patent license to
  make, have made, use, offer to sell, sell, import, transfer, and
  otherwise run, modify, and propagate the contents of THIS
  IMPLEMENTATION, where such license applies only to the PATENT
  CLAIMS.  This grant does not include claims that would be infringed
  only as a consequence of further modifications of THIS
  IMPLEMENTATION.  If you or your agent or licensee institute or order
  or agree to the institution of patent litigation against any entity
  (including a cross-claim or counterclaim in a lawsuit) alleging that
  THIS IMPLEMENTATION constitutes direct or contributory patent
  infringement, or inducement of patent infringement, then any rights
  granted to you under this License shall terminate as of the date
  such litigation is filed.  If you or your agent or exclusive
  licensee institute or order or agree to the institution of a PATENT
  CHALLENGE, then Tokutek may terminate any rights granted to you
  under this License.
*/

#ident "Copyright (c) 2009-2013 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
#ident "$Id$"

#include <string>
#include <cstring>

#include "portability/memory.h"
#include "portability/toku_assert.h"
#include "portability/toku_stdint.h"
#include "portability/toku_stdlib.h"

#include "ft/serialize/block_allocator.h"
#include "ft/serialize/block_allocator_strategy.h"

#if 0
#define VALIDATE() validate()
#else
#define VALIDATE()
#endif

void block_allocator::create(uint64_t reserve_at_beginning, uint64_t alignment) {
    // the alignment must be at least 512 and aligned with 512 to work with direct I/O
    assert(alignment >= 512 && (alignment % 512) == 0);

    _reserve_at_beginning = reserve_at_beginning;
    _alignment = alignment;
    _n_blocks = 0;
    _blocks_array_size = 1;
    XMALLOC_N(_blocks_array_size, _blocks_array);
    _n_bytes_in_use = reserve_at_beginning;
    _strategy = BA_STRATEGY_FIRST_FIT;

    VALIDATE();
}

void block_allocator::destroy() {
    toku_free(_blocks_array);
}

void block_allocator::set_strategy(enum allocation_strategy strategy) {
    _strategy = strategy;
}

void block_allocator::grow_blocks_array_by(uint64_t n_to_add) {
    if (_n_blocks + n_to_add > _blocks_array_size) {
        uint64_t new_size = _n_blocks + n_to_add;
        uint64_t at_least = _blocks_array_size * 2;
        if (at_least > new_size) {
            new_size = at_least;
        }
        _blocks_array_size = new_size;
        XREALLOC_N(_blocks_array_size, _blocks_array);
    }
}

void block_allocator::grow_blocks_array() {
    grow_blocks_array_by(1);
}

void block_allocator::merge_blockpairs_into(uint64_t d, struct blockpair dst[],
                                            uint64_t s, const struct blockpair src[])
{
    uint64_t tail = d+s;
    while (d > 0 && s > 0) {
        struct blockpair       *dp = &dst[d - 1];
        struct blockpair const *sp = &src[s - 1];
        struct blockpair       *tp = &dst[tail - 1];
        assert(tail > 0);
        if (dp->offset > sp->offset) {
            *tp = *dp;
            d--;
            tail--;
        } else {
            *tp = *sp;
            s--;
            tail--;
        }
    }
    while (d > 0) {
        struct blockpair *dp = &dst[d - 1];
        struct blockpair *tp = &dst[tail - 1];
        *tp = *dp;
        d--;
        tail--;
    }
    while (s > 0) {
        struct blockpair const *sp = &src[s - 1];
        struct blockpair       *tp = &dst[tail - 1];
        *tp = *sp;
        s--;
        tail--;
    }
}

int block_allocator::compare_blockpairs(const void *av, const void *bv) {
    const struct blockpair *a = (const struct blockpair *) av;
    const struct blockpair *b = (const struct blockpair *) bv;
    if (a->offset < b->offset) {
        return -1;
    } else if (a->offset > b->offset) {
        return 1;
    } else {
        return 0;
    }
}

// See the documentation in block_allocator.h
void block_allocator::alloc_blocks_at(uint64_t n_blocks, struct blockpair pairs[]) {
    VALIDATE();
    qsort(pairs, n_blocks, sizeof(*pairs), compare_blockpairs);
    for (uint64_t i = 0; i < n_blocks; i++) {
        assert(pairs[i].offset >= _reserve_at_beginning);
        assert(pairs[i].offset % _alignment == 0);
        _n_bytes_in_use += pairs[i].size;
        // Allocator does not support size 0 blocks. See block_allocator_free_block.
        invariant(pairs[i].size > 0);
    }
    grow_blocks_array_by(n_blocks);
    merge_blockpairs_into(_n_blocks, _blocks_array, n_blocks, pairs);
    _n_blocks += n_blocks;
    VALIDATE();
}

void block_allocator::alloc_block_at(uint64_t size, uint64_t offset) {
    struct blockpair p(offset, size);

    // Just do a linear search for the block.
    // This data structure is a sorted array (no gaps or anything), so the search isn't really making this any slower than the insertion.
    // To speed up the insertion when opening a file, we provide the block_allocator_alloc_blocks_at function.
    alloc_blocks_at(1, &p);
}

// Effect: align a value by rounding up.
static inline uint64_t align(uint64_t value, uint64_t ba_alignment) {
    return ((value + ba_alignment - 1) / ba_alignment) * ba_alignment;
}

struct block_allocator::blockpair *
block_allocator::choose_block_to_alloc_after(size_t size) {
    switch (_strategy) {
    case BA_STRATEGY_FIRST_FIT:
        return block_allocator_strategy::first_fit(_blocks_array, _n_blocks, size, _alignment);
    default:
        abort();
    }
}

// Effect: Allocate a block. The resulting block must be aligned on the ba->alignment (which to make direct_io happy must be a positive multiple of 512).
void block_allocator::alloc_block(uint64_t size, uint64_t *offset) {
    // Allocator does not support size 0 blocks. See block_allocator_free_block.
    invariant(size > 0);

    grow_blocks_array();
    _n_bytes_in_use += size;

    // First and only block
    if (_n_blocks == 0) {
        assert(_n_bytes_in_use == _reserve_at_beginning + size); // we know exactly how many are in use
        _blocks_array[0].offset = align(_reserve_at_beginning, _alignment);
        _blocks_array[0].size = size;
        *offset = _blocks_array[0].offset;
        _n_blocks++;
        return;
    }

    // Check to see if the space immediately after the reserve is big enough to hold the new block.
    uint64_t end_of_reserve = align(_reserve_at_beginning, _alignment);
    if (end_of_reserve + size <= _blocks_array[0].offset ) {
        struct blockpair *bp = &_blocks_array[0];
        memmove(bp + 1, bp, _n_blocks * sizeof(*bp));
        bp[0].offset = end_of_reserve;
        bp[0].size = size;
        _n_blocks++;
        *offset = end_of_reserve;
        VALIDATE();
        return;
    }

    struct blockpair *bp = choose_block_to_alloc_after(size);
    if (bp != nullptr) {
        // our allocation strategy chose the space after `bp' to fit the new block
        uint64_t answer_offset = align(bp->offset + bp->size, _alignment);
        uint64_t blocknum = bp - _blocks_array;
        assert(&_blocks_array[blocknum] == bp);
        memmove(bp + 2, bp + 1, (_n_blocks - blocknum - 1) * sizeof(*bp));
        bp[1].offset = answer_offset;
        bp[1].size = size;
        *offset = answer_offset;
    } else {
        // It didn't fit anywhere, so fit it on the end.
        assert(_n_blocks < _blocks_array_size);
        bp = &_blocks_array[_n_blocks];
        uint64_t answer_offset = align(bp[-1].offset + bp[-1].size, _alignment);
        bp->offset = answer_offset;
        bp->size = size;
        *offset = answer_offset;
    }
    _n_blocks++;
    VALIDATE();
}

// Find the index in the blocks array that has a particular offset.  Requires that the block exist.
// Use binary search so it runs fast.
int64_t block_allocator::find_block(uint64_t offset) {
    VALIDATE();
    if (_n_blocks == 1) {
        assert(_blocks_array[0].offset == offset);
        return 0;
    }

    uint64_t lo = 0;
    uint64_t hi = _n_blocks;
    while (1) {
        assert(lo<hi); // otherwise no such block exists.
        uint64_t mid = (lo+hi)/2;
        uint64_t thisoff = _blocks_array[mid].offset;
        if (thisoff < offset) {
            lo = mid+1;
        } else if (thisoff > offset) {
            hi = mid;
        } else {
            return mid;
        }
    }
}

// To support 0-sized blocks, we need to include size as an input to this function.
// All 0-sized blocks at the same offset can be considered identical, but
// a 0-sized block can share offset with a non-zero sized block.
// The non-zero sized block is not exchangable with a zero sized block (or vice versa),
// so inserting 0-sized blocks can cause corruption here.
void block_allocator::free_block(uint64_t offset) {
    VALIDATE();
    int64_t bn = find_block(offset);
    assert(bn >= 0); // we require that there is a block with that offset.
    _n_bytes_in_use -= _blocks_array[bn].size;
    memmove(&_blocks_array[bn], &_blocks_array[bn +1 ],
            (_n_blocks - bn - 1) * sizeof(struct blockpair));
    _n_blocks--;
    VALIDATE();
}

uint64_t block_allocator::block_size(uint64_t offset) {
    int64_t bn = find_block(offset);
    assert(bn >=0); // we require that there is a block with that offset.
    return _blocks_array[bn].size;
}

uint64_t block_allocator::allocated_limit() const {
    if (_n_blocks == 0) {
        return _reserve_at_beginning;
    } else {
        struct blockpair *last = &_blocks_array[_n_blocks - 1];
        return last->offset + last->size;
    }
}

// Effect: Consider the blocks in sorted order.  The reserved block at the beginning is number 0.  The next one is number 1 and so forth.
// Return the offset and size of the block with that number.
// Return 0 if there is a block that big, return nonzero if b is too big.
int block_allocator::get_nth_block_in_layout_order(uint64_t b, uint64_t *offset, uint64_t *size) {
    if (b ==0 ) {
        *offset = 0;
        *size = _reserve_at_beginning;
        return  0;
    } else if (b > _n_blocks) {
        return -1;
    } else {
        *offset =_blocks_array[b - 1].offset;
        *size =_blocks_array[b - 1].size;
        return 0;
    }
}

// Requires: report->file_size_bytes is filled in
// Requires: report->data_bytes is filled in
// Requires: report->checkpoint_bytes_additional is filled in
void block_allocator::get_unused_statistics(TOKU_DB_FRAGMENTATION report) {
    assert(_n_bytes_in_use == report->data_bytes + report->checkpoint_bytes_additional);

    report->unused_bytes = 0;
    report->unused_blocks = 0;
    report->largest_unused_block = 0;
    if (_n_blocks > 0) {
        //Deal with space before block 0 and after reserve:
        {
            struct blockpair *bp = &_blocks_array[0];
            assert(bp->offset >= align(_reserve_at_beginning, _alignment));
            uint64_t free_space = bp->offset - align(_reserve_at_beginning, _alignment);
            if (free_space > 0) {
                report->unused_bytes += free_space;
                report->unused_blocks++;
                if (free_space > report->largest_unused_block) {
                    report->largest_unused_block = free_space;
                }
            }
        }

        //Deal with space between blocks:
        for (uint64_t blocknum = 0; blocknum +1 < _n_blocks; blocknum ++) {
            // Consider the space after blocknum
            struct blockpair *bp = &_blocks_array[blocknum];
            uint64_t this_offset = bp[0].offset;
            uint64_t this_size   = bp[0].size;
            uint64_t end_of_this_block = align(this_offset+this_size, _alignment);
            uint64_t next_offset = bp[1].offset;
            uint64_t free_space  = next_offset - end_of_this_block;
            if (free_space > 0) {
                report->unused_bytes += free_space;
                report->unused_blocks++;
                if (free_space > report->largest_unused_block) {
                    report->largest_unused_block = free_space;
                }
            }
        }

        //Deal with space after last block
        {
            struct blockpair *bp = &_blocks_array[_n_blocks-1];
            uint64_t this_offset = bp[0].offset;
            uint64_t this_size   = bp[0].size;
            uint64_t end_of_this_block = align(this_offset+this_size, _alignment);
            if (end_of_this_block < report->file_size_bytes) {
                uint64_t free_space  = report->file_size_bytes - end_of_this_block;
                assert(free_space > 0);
                report->unused_bytes += free_space;
                report->unused_blocks++;
                if (free_space > report->largest_unused_block) {
                    report->largest_unused_block = free_space;
                }
            }
        }
    } else {
        // No blocks.  Just the reserve.
        uint64_t end_of_this_block = align(_reserve_at_beginning, _alignment);
        if (end_of_this_block < report->file_size_bytes) {
            uint64_t free_space  = report->file_size_bytes - end_of_this_block;
            assert(free_space > 0);
            report->unused_bytes += free_space;
            report->unused_blocks++;
            if (free_space > report->largest_unused_block) {
                report->largest_unused_block = free_space;
            }
        }
    }
}

void block_allocator::validate() const {
    uint64_t n_bytes_in_use = _reserve_at_beginning;
    for (uint64_t i = 0; i < _n_blocks; i++) {
        n_bytes_in_use += _blocks_array[i].size;
        if (i > 0) {
            assert(_blocks_array[i].offset >  _blocks_array[i - 1].offset);
            assert(_blocks_array[i].offset >= _blocks_array[i - 1].offset + _blocks_array[i - 1].size );
        }
    }
    assert(n_bytes_in_use == _n_bytes_in_use);
}
