#include "ContigMethods.hpp"

static const char beta[4] = {'T','G','A','C'}; // c -> beta[(c & 7) >> 1] maps: 'A' <-> 'T', 'C' <-> 'G'

// use:  getMappingInfo(repequal, pos, dist, kmernum, cmppos)
// pre:  
// post: cmppos is the first character after the kmer-match at position pos
void getMappingInfo(const bool repequal, const int32_t pos, const size_t dist, size_t &kmernum, int32_t &cmppos) {
  size_t k = Kmer::k; 
  // Now we find the right location of the kmer inside the contig
  // to increase coverage 
  if (pos >= 0) {
    if (repequal) {
      cmppos = pos - dist + k;
      kmernum = cmppos - k;
    } else {
      cmppos = pos - 1 + dist;
      kmernum = cmppos +1;
    }
  } else {
    if (repequal) {
      cmppos = -pos + dist -k;
      kmernum = cmppos +1;
    } else {
      cmppos = -pos + 1 - dist; // Original: (-pos +1 -k) - dist + k
      kmernum = cmppos - k;
    }
  }
}


// use:  cc = check_contig_(bf,km,mapper);
// pre:  
// post: if km does not map to a contig: cc.cr.isEmpty() == true and cc.dist == 0
//       else: km is in a contig which cc.cr maps to and cc.dist is the distance 
//             from km to the mapping location 
//             (cc.eq == true):  km has the same direction as the contig
//             else:  km has the opposite direction to the contig
CheckContig check_contig(BloomFilter &bf, KmerMapper &mapper, Kmer km) {
  ContigRef cr = mapper.find(km);
  if (!cr.isEmpty()) {
    return CheckContig(cr, 0, km == km.rep());
  }
  int i, j;
  size_t dist = 1;
  bool found = false;
  Kmer end = km;
  while (dist < mapper.stride) {
    size_t fw_count = 0;
    j = -1;
    for (i = 0; i < 4; ++i) {
      Kmer fw_rep = end.forwardBase(alpha[i]).rep();
      if (bf.contains(fw_rep)) {
        j = i;
        ++fw_count;
        if (fw_count > 1) {
          break;
        }
      }
    }

    if (fw_count != 1) {
      break;
    }

    Kmer fw = end.forwardBase(alpha[j]);

    size_t bw_count = 0;
    for (i = 0; i < 4; ++i) {
      Kmer bw_rep = fw.backwardBase(alpha[i]).rep();
      if (bf.contains(bw_rep)) {
        ++bw_count;
        if (bw_count > 1) {
          break;
        }
      }
    }

    assert(bw_count >= 1);
    if (bw_count != 1) {
      break;
    }
    cr = mapper.find(fw);
    end = fw;
    if (!cr.isEmpty()) {
      found = true;
      break;
    }
    ++dist;
  }
  if (found) {
    return CheckContig(cr, dist, end == end.rep());
  } else {
    return CheckContig(ContigRef(), 0, 0);
  }
}

// use:  mc = make_contig(bf, mapper, km, s);
// pre:  km is not contained in a mapped contig in mapper 
// post: Finds the forward and backward limits of the contig
//       which contains km  according to the bloom filter bf and puts it into mc.seq
//       mc.pos is the position where km maps into this contig
MakeContig make_contig(BloomFilter &bf, KmerMapper &mapper, Kmer km) {
  size_t k = Kmer::k;
  string seq;
  FindContig fc_fw = find_contig_forward(bf, km);
  int selfloop = fc_fw.selfloop;

  if (selfloop == 0) {
    // Case 0: Regular contig, grow it backwards if possible
  } else if (selfloop == 1) {
    // Found a regular self-looping contig: 
    // Case 1: firstkm -> ... ->lastkm -> firstkm -> ... ->lastkm
    // We don't want to grow the contig backwards, it would duplicate kmers
    return MakeContig(fc_fw.s, selfloop, 0); 
  } else if (selfloop == 2) {
    // Reverse self-looped found on forward strand but maybe we don't have all the contig yet
    // Reversely self-looped contigs can namely behave in three ways:
    // Case 2a) firstkm -> ... -> lastkm -> twin(lastkm) -> ... -> twin(firstkm)
    // Case 2b) twin(lastkm) -> ... -> twin(firstkm) -> firstkm -> ... -> lastkm
    // Case 2c) firstkm -> ... -> lastkm -> twin(lastkm) -> ... -> twin(firstkm) -> firstkm -> ... -> lastkm -> ... (can repeat infinitely)
    // We continue backwards because km is maybe not equal to firstkm
  } 

  FindContig fc_bw = find_contig_forward(bf, km.twin());
  ContigRef cr_tw_end = mapper.find(fc_bw.end);
  assert(cr_tw_end.isEmpty());

  if (fc_bw.selfloop == 1) { 
    // According to the BF, km is contained in a regularly self-looping contig. 
    // Since fc_fw.selfloop != 1 there are two connections from km, into the loop or out of it
    // We although have: Case 1
    assert(fc_fw.dist == 1);  
  }

  if (fc_bw.selfloop == 2) {
    // Reverse self-loop found on backward strand
    // if selfloop == 0 we have Case 2b
    // if selfloop == 2 we have Case 2c
    selfloop = fc_bw.selfloop;
  }

  // post: seq == twin(fc_bw.s)[:-k] + fc_fw.s
  if (fc_bw.dist > 1) {
    seq.reserve(fc_bw.s.size() + fc_fw.s.size() - k);
    for (size_t j = fc_bw.s.size() - 1; j >= k; --j) {
      seq.push_back(beta[(fc_bw.s[j] & 7) >> 1]);
    }
    seq += fc_fw.s;
  } else {
    seq = fc_fw.s;
  }

  return MakeContig(seq, selfloop, fc_bw.dist - 1);
}
