#include "encoder_factory.h"
#include "varint_encoder.h"
#include "for_encoder.h"
#include "pfor_delta_encoder.h"
#include "simple9_encoder.h"
#include "new_pfor_delta_encoder.h"
#include "adaptive_encoder.h"

std::shared_ptr<Encoder> EncoderFactory::createEncoder(const EncodingType type) {
    switch (type) {
    case EncodingType::VARINT:
        return std::make_shared<VarIntEncoder>();
    case EncodingType::FOR_VARINT:
        return std::make_shared<FrameOfReferenceEncoder>();
    case EncodingType::PFOR_DELTA:
        return std::make_shared<PForDeltaEncoder>();
    case EncodingType::SIMPLE9:
        return std::make_shared<Simple9Encoder>();
    case EncodingType::NEW_PFOR_DELTA:
        return std::make_shared<NewPForDeltaEncoder>();
    case EncodingType::ADAPTIVE:
    default:
        return std::make_shared<AdaptiveEncoder>();
    }
}
