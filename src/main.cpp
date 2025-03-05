#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <benchmark/benchmark.h>
#include <constants.h>

#include <fstream>

using namespace Aws;
using namespace Aws::S3;
using namespace Aws::S3::Model;

namespace {
const char *KEY = "key";
const char *LOG_TAG = "s3benchmark";
const int ITERATIONS = 20;
}

static SDKOptions s_options;

static void CreateBucketUploadTestObjects() {
  S3Client client{};

  // Create the bucket if it doesnt exist
  auto list_buckets_outcome = client.ListBuckets();
  assert(list_buckets_outcome.IsSuccess());
  auto listed_buckets = list_buckets_outcome.GetResult().GetBuckets();
  if (std::ranges::find_if(listed_buckets, [](const Bucket &bucket) { return bucket.GetName() == bucket_name; }) == listed_buckets.end()) {
    auto create_bucket_output = client.CreateBucket(CreateBucketRequest().WithBucket(bucket_name));
    assert(create_bucket_output.IsSuccess());
    benchmark::DoNotOptimize(create_bucket_output);
  }

  // Create Objects if they dont exist
  auto list_objects_outcome = client.ListObjectsV2(ListObjectsV2Request().WithBucket(bucket_name));
  assert(list_objects_outcome.IsSuccess());
  Vector<Object> objects{};
  objects.insert(objects.end(), list_objects_outcome.GetResult().GetContents().begin(), list_objects_outcome.GetResult().GetContents().end());
  while (!list_objects_outcome.GetResult().GetContinuationToken().empty()) {
    list_objects_outcome = client.ListObjectsV2(ListObjectsV2Request().WithBucket(bucket_name).WithContinuationToken(list_objects_outcome.GetResult().GetContinuationToken()));
    assert(list_objects_outcome.IsSuccess());
    objects.insert(objects.end(), list_objects_outcome.GetResult().GetContents().begin(), list_objects_outcome.GetResult().GetContents().end());
  }
  if (std::ranges::find_if(objects, [](const Object& object) { return object.GetKey() == KEY; }) == objects.end()) {
    auto put_object_request = PutObjectRequest().WithBucket(bucket_name).WithKey(KEY);
    std::shared_ptr<IOStream> body = Aws::MakeShared<FStream>(LOG_TAG, file_location, std::ios::binary | std::ios::in);
    put_object_request.SetBody(body);
    auto put_object_response = client.PutObject(put_object_request);
    assert(put_object_response.IsSuccess());
    benchmark::DoNotOptimize(put_object_response);
  }
}

static void DoSetup(const benchmark::State &state) {
  InitAPI(s_options);
  {
    CreateBucketUploadTestObjects();
  }
}

static void DoTeardown(const benchmark::State &state) {
  ShutdownAPI(s_options);
}

static void BM_S3GetObject(benchmark::State& state) {
  S3Client client{};
  for (auto _ : state) {
    auto get_object_output = client.GetObject(GetObjectRequest().WithBucket(bucket_name).WithKey(KEY));
    assert(get_object_output.IsSuccess());
    benchmark::DoNotOptimize(get_object_output);
  }
}

BENCHMARK(BM_S3GetObject)
    ->Setup(DoSetup)
    ->Teardown(DoTeardown)
    ->MeasureProcessCPUTime()
    ->Iterations(ITERATIONS)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();