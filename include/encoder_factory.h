#pragma once

#include "encoder.h"

#include <memory>

/**
 * Factory class for creating encoder instances
 */
class EncoderFactory {
public:
    /**
     * Create an encoder instance based on encoding type
     * @param type The encoding type
     * @return Shared pointer to the encoder
     */
    static std::shared_ptr<Encoder> createEncoder(EncodingType type);
};
