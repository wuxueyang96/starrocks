#pragma once

#include <memory>
#include <string>

#include "arrow/api.h"
#include "aws/core/Aws.h"
#include "config.h"

namespace Aws::S3 {
class S3Client;
}

class AwsSdkInitializer {
public:
    AwsSdkInitializer() { Aws::InitAPI(options); }
    ~AwsSdkInitializer() { Aws::ShutdownAPI(options); }

private:
    Aws::SDKOptions options{};
};

class S3ParquetReader {
public:
    explicit S3ParquetReader(const Config& config);
    ~S3ParquetReader();

    // Read parquet file from S3 and return table
    arrow::Result<std::shared_ptr<arrow::Table>> readParquetFromS3() const;

private:
    const Config _config;
    std::shared_ptr<Aws::S3::S3Client> _client;
};
