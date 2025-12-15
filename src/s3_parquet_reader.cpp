#include "s3_parquet_reader.h"

#include <algorithm>
#include <fstream>

#include "arrow/api.h"
#include "arrow/io/api.h"
#include "aws/core/auth/AWSCredentials.h"
#include "aws/s3/S3Client.h"
#include "aws/s3/model/GetObjectRequest.h"
#include "fmt/format.h"
#include "parquet/arrow/reader.h"

S3ParquetReader::S3ParquetReader(const Config& config) : _config(config) {
    // Initialize S3 client with credentials
    Aws::Auth::AWSCredentials credentials(config.access_key_id, config.access_key_secret);
    Aws::Client::ClientConfiguration client_config;
    client_config.region = config.region;
    client_config.endpointOverride = config.endpoint;
    _client = std::make_shared<Aws::S3::S3Client>(credentials, nullptr, client_config);
}

S3ParquetReader::~S3ParquetReader() = default;

arrow::Result<std::shared_ptr<arrow::Table>> S3ParquetReader::readParquetFromS3() const {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(_config.bucket_name);
    request.SetKey(_config.object_key);

    auto outcome = _client->GetObject(request);
    if (!outcome.IsSuccess()) {
        return arrow::Status::IOError(fmt::format("Failed to download file from S3", outcome.GetError().GetMessage()));
    }

    std::ostringstream buffer;
    buffer << outcome.GetResult().GetBody().rdbuf();

    const auto buffer_reader = std::make_shared<arrow::io::BufferReader>(arrow::Buffer::FromString(buffer.str()));
    ARROW_ASSIGN_OR_RAISE(const auto arrow_reader,
                          parquet::arrow::OpenFile(buffer_reader, arrow::default_memory_pool()));

    std::shared_ptr<arrow::Table> table;
    ARROW_RETURN_NOT_OK(arrow_reader->ReadTable(_config.column_indices, &table));
    return table;
}
