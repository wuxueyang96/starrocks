#include <iostream>

#include "encoder.h"
#include "encoder_factory.h"
#include "roaring/roaring.hh"

int main() {
    auto encoder_ = EncoderFactory::createEncoder(EncodingType::VARINT);

    roaring::Roaring original;
    original.add(42);

    std::vector<uint8_t> encoded = encoder_->encode(original);
    std::cout << "encoded bytes: " << encoded.size() << std::endl;

    roaring::Roaring decoded = encoder_->decode(encoded);
    std::cout << "decoded: " << decoded.cardinality() << std::endl;
    std::cout << "contains origin val: " << decoded.contains(42) << std::endl;
    return 0;
}