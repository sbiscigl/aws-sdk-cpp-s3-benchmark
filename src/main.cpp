#include <aws/core/Aws.h>
#include <aws/s3-crt/S3CrtClient.h>
#include <aws/s3-crt/model/CreateBucketRequest.h>
#include <aws/s3-crt/model/GetObjectRequest.h>
#include <aws/s3-crt/model/ListObjectsV2Request.h>
#include <aws/s3-crt/model/PutObjectRequest.h>
#include <benchmark/benchmark.h>
#include <constants.h>

#include <fstream>

using namespace Aws;
using namespace Aws::S3Crt;
using namespace Aws::S3Crt::Model;

namespace {
const char *KEY = "key";
const char *LOG_TAG = "s3benchmark";
const size_t FILES_TO_UPLOAD = 10;
const int ITERATIONS = 1;
const int64_t TOTAL_TRANFSER_BYTES = 30L * FILES_TO_UPLOAD * 1024 * 1024 * 1024 * 8;
}  // namespace

static SDKOptions s_options;

static void CreateBucketUploadTestObjects() {
  S3CrtClient client{};

  // Create the bucket if it doesnt exist
  auto list_buckets_outcome = client.ListBuckets();
  assert(list_buckets_outcome.IsSuccess());
  auto listed_buckets = list_buckets_outcome.GetResult().GetBuckets();
  if (std::ranges::find_if(listed_buckets, [](const Bucket &bucket) { return bucket.GetName() == bucket_name; }) == listed_buckets.end()) {
    auto create_bucket_output = client.CreateBucket(CreateBucketRequest().WithBucket(bucket_name));
    assert(create_bucket_output.IsSuccess());
    benchmark::DoNotOptimize(create_bucket_output);
  }

  // Create the bucket if it doesnt exist
  auto list_directory_buckets_outcome = client.ListDirectoryBuckets();
  assert(list_directory_buckets_outcome.IsSuccess());
  auto directory_buckets = list_directory_buckets_outcome.GetResult().GetBuckets();
  if (std::ranges::find_if(directory_buckets, [](const Bucket &bucket) { return bucket.GetName() == express_bucket_name; }) == listed_buckets.end()) {
    auto create_directory_bucket = client.CreateBucket(
        CreateBucketRequest{}
            .WithBucket(express_bucket_name)
            .WithCreateBucketConfiguration(CreateBucketConfiguration()
                                               .WithLocation(LocationInfo().WithType(LocationType::AvailabilityZone).WithName("use1-az6"))
                                               .WithBucket(BucketInfo().WithType(BucketType::Directory).WithDataRedundancy(DataRedundancy::SingleAvailabilityZone))));
    assert(create_directory_bucket.IsSuccess());
    benchmark::DoNotOptimize(create_directory_bucket);
  }

  for (auto bucket : {bucket_name, express_bucket_name}) {
    // Create Objects if they dont exist
    auto list_objects_outcome = client.ListObjectsV2(ListObjectsV2Request().WithBucket(bucket));
    assert(list_objects_outcome.IsSuccess());
    Vector<Object> objects{};
    objects.insert(objects.end(), list_objects_outcome.GetResult().GetContents().begin(), list_objects_outcome.GetResult().GetContents().end());
    while (!list_objects_outcome.GetResult().GetContinuationToken().empty()) {
      list_objects_outcome = client.ListObjectsV2(ListObjectsV2Request().WithBucket(bucket).WithContinuationToken(list_objects_outcome.GetResult().GetContinuationToken()));
      assert(list_objects_outcome.IsSuccess());
      objects.insert(objects.end(), list_objects_outcome.GetResult().GetContents().begin(), list_objects_outcome.GetResult().GetContents().end());
    }
    if (std::ranges::find_if(objects, [](const Object &object) { return object.GetKey() == KEY; }) == objects.end()) {
      auto put_object_request = PutObjectRequest().WithBucket(bucket).WithKey(KEY);
      std::shared_ptr<IOStream> body = Aws::MakeShared<FStream>(LOG_TAG, file_location, std::ios::binary | std::ios::in);
      put_object_request.SetBody(body);
      auto put_object_response = client.PutObject(put_object_request);
      assert(put_object_response.IsSuccess());
      benchmark::DoNotOptimize(put_object_response);
    }
  }
}

static void DoSetup(const benchmark::State &state) {
  s_options.ioOptions.clientBootstrap_create_fn = []() -> std::shared_ptr<Aws::Crt::Io::ClientBootstrap> {
    Aws::Crt::Io::EventLoopGroup eventLoopGroup{480};
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30);
    auto clientBootstrap = Aws::MakeShared<Aws::Crt::Io::ClientBootstrap>(LOG_TAG, eventLoopGroup, defaultHostResolver);
    clientBootstrap->EnableBlockingShutdown();
    return clientBootstrap;
  };

  InitAPI(s_options);
  {
    CreateBucketUploadTestObjects();
  }
}

static void DoTeardown(const benchmark::State &state) { ShutdownAPI(s_options); }

static void UploadFilesAsync(const S3CrtClient &client, const Aws::String &bucketName) {
  size_t calls_complete{0};
  std::mutex call_complete_mx;
  std::condition_variable call_complete_cv;
  for (size_t n = 0; n < FILES_TO_UPLOAD; n++) {
    const auto current_key = KEY + std::to_string(n);
    auto put_object_request = PutObjectRequest().WithBucket(bucketName).WithKey(current_key);
    put_object_request.SetBody(Aws::MakeShared<FStream>(LOG_TAG, file_location, std::ios::binary | std::ios::in));
    client.PutObjectAsync(put_object_request,
                          [&call_complete_mx, &calls_complete, &call_complete_cv](const S3CrtClient *, const Model::PutObjectRequest &, const Model::PutObjectOutcome &outcome,
                                                                                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &) -> void {
                            {
                              std::unique_lock lock{call_complete_mx};
                              assert(outcome.IsSuccess());
                              calls_complete++;
                            }
                            call_complete_cv.notify_one();
                          });
  }
  std::unique_lock lock{call_complete_mx};
  call_complete_cv.wait(lock, [&calls_complete] { return calls_complete == FILES_TO_UPLOAD; });
}

static void DownloadFilesAsync(const S3CrtClient &client, const Aws::String &bucketName) {
  size_t calls_complete{0};
  std::mutex call_complete_mx;
  std::condition_variable call_complete_cv;
  for (size_t n = 0; n < FILES_TO_UPLOAD; n++) {
    const auto current_key = KEY + std::to_string(n);
    auto get_object_request = GetObjectRequest().WithBucket(bucketName).WithKey(current_key);
    get_object_request.SetResponseStreamFactory([] { return Aws::New<FStream>("FStreamAllocationTag", "/dev/null", std::ios_base::out); });
    client.GetObjectAsync(get_object_request,
                          [&call_complete_mx, &calls_complete, &call_complete_cv](const S3CrtClient *, const Model::GetObjectRequest &, const Model::GetObjectOutcome &outcome,
                                                                                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &) -> void {
                            {
                              std::unique_lock lock{call_complete_mx};
                              assert(outcome.IsSuccess());
                              benchmark::DoNotOptimize(outcome.GetResult().GetBody());
                              calls_complete++;
                            }
                            call_complete_cv.notify_one();
                          });
  }
  std::unique_lock lock{call_complete_mx};
  call_complete_cv.wait(lock, [&calls_complete] { return calls_complete == FILES_TO_UPLOAD; });
}

static void BM_S3PutObjectOneNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32"};
  client_configuration.throughputTargetGbps = 100.0;
  S3CrtClient client{client_configuration};
  int64_t total_bytes_processed = 0;
  for (auto _ : state) {
    UploadFilesAsync(client, bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3PutObjectTwoNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32", "ens64"};
  client_configuration.throughputTargetGbps = 200.0;
  int64_t total_bytes_processed = 0;
  S3CrtClient client{client_configuration};
  for (auto _ : state) {
    UploadFilesAsync(client, bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3GetObjectOneNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32"};
  client_configuration.throughputTargetGbps = 100.0;
  S3CrtClient client{client_configuration};
  int64_t total_bytes_processed = 0;
  for (auto _ : state) {
    DownloadFilesAsync(client, bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3GetObjectTwoNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32", "ens64"};
  client_configuration.throughputTargetGbps = 200.0;
  S3CrtClient client{client_configuration};
  int64_t total_bytes_processed = 0;
  for (auto _ : state) {
    DownloadFilesAsync(client, bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3ExpressPutObjectOneNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32"};
  client_configuration.throughputTargetGbps = 100.0;
  S3CrtClient client{client_configuration};
  int64_t total_bytes_processed = 0;
  for (auto _ : state) {
    UploadFilesAsync(client, express_bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3ExpressPutObjectTwoNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32", "ens64"};
  client_configuration.throughputTargetGbps = 200.0;
  int64_t total_bytes_processed = 0;
  S3CrtClient client{client_configuration};
  for (auto _ : state) {
    UploadFilesAsync(client, express_bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3ExpressGetObjectOneNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32"};
  client_configuration.throughputTargetGbps = 100.0;
  S3CrtClient client{client_configuration};
  int64_t total_bytes_processed = 0;
  for (auto _ : state) {
    DownloadFilesAsync(client, express_bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

static void BM_S3ExpressGetObjectTwoNic(benchmark::State &state) {
  S3CrtClientConfiguration client_configuration{};
  client_configuration.networkInterfaceNames = {"ens32", "ens64"};
  client_configuration.throughputTargetGbps = 200.0;
  S3CrtClient client{client_configuration};
  int64_t total_bytes_processed = 0;
  for (auto _ : state) {
    DownloadFilesAsync(client, express_bucket_name);
    total_bytes_processed += TOTAL_TRANFSER_BYTES;
  }
  state.SetBytesProcessed(total_bytes_processed);
}

BENCHMARK(BM_S3PutObjectOneNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();
BENCHMARK(BM_S3PutObjectTwoNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();
BENCHMARK(BM_S3ExpressPutObjectOneNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();
BENCHMARK(BM_S3ExpressPutObjectTwoNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();

BENCHMARK(BM_S3GetObjectOneNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();
BENCHMARK(BM_S3GetObjectTwoNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();
BENCHMARK(BM_S3ExpressGetObjectOneNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();
BENCHMARK(BM_S3ExpressGetObjectTwoNic)->Setup(DoSetup)->Teardown(DoTeardown)->Iterations(ITERATIONS)->Unit(benchmark::kSecond)->UseRealTime();

BENCHMARK_MAIN();
