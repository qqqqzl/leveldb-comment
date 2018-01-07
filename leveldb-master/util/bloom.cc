// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/filter_policy.h"

#include "leveldb/slice.h"
#include "util/hash.h"

namespace leveldb {

namespace {
static uint32_t BloomHash(const Slice& key) {
  return Hash(key.data(), key.size(), 0xbc9f1d34);
}

class BloomFilterPolicy : public FilterPolicy {
 private:

  // bits_per_key_存储的是每个key加入到Bloom过滤器时在位缓冲区占的位数
  size_t bits_per_key_;

  // k_存储的是key值加入到Bloom过滤器时需要执行的hash次数。
  size_t k_;

 public:
  explicit BloomFilterPolicy(int bits_per_key)
      : bits_per_key_(bits_per_key) {
    // We intentionally round down to reduce probing cost a little bit
    k_ = static_cast<size_t>(bits_per_key * 0.69);  // 0.69 =~ ln(2)
    if (k_ < 1) k_ = 1;
    if (k_ > 30) k_ = 30;
  }

 //Name()方法返回Bloom过滤器的名字。 
  virtual const char* Name() const {
    return "leveldb.BuiltinBloomFilter2";
  }

  // CreateFilter()用于创建Bloom过滤器
  virtual void CreateFilter(const Slice* keys, int n, std::string* dst) const {
    // Compute bloom filter size (in both bits and bytes)
    // 计算n个key总共会占用的位数
    size_t bits = n * bits_per_key_;

    // For small n, we can see a very high false positive rate.  Fix it
    // by enforcing a minimum bloom filter length.
    if (bits < 64) bits = 64;

	 // 计算总位数向上规整后所需要的字节数，并得出最后需要的位数
    size_t bytes = (bits + 7) / 8;
    bits = bytes * 8;

	// 对dst进行精准扩容，接着在原有内容后面追加每个key加入到Bloom过滤器时
	// 需要进行hash的次数。然后遍历所有的key值，将每个key值进行hash，得到的
	// hash值对总位数求余，然后利用余数将array数组中对应的二进制位置位。
	// 循环k_次之后array数组中key对应的位就都置位了。
    const size_t init_size = dst->size();
    dst->resize(init_size + bytes, 0);
    dst->push_back(static_cast<char>(k_));  // Remember # of probes in filter
    char* array = &(*dst)[init_size];
    for (int i = 0; i < n; i++) {
      // Use double-hashing to generate a sequence of hash values.
      // See analysis in [Kirsch,Mitzenmacher 2006].
      uint32_t h = BloomHash(keys[i]);
      const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
      for (size_t j = 0; j < k_; j++) {
        const uint32_t bitpos = h % bits;
        array[bitpos/8] |= (1 << (bitpos % 8));
        h += delta;
      }
    }
  }

  // KeyMayMatch()函数用于判断key是否在bloom过滤器中，其判断逻辑为：
  // 1. 从bloom_filter中取出创建过滤器时存入到其中的key需要hash的次数。
  // 2. 计算key对应的bloom hash值。并计算出delta值。
  // 3. 然后循环判断每一次hash的结果在bloom filter中对应的位是否被置位，如果某次
  //    循环计算得到的hash值在bloom filter中对应的位没有被置位，说明key一定不在
  //    bloom filter中，如果所有的hash值在bloom filter中的位都处于被置位状态，那么
  //    key值很有可能在bloom filter中。
  virtual bool KeyMayMatch(const Slice& key, const Slice& bloom_filter) const {
    const size_t len = bloom_filter.size();
    if (len < 2) return false;

    const char* array = bloom_filter.data();
    const size_t bits = (len - 1) * 8;

    // Use the encoded k so that we can read filters generated by
    // bloom filters created using different parameters.
    const size_t k = array[len-1];
    if (k > 30) {
      // Reserved for potentially new encodings for short bloom filters.
      // Consider it a match.
      return true;
    }

    uint32_t h = BloomHash(key);
    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
    for (size_t j = 0; j < k; j++) {
	  // 用hash值对总位数求余，得到本次hash值在bloom filter中对应的bit位。
      const uint32_t bitpos = h % bits;
      if ((array[bitpos/8] & (1 << (bitpos % 8))) == 0) return false;
      h += delta;
    }
    return true;
  }
};
}

// NewBloomFilterPolicy()函数用于创建一个BloomFilterPolicy类实例。
const FilterPolicy* NewBloomFilterPolicy(int bits_per_key) {
  return new BloomFilterPolicy(bits_per_key);
}

}  // namespace leveldb