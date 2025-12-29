#include <iostream>

#include "encoder.h"
#include "encoder_factory.h"
#include "roaring/roaring.hh"

int main() {
    auto encoder_ = EncoderFactory::createEncoder(EncodingType::VARINT);

    roaring::Roaring original;
    original.add(42);

    std::vector<uint8_t> encoded;
    Status status = encoder_->encode(original, &encoded);
    std::cout << "encode status: " << (status == Status::OK ? "OK" : "FAILED") << std::endl;
    std::cout << "encoded bytes: " << encoded.size() << std::endl;
    return 0;
}